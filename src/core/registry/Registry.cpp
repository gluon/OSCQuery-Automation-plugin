/*
    This file is part of the OSCQuery Automation Plugin —
    https://github.com/pob31/OSCQuery-Automation-plugin

    Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle

    The OSCQuery Automation Plugin is free software: you can redistribute it
    and/or modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the License,
    or (at your option) any later version. It is distributed in the hope that it
    will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
    Public License (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <core/registry/Registry.h>

#include <core/registry/ChangeRing.h>
#include <core/registry/Wake.h>
#include <core/rt/RtCheck.h>
#include <core/send/RateLimiter.h>
#include <core/send/TimeTags.h>

#include <oscquery/Device.h>
#include <oscquery/net/RawUdpSender.h>
#include <oscquery/net/TcpSender.h>
#include <oscquery/osc/Bundler.h>
#include <oscquery/osc/Codec.h>
#include <oscquery/osc/FixedEncoder.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace oqa::core
{
    using oscquery::osc::Value;

    namespace
    {
        constexpr std::size_t ringCapacity = 8192;
        constexpr std::size_t incomingCapacity = 4096;
        constexpr std::size_t datagramBufferBytes = 65536;
        constexpr std::uint16_t noSlot = 0xffff;

        /** The steady clock in nanoseconds: what the loop guard measures with, on any thread. */
        std::int64_t steadyNanos() noexcept
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        std::string keyOf (const std::string& name, const std::string& identityValue)
        {
            return name + "\n" + identityValue;
        }

        std::string keyOf (const DeviceRef& ref)
        {
            return keyOf (ref.name, ref.identityValue.value_or (""));
        }

        /** The JSON of a tree's node at `path`, walking CONTENTS. */
        std::optional<juce::var> nodeAt (const juce::var& root, const std::string& path)
        {
            if (path.empty() || path == "/")
                return root;

            juce::var node = root;
            std::size_t start = 1;

            while (start <= path.size())
            {
                const auto end = path.find ('/', start);
                const auto segment = path.substr (start, end == std::string::npos ? std::string::npos : end - start);

                if (segment.empty())
                    return std::nullopt;

                const auto contents = node.getProperty ("CONTENTS", {});

                if (! contents.isObject() || contents.isArray())
                    return std::nullopt;

                node = contents.getProperty (juce::String (segment), {});

                if (node.isVoid())
                    return std::nullopt;

                if (end == std::string::npos)
                    break;

                start = end + 1;
            }

            return node;
        }
    }

    //==============================================================================
    juce::var RegistrySettings::toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("direct_send", directSend);
        o->setProperty ("default_rate_hz", defaultRateHz);
        o->setProperty ("resend_ms", resendMs);
        o->setProperty ("gather_deadline_us", gatherDeadlineUs);
        o->setProperty ("mtu", static_cast<int> (mtu));
        o->setProperty ("active_window_ms", activeWindowMs);
        o->setProperty ("receive_port", receivePort);
        o->setProperty ("loop_guard_ms", loopGuardMs);
        o->setProperty ("gesture_timeout_ms", gestureTimeoutMs);
        return juce::var (o);
    }

    void RegistrySettings::applyVar (const juce::var& v)
    {
        if (! v.isObject() || v.isArray())
            return;

        if (v.hasProperty ("direct_send"))        directSend = static_cast<bool> (v["direct_send"]);
        if (v.hasProperty ("default_rate_hz"))    defaultRateHz = std::max (0.0, static_cast<double> (v["default_rate_hz"]));
        if (v.hasProperty ("resend_ms"))          resendMs = std::max (0.0, static_cast<double> (v["resend_ms"]));
        if (v.hasProperty ("gather_deadline_us")) gatherDeadlineUs = std::clamp (static_cast<int> (v["gather_deadline_us"]), 0, 20000);
        if (v.hasProperty ("mtu"))                mtu = static_cast<std::size_t> (std::clamp (static_cast<int> (v["mtu"]), 64, 65507));
        if (v.hasProperty ("active_window_ms"))   activeWindowMs = std::clamp (static_cast<int> (v["active_window_ms"]), 10, 60000);
        if (v.hasProperty ("receive_port"))       receivePort = std::clamp (static_cast<int> (v["receive_port"]), 0, 65535);
        if (v.hasProperty ("loop_guard_ms"))      loopGuardMs = std::clamp (static_cast<double> (v["loop_guard_ms"]), 0.0, 10000.0);
        if (v.hasProperty ("gesture_timeout_ms")) gestureTimeoutMs = std::clamp (static_cast<double> (v["gesture_timeout_ms"]), 10.0, 60000.0);
    }

    //==============================================================================
    class DeviceEntry;

    /*  One bound path of one instance: the message the device receives, and
        the one it streams back. */
    struct Group
    {
        std::uint32_t instanceId = 0;
        DeviceEntry* device = nullptr;
        std::string path;
        bool critical = false;
        std::size_t limiterKey = 0;

        /*  Per argument: the slot bound to it (a copy, for scaling), whether a
            sending slot drives it, and the receiving slot's host parameter ID
            (or -1). A slot whose direction is both is driven and received. */
        std::vector<Slot> slots;
        std::vector<bool> driven;
        std::vector<int> receiver;

        /** Per argument: the normalised value last posted by the audio thread (driven arguments). */
        std::unique_ptr<std::atomic<float>[]> known;

        /*  Per argument: the device's value as last known, in its units: the
            tree's at bind time, then whatever the device streams (written on
            the Device thread, read on the network thread). `tags` says how it
            goes on the wire; `initial` keeps the tree's value for an argument
            of a type no slot binds (s, b...), which never changes. */
        std::unique_ptr<std::atomic<double>[]> latest;
        std::string tags;
        std::vector<Value> initial;

        std::atomic<bool> dirty { false };

        /** When this group was last sent (steady clock): the loop guard's reference. 0: never. */
        std::atomic<std::int64_t> lastSentNanos { 0 };

        std::size_t argCount() const noexcept { return slots.size(); }

        /** The wire value of argument `i` now. */
        Value argument (std::size_t i) const noexcept
        {
            if (driven[i])
                return slots[i].deviceValue (known[i].load (std::memory_order_relaxed));

            const double v = latest[i].load (std::memory_order_relaxed);

            switch (tags[i])
            {
                case 'f': return Value::float32 (static_cast<float> (v));
                case 'i':
                {
                    constexpr double lo = static_cast<double> (std::numeric_limits<std::int32_t>::min());
                    constexpr double hi = static_cast<double> (std::numeric_limits<std::int32_t>::max());
                    return Value::int32 (static_cast<std::int32_t> (std::clamp (std::round (v), lo, hi)));
                }
                case 'T':
                case 'F': return Value::boolean (v >= 0.5);
                default:  return initial[i];
            }
        }
    };

    /** One received value on its way to an instance's slot. */
    struct Delivery
    {
        std::uint32_t instance = 0;
        int slot = 0;
        float normalised = 0.0f;
    };

    struct InstancePlan
    {
        std::vector<Group*> groupOfSlot;
        std::vector<std::uint16_t> argOfSlot;
        std::vector<DeviceEntry*> devices;
        std::vector<std::shared_ptr<Group>> groups;
    };

    class Registry::InstanceRecord
    {
    public:
        std::uint32_t id = 0;
        std::string label;
        int slotCount = 0;

        std::atomic<const InstancePlan*> plan { nullptr };
        std::vector<std::unique_ptr<InstancePlan>> plans;       // every plan ever, the live one last

        std::vector<DeviceEntry*> deviceByIndex;                // the chunk's devices[], resolved (message thread)
        std::vector<std::string> warnings;
        std::unique_ptr<StateChunk> lastChunk;                  // what the plan was built from, for a rebuild

        /** The direct-send path's buffer, this instance's own. */
        std::vector<std::uint8_t> directBuffer;
        std::atomic<std::uint64_t> directSent { 0 };

        /*  Record: values from the devices for this instance's receiving
            slots, drained by the shim through pollReceived; the wake it asked
            for, called under the registry's lock so it cannot outlive the
            instance. Full is a counted drop, as the send ring's is. */
        ChangeRing<incomingCapacity> incoming;
        void (*wake) (void* context) = nullptr;
        void* wakeContext = nullptr;
        std::atomic<std::uint64_t> received { 0 };
    };

    //==============================================================================
    class DeviceEntry final : private juce::Thread
    {
    public:
        DeviceEntry (std::string keyIn, DeviceRef refIn, const RegistrySettings& settingsIn,
                     std::function<void (std::vector<Delivery>&)> onReceivedIn,
                     std::function<void (DeviceEntry&)> onTreeLoadedIn)
            : juce::Thread ("oqa device " + juce::String (refIn.name)),
              key (std::move (keyIn)), ref (std::move (refIn)), settings (settingsIn),
              onReceived (std::move (onReceivedIn)), onTreeLoaded (std::move (onTreeLoadedIn)),
              guardMs (settingsIn.loopGuardMs)
        {
            buffer.resize (datagramBufferBytes);
            openDevice();

            /*  Realtime where the system allows it: woken about once a block,
                busy for tens of microseconds. The budget declared is small on
                purpose: macOS demotes a time-constraint thread that overruns
                its computation, and the gather below waits on the semaphore
                rather than spinning, so the thread never does. A high-priority
                thread where realtime is refused. */
            if (! startRealtimeThread (RealtimeOptions {}.withPriority (9).withPeriodMs (1.0)
                                                         .withProcessingTimeMs (0.05).withMaximumProcessingTimeMs (0.25)))
                startThread (Priority::highest);
        }

        ~DeviceEntry() override
        {
            signalThreadShouldExit();
            wake.signal();
            stopThread (3000);

            const std::lock_guard<std::mutex> guard (deviceLock);

            if (device != nullptr)
                device->stop();
        }

        const std::string key;

        //==============================================================================
        //  The audio thread.
        ChangeRing<ringCapacity> ring;
        Wake wake;

        bool directSend (Registry::InstanceRecord& instance, Group& group) noexcept
        {
            if (! senderReady.load (std::memory_order_acquire) || tcp != nullptr || group.critical)
                return false;

            std::array<Value, 16> args;
            const auto n = std::min (group.argCount(), args.size());

            for (std::size_t i = 0; i < n; ++i)
                args[i] = group.argument (i);

            const auto size = oscquery::osc::encodeMessageInto (instance.directBuffer.data(), instance.directBuffer.size(),
                                                                group.path, args.data(), n);

            if (size == 0)
                return false;

            if (udp.send (instance.directBuffer.data(), size))
            {
                group.lastSentNanos.store (steadyNanos(), std::memory_order_relaxed);
                instance.directSent.fetch_add (1, std::memory_order_relaxed);
                messagesSent.fetch_add (1, std::memory_order_relaxed);
                return true;
            }

            return false;
        }

        //==============================================================================
        //  The message thread.

        /*  The instance's groups replace its old ones; the device is then told
            to LISTEN on every path any group holds and to IGNORE the ones none
            does. Under the groups lock first, the device lock after, never
            both: received() runs under the former alone. */
        void setGroups (std::uint32_t instanceId, std::vector<std::shared_ptr<Group>> incoming)
        {
            std::set<std::string> before, after;

            {
                const std::lock_guard<std::mutex> guard (groupsLock);

                for (const auto& g : allGroups)
                    before.insert (g->path);

                std::erase_if (allGroups, [instanceId] (const std::shared_ptr<Group>& g) { return g->instanceId == instanceId; });

                for (auto& g : incoming)
                    allGroups.push_back (std::move (g));

                for (const auto& g : allGroups)
                    after.insert (g->path);

                groupsRevision.fetch_add (1, std::memory_order_release);
            }

            withDevice ([&] (oscquery::Device& d)
            {
                for (const auto& path : after)
                    if (before.count (path) == 0)
                        (void) d.listen (path);

                for (const auto& path : before)
                    if (after.count (path) == 0)
                        d.ignore (path);
            });
        }

        void removeGroups (std::uint32_t instanceId)
        {
            setGroups (instanceId, {});
        }

        void setSettings (const RegistrySettings& s)
        {
            const std::lock_guard<std::mutex> guard (settingsLock);
            settings = s;
            guardMs.store (s.loopGuardMs, std::memory_order_relaxed);
            settingsRevision.fetch_add (1, std::memory_order_release);
        }

        void setAddress (const std::string& host, int port)
        {
            {
                const std::lock_guard<std::mutex> guard (settingsLock);
                ref.lastHost = host;
                ref.lastPort = port;
            }

            addressChanged.store (true, std::memory_order_release);
            wake.signal();
        }

        void setTiming (bool honoursTimetags, double offsetMs)
        {
            const std::lock_guard<std::mutex> guard (settingsLock);
            ref.honoursTimetags = honoursTimetags;
            ref.offsetMs = offsetMs;
        }

        DeviceRef reference() const
        {
            const std::lock_guard<std::mutex> guard (settingsLock);
            return ref;
        }

        template <typename Fn>
        void withDevice (Fn&& fn) const
        {
            const std::lock_guard<std::mutex> guard (deviceLock);

            if (device != nullptr)
                fn (*device);
        }

        /** The tree's VALUE for every argument of `path`, when the tree is loaded. */
        std::vector<Value> initialValues (const std::string& path, std::size_t argCount) const
        {
            std::vector<Value> values (argCount, Value::float32 (0.0f));

            withDevice ([&] (const oscquery::Device& d)
            {
                if (! d.treeLoaded())
                    return;

                d.withTree ([&] (const oscquery::Tree& tree)
                {
                    if (const auto* node = tree.find (path))
                        for (std::size_t i = 0; i < argCount && i < node->argumentCount(); ++i)
                            if (const auto v = node->argument (i).value)
                                values[i] = *v;
                });
            });

            return values;
        }

        bool nodeIsCritical (const std::string& path) const
        {
            bool critical = false;

            withDevice ([&] (const oscquery::Device& d)
            {
                d.withTree ([&] (const oscquery::Tree& tree)
                {
                    if (const auto* node = tree.find (path))
                        critical = node->critical;
                });
            });

            return critical;
        }

        /** The node's TYPE when the tree is loaded and has it; empty otherwise. */
        std::string nodeTags (const std::string& path) const
        {
            std::string tags;

            withDevice ([&] (const oscquery::Device& d)
            {
                if (! d.treeLoaded())
                    return;

                d.withTree ([&] (const oscquery::Tree& tree)
                {
                    if (const auto* node = tree.find (path))
                        if (node->type.has_value())
                            tags = *node->type;
                });
            });

            return tags;
        }

        //==============================================================================
        /*  A VALUE FROM THE DEVICE, on the library's Device thread (LISTEN) or
            the UDP receiver's (4b). The groups at that path, copied under the
            groups lock alone; then, per argument: the value the device holds is
            remembered for the arguments no slot drives; a driven argument that
            echoes what this device was just sent, within the guard window, is
            counted and dropped; what a receiving slot wants is scaled to 0-1 and
            handed to the registry for its instance. */
        void received (const std::string& path, const oscquery::osc::Values& values)
        {
            std::vector<std::shared_ptr<Group>> matching;

            {
                const std::lock_guard<std::mutex> guard (groupsLock);

                for (const auto& g : allGroups)
                    if (g->path == path)
                        matching.push_back (g);
            }

            valuesReceived.fetch_add (1, std::memory_order_relaxed);

            if (matching.empty())
            {
                unmatchedReceived.fetch_add (1, std::memory_order_relaxed);
                return;
            }

            const auto now = steadyNanos();
            const auto guardNanos = static_cast<std::int64_t> (guardMs.load (std::memory_order_relaxed) * 1.0e6);
            std::vector<Delivery> deliveries;

            for (const auto& g : matching)
            {
                const auto n = std::min (values.size(), g->argCount());

                for (std::size_t i = 0; i < n; ++i)
                {
                    const auto& v = values[i];
                    const bool boolean = v.isBool();

                    if (! boolean && ! (v.isNumber() && ! v.isNonFinite()))
                        continue;

                    if (! g->driven[i])
                        g->latest[i].store (boolean ? (v.getBool() ? 1.0 : 0.0) : v.asDouble(), std::memory_order_relaxed);

                    if (g->receiver[i] < 0)
                        continue;

                    const auto host = g->slots[i].hostValue (v);

                    if (! host.has_value())
                        continue;

                    if (g->driven[i])
                    {
                        const auto sentAt = g->lastSentNanos.load (std::memory_order_relaxed);

                        if (sentAt != 0 && now - sentAt < guardNanos
                            && std::abs (*host - g->known[i].load (std::memory_order_relaxed)) <= 1.0e-4f)
                        {
                            loopGuarded.fetch_add (1, std::memory_order_relaxed);
                            continue;
                        }
                    }

                    deliveries.push_back ({ g->instanceId, g->receiver[i], *host });
                }
            }

            if (! deliveries.empty() && onReceived)
                onReceived (deliveries);
        }

        juce::var status() const
        {
            auto* o = new juce::DynamicObject();
            const auto r = reference();

            o->setProperty ("name", juce::String (r.name));
            o->setProperty ("identity_value", r.identityValue.has_value() ? juce::var (juce::String (*r.identityValue)) : juce::var());
            o->setProperty ("host", juce::String (r.lastHost));
            o->setProperty ("port", r.lastPort);
            o->setProperty ("honours_timetags", r.honoursTimetags);
            o->setProperty ("offset_ms", r.offsetMs);

            withDevice ([&] (const oscquery::Device& d)
            {
                const auto state = d.state();
                o->setProperty ("state", juce::String (oscquery::Device::stateName (state)));
                o->setProperty ("reachable", state != oscquery::Device::State::Unprobed
                                          && state != oscquery::Device::State::Unreachable
                                          && state != oscquery::Device::State::Closed);
                o->setProperty ("tree_loaded", d.treeLoaded());

                if (const auto info = d.hostInfo())
                {
                    o->setProperty ("osc_transport", juce::String (info->oscTransport));
                    o->setProperty ("osc_port", info->oscPort);
                }

                o->setProperty ("last_error", juce::String (d.lastError()));

                /*  Where record stands on this device: listening over the
                    websocket; unsupported (no LISTEN in EXTENSIONS: the UDP
                    receiver is the way, 4b); else pending. */
                const auto info = d.hostInfo();
                const char* listen = "pending";

                if (info.has_value() && ! info->has ("LISTEN"))
                    listen = "unsupported";
                else if (state == oscquery::Device::State::Listening)
                    listen = "listening";

                o->setProperty ("listen", listen);
            });

            o->setProperty ("values_received", static_cast<juce::int64> (valuesReceived.load()));
            o->setProperty ("loop_guarded", static_cast<juce::int64> (loopGuarded.load()));
            o->setProperty ("unmatched_received", static_cast<juce::int64> (unmatchedReceived.load()));

            o->setProperty ("sender_ready", senderReady.load());
            o->setProperty ("resolved", juce::String (udp.resolvedAddress()));
            o->setProperty ("bundles_sent", static_cast<juce::int64> (bundlesSent.load()));
            o->setProperty ("messages_sent", static_cast<juce::int64> (messagesSent.load()));
            o->setProperty ("datagrams_dropped", static_cast<juce::int64> (udp.dropped() + ring.dropped()));
            o->setProperty ("send_failures", static_cast<juce::int64> (udp.failures() + tcpFailures.load()));
            o->setProperty ("unsent_no_route", static_cast<juce::int64> (unsent.load()));
            o->setProperty ("websocket_sent", static_cast<juce::int64> (websocketSent.load()));
            o->setProperty ("rounds", static_cast<juce::int64> (rounds.load()));
            o->setProperty ("rate_limited", static_cast<juce::int64> (limiter.held()));
            o->setProperty ("resent", static_cast<juce::int64> (limiter.resent()));

            {
                const std::lock_guard<std::mutex> guard (groupsLock);
                o->setProperty ("groups", static_cast<int> (allGroups.size()));
            }

            return juce::var (o);
        }

    private:
        //==============================================================================
        void openDevice()
        {
            const auto r = reference();

            oscquery::Device::Options options;
            options.host = r.lastHost;
            options.port = r.lastPort;

            /*  The paths to LISTEN on, taken before the device lock: the
                groups lock is never taken under it. */
            std::set<std::string> paths;

            {
                const std::lock_guard<std::mutex> groupsGuard (groupsLock);

                for (const auto& g : allGroups)
                    paths.insert (g->path);
            }

            const std::lock_guard<std::mutex> guard (deviceLock);

            if (device != nullptr)
                device->stop();

            device = std::make_unique<oscquery::Device> (options);

            /*  The tree landing (or changing) is noted for the network thread,
                which has the registry rebuild the plans that were made before
                it: a Device thread never takes the device lock, which the
                thread stopping that device holds. */
            oscquery::Device::Callbacks callbacks;
            callbacks.onValue = [this] (const std::string& path, const oscquery::osc::Values& values) { received (path, values); };
            callbacks.onState = [this] (oscquery::Device::State state)
            {
                if (state == oscquery::Device::State::TreeLoaded || state == oscquery::Device::State::Listening)
                {
                    treeArrived.store (true, std::memory_order_release);
                    wake.signal();
                }
            };
            callbacks.onTreeChanged = [this] (const std::string&)
            {
                treeArrived.store (true, std::memory_order_release);
                wake.signal();
            };
            device->setCallbacks (std::move (callbacks));
            device->start();

            for (const auto& path : paths)
                (void) device->listen (path);

            senderReady.store (false, std::memory_order_release);
            udp.close();
            tcp.reset();
        }

        /*  Once HOST_INFO is in: the UDP sender to OSC_IP (else the HTTP host)
            and OSC_PORT, or the TCP sender when OSC_TRANSPORT says so. */
        void ensureSender()
        {
            if (senderReady.load (std::memory_order_relaxed))
                return;

            std::optional<oscquery::HostInfo> info;
            std::string host;

            withDevice ([&] (const oscquery::Device& d)
            {
                info = d.hostInfo();
                host = d.options().host;
            });

            if (! info.has_value() || info->oscPort <= 0)
                return;

            const auto oscHost = info->oscHost (host);
            std::string error;

            if (info->oscOverTcp())
            {
                oscquery::net::TcpSender::Options o;
                o.host = oscHost;
                o.port = info->oscPort;
                tcp = std::make_unique<oscquery::net::TcpSender> (o);

                if (! tcp->connect (error))
                {
                    tcp.reset();
                    return;
                }
            }
            else if (! udp.open (oscHost, info->oscPort, error))
            {
                return;
            }

            senderReady.store (true, std::memory_order_release);
        }

        void refreshGroups()
        {
            const auto revision = groupsRevision.load (std::memory_order_acquire);

            if (revision == seenGroupsRevision)
                return;

            seenGroupsRevision = revision;

            const std::lock_guard<std::mutex> guard (groupsLock);
            groups = allGroups;

            std::size_t maxKey = 0;

            for (const auto& g : groups)
                maxKey = std::max (maxKey, g->limiterKey + 1);

            if (limiter.size() < maxKey)
                limiter.resize (maxKey);

            groupByKey.assign (maxKey, nullptr);

            for (const auto& g : groups)
                groupByKey[g->limiterKey] = g.get();
        }

        void refreshSettings()
        {
            const auto revision = settingsRevision.load (std::memory_order_acquire);

            if (revision == seenSettingsRevision)
                return;

            seenSettingsRevision = revision;

            const std::lock_guard<std::mutex> guard (settingsLock);
            local = settings;
            limiter.setOptions ({ local.defaultRateHz, local.resendMs });
        }

        /** Pops everything waiting; true if anything was. */
        bool drain (std::chrono::steady_clock::time_point now)
        {
            SlotChange change;
            bool any = false;

            while (ring.pop (change))
            {
                any = true;

                if (change.kind == SlotChange::endOfBlock)
                {
                    marked.insert (change.instance);
                    lastSeen[change.instance] = now;
                    continue;
                }

                lastSeen[change.instance] = now;

                for (const auto& g : groups)
                    if (g->instanceId == change.instance)
                        for (std::size_t i = 0; i < g->argCount(); ++i)
                            if (g->driven[i] && g->slots[i].hostParamId == change.slot)
                                g->dirty.store (true, std::memory_order_relaxed);
            }

            return any;
        }

        bool everyActiveMarked (std::chrono::steady_clock::time_point now) const
        {
            const auto window = std::chrono::milliseconds (local.activeWindowMs);

            for (const auto& [instance, seen] : lastSeen)
                if (now - seen <= window && marked.count (instance) == 0)
                    return false;

            return true;
        }

        void flush()
        {
            const auto nowNanos = unixNanosNow();

            limiter.flushDue (nowNanos, [this] (std::size_t key, double)
            {
                if (key < groupByKey.size() && groupByKey[key] != nullptr)
                    due.push_back (groupByKey[key]);
            });

            for (const auto& g : groups)
                if (g->dirty.exchange (false, std::memory_order_relaxed))
                    if (limiter.offer (g->limiterKey, 0.0, nowNanos))
                        due.push_back (g.get());

            if (due.empty())
                return;

            ensureSender();

            /*  The loop guard's reference: what goes out now may come back
                over LISTEN within the window. Stamped whether the send below
                succeeds or not; an unsent value echoes nothing. */
            const auto sentAt = steadyNanos();

            for (auto* g : due)
                g->lastSentNanos.store (sentAt, std::memory_order_relaxed);

            const bool timetags = ref.honoursTimetags;
            const auto tag = timetags ? ntpNow (ref.offsetMs) : immediateTimeTag;

            /*  CRITICAL on a device that is not TCP: a websocket frame each
                (spec draft L4); what the websocket cannot take goes on the wire. */
            wire.clear();

            for (auto* g : due)
            {
                if (g->critical && tcp == nullptr)
                {
                    std::vector<Value> args;

                    for (std::size_t i = 0; i < g->argCount(); ++i)
                        args.push_back (g->argument (i));

                    bool sent = false;
                    std::string error;

                    withDevice ([&] (oscquery::Device& d)
                    {
                        sent = d.sendOsc (oscquery::osc::Packet::message (g->path, std::move (args)), error);
                    });

                    if (sent)
                    {
                        websocketSent.fetch_add (1, std::memory_order_relaxed);
                        messagesSent.fetch_add (1, std::memory_order_relaxed);
                        continue;
                    }
                }

                wire.push_back (g);
            }

            due.clear();

            if (wire.empty())
                return;

            if (! senderReady.load (std::memory_order_relaxed))
            {
                unsent.fetch_add (wire.size(), std::memory_order_relaxed);
                return;
            }

            if (tcp != nullptr)
            {
                std::vector<oscquery::osc::Packet> packets;

                for (auto* g : wire)
                {
                    std::vector<Value> args;

                    for (std::size_t i = 0; i < g->argCount(); ++i)
                        args.push_back (g->argument (i));

                    packets.push_back (oscquery::osc::Packet::message (g->path, std::move (args)));
                }

                const auto bundled = oscquery::osc::bundle (packets, tag, datagramBufferBytes);

                for (const auto& datagram : bundled.datagrams)
                {
                    std::string error;

                    if (tcp->send (datagram, error))
                    {
                        bundlesSent.fetch_add (1, std::memory_order_relaxed);
                    }
                    else
                    {
                        tcpFailures.fetch_add (1, std::memory_order_relaxed);
                    }
                }

                messagesSent.fetch_add (packets.size() - bundled.refused.size(), std::memory_order_relaxed);
                return;
            }

            /*  UDP: one bundle per round, split at the MTU by refusal. */
            oscquery::osc::BundleWriter writer (buffer.data(), std::min (local.mtu, buffer.size()));
            writer.begin (tag);
            std::array<Value, 16> args;

            for (auto* g : wire)
            {
                const auto n = std::min (g->argCount(), args.size());

                for (std::size_t i = 0; i < n; ++i)
                    args[i] = g->argument (i);

                if (writer.add (g->path, args.data(), n))
                {
                    messagesSent.fetch_add (1, std::memory_order_relaxed);
                    continue;
                }

                if (! writer.empty())
                {
                    if (udp.send (writer.data(), writer.size()))
                        bundlesSent.fetch_add (1, std::memory_order_relaxed);

                    writer.begin (tag);

                    if (writer.add (g->path, args.data(), n))
                    {
                        messagesSent.fetch_add (1, std::memory_order_relaxed);
                        continue;
                    }
                }

                refused.fetch_add (1, std::memory_order_relaxed);       // too large for any bundle at this MTU
            }

            if (! writer.empty() && udp.send (writer.data(), writer.size()))
                bundlesSent.fetch_add (1, std::memory_order_relaxed);
        }

        void run() override
        {
            using namespace std::chrono;

            while (! threadShouldExit())
            {
                (void) wake.waitFor (milliseconds (100));
                wake.drain();

                if (threadShouldExit())
                    break;

                if (addressChanged.exchange (false, std::memory_order_acq_rel))
                    openDevice();

                if (treeArrived.exchange (false, std::memory_order_acq_rel) && onTreeLoaded)
                {
                    const bool loaded = [this]
                    {
                        bool out = false;
                        withDevice ([&] (const oscquery::Device& d) { out = d.treeLoaded(); });
                        return out;
                    }();

                    if (loaded)
                        onTreeLoaded (*this);
                }

                refreshSettings();
                refreshGroups();
                ensureSender();

                auto now = steady_clock::now();
                marked.clear();

                if (drain (now))
                {
                    /*  Gather: every instance active lately posts its marker
                        within the block, each with a wake; wait for the last
                        of them on the semaphore, never spinning, until the
                        deadline. */
                    const auto deadline = now + microseconds (local.gatherDeadlineUs);

                    while (! everyActiveMarked (now) && now < deadline)
                    {
                        if (! drain (now))
                            (void) wake.waitFor (deadline - now);

                        now = steady_clock::now();
                    }

                    rounds.fetch_add (1, std::memory_order_relaxed);
                }

                flush();

                /*  Forget instances silent for ten windows, so a closed
                    instance is not waited for. */
                const auto forget = milliseconds (local.activeWindowMs) * 10;

                for (auto it = lastSeen.begin(); it != lastSeen.end();)
                    it = (now - it->second > forget) ? lastSeen.erase (it) : std::next (it);
            }
        }

        //==============================================================================
        mutable std::mutex settingsLock;
        DeviceRef ref;
        RegistrySettings settings;
        RegistrySettings local;
        std::atomic<std::uint64_t> settingsRevision { 1 };
        std::uint64_t seenSettingsRevision = 0;

        mutable std::mutex deviceLock;
        std::unique_ptr<oscquery::Device> device;
        std::atomic<bool> addressChanged { false };
        std::atomic<bool> treeArrived { false };
        std::function<void (DeviceEntry&)> onTreeLoaded;

        oscquery::net::RawUdpSender udp;
        std::unique_ptr<oscquery::net::TcpSender> tcp;
        std::atomic<bool> senderReady { false };

        mutable std::mutex groupsLock;
        std::vector<std::shared_ptr<Group>> allGroups;
        std::atomic<std::uint64_t> groupsRevision { 1 };

        //  The network thread's own.
        std::uint64_t seenGroupsRevision = 0;
        std::vector<std::shared_ptr<Group>> groups;
        std::vector<Group*> groupByKey;
        std::vector<Group*> due;
        std::vector<Group*> wire;
        RateLimiter limiter;
        std::unordered_set<std::uint32_t> marked;
        std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> lastSeen;
        std::vector<std::uint8_t> buffer;

        std::atomic<std::uint64_t> bundlesSent { 0 };
        std::atomic<std::uint64_t> messagesSent { 0 };
        std::atomic<std::uint64_t> websocketSent { 0 };
        std::atomic<std::uint64_t> tcpFailures { 0 };
        std::atomic<std::uint64_t> unsent { 0 };
        std::atomic<std::uint64_t> refused { 0 };
        std::atomic<std::uint64_t> rounds { 0 };

        //  Record.
        std::function<void (std::vector<Delivery>&)> onReceived;
        std::atomic<double> guardMs { 50.0 };
        std::atomic<std::uint64_t> valuesReceived { 0 };
        std::atomic<std::uint64_t> loopGuarded { 0 };
        std::atomic<std::uint64_t> unmatchedReceived { 0 };
    };

    //==============================================================================
    struct Registry::Impl
    {
        mutable std::mutex lock;
        std::map<std::string, std::unique_ptr<DeviceEntry>> devices;
        std::map<std::uint32_t, std::unique_ptr<InstanceRecord>> instances;
        std::uint32_t nextInstanceId = 1;
        std::size_t nextLimiterKey = 0;
        RegistrySettings settings;
        std::atomic<bool> directSend { false };

        void applyBindings (InstanceRecord* instance, const StateChunk& chunk);
        void rebuildPlansFor (DeviceEntry* entry);

        DeviceEntry& entryFor (const DeviceRef& ref)
        {
            const auto key = keyOf (ref);
            auto found = devices.find (key);

            if (found != devices.end())
            {
                found->second->setTiming (ref.honoursTimetags, ref.offsetMs);
                return *found->second;
            }

            auto entry = std::make_unique<DeviceEntry> (key, ref, settings,
                                                        [this] (std::vector<Delivery>& deliveries) { deliver (deliveries); },
                                                        [this] (DeviceEntry& e) { rebuildPlansFor (&e); });
            auto& out = *entry;
            devices.emplace (key, std::move (entry));
            return out;
        }

        /*  Received values to their instances' rings, one wake per instance
            touched. On a Device thread (or the UDP receiver's), under the
            registry's lock, so the instance and its wake are alive for the
            call; the wake only signals, by contract. */
        void deliver (std::vector<Delivery>& deliveries)
        {
            const std::lock_guard<std::mutex> guard (lock);
            InstanceRecord* last = nullptr;

            for (const auto& d : deliveries)
            {
                const auto found = instances.find (d.instance);

                if (found == instances.end())
                    continue;

                auto* instance = found->second.get();
                instance->incoming.push ({ d.instance, static_cast<std::uint16_t> (d.slot), SlotChange::value, d.normalised });
                instance->received.fetch_add (1, std::memory_order_relaxed);

                if (instance != last)
                {
                    if (instance->wake != nullptr)
                        instance->wake (instance->wakeContext);

                    last = instance;
                }
            }
        }
    };

    Registry::Registry() : impl (std::make_unique<Impl>()) {}

    Registry::~Registry()
    {
        shutdown();
    }

    void Registry::shutdown()
    {
        /*  Instances first (their plans point at devices), then the devices,
            each stopping its thread and its device. For a process that is
            ending, or a test that wants no thread left when main returns.
            Taken out from under the lock before they are destroyed: a device
            being stopped may be mid-delivery, waiting for that very lock. */
        std::map<std::uint32_t, std::unique_ptr<InstanceRecord>> instances;
        std::map<std::string, std::unique_ptr<DeviceEntry>> devices;

        {
            const std::lock_guard<std::mutex> guard (impl->lock);
            instances.swap (impl->instances);
            devices.swap (impl->devices);
        }

        instances.clear();
        devices.clear();
    }

    Registry& Registry::shared()
    {
        static Registry registry;
        return registry;
    }

    //==============================================================================
    Registry::InstanceRecord* Registry::registerInstance (const std::string& label, int slotCount)
    {
        const std::lock_guard<std::mutex> guard (impl->lock);

        auto record = std::make_unique<InstanceRecord>();
        record->id = impl->nextInstanceId++;
        record->label = label;
        record->slotCount = std::max (0, slotCount);
        record->directBuffer.resize (4096);

        auto empty = std::make_unique<InstancePlan>();
        empty->groupOfSlot.assign (static_cast<std::size_t> (record->slotCount), nullptr);
        empty->argOfSlot.assign (static_cast<std::size_t> (record->slotCount), noSlot);
        record->plan.store (empty.get(), std::memory_order_release);
        record->plans.push_back (std::move (empty));

        auto* out = record.get();
        impl->instances.emplace (out->id, std::move (record));
        return out;
    }

    void Registry::unregisterInstance (InstanceRecord* instance)
    {
        if (instance == nullptr)
            return;

        const std::lock_guard<std::mutex> guard (impl->lock);
        const auto found = impl->instances.find (instance->id);

        if (found == impl->instances.end())
            return;

        for (auto& [key, entry] : impl->devices)
            entry->removeGroups (instance->id);

        impl->instances.erase (found);
    }

    bool Registry::setBindings (InstanceRecord* instance, const StateChunk& chunk, std::string& error)
    {
        if (instance == nullptr)
        {
            error = "no instance";
            return false;
        }

        const std::lock_guard<std::mutex> guard (impl->lock);

        if (impl->instances.count (instance->id) == 0)
        {
            error = "the instance is not registered";
            return false;
        }

        instance->lastChunk = std::make_unique<StateChunk> (chunk);
        impl->applyBindings (instance, chunk);
        return true;
    }

    /*  THE PLAN, built from the chunk under the registry's lock: on the message
        thread from setBindings, and again from a device's network thread when
        its tree lands after the bindings did (a reload), so a group planned
        short of the node's argument count is planned whole. The values the
        audio thread last posted carry over from the plan being replaced, so
        nothing is re-sent for the rebuild alone. */
    void Registry::Impl::applyBindings (InstanceRecord* instance, const StateChunk& chunk)
    {
        std::map<std::pair<DeviceEntry*, std::string>, std::shared_ptr<Group>> previous;

        if (const auto* live = instance->plan.load (std::memory_order_acquire))
            for (const auto& g : live->groups)
                previous[std::make_pair (g->device, g->path)] = g;

        instance->warnings.clear();
        instance->deviceByIndex.clear();

        for (const auto& ref : chunk.devices)
        {
            if (ref.name.empty() || ref.lastHost.empty() || ref.lastPort <= 0)
            {
                instance->deviceByIndex.push_back (nullptr);
                instance->warnings.push_back ("device \"" + ref.name + "\" has no address; its slots stay unresolved");
                continue;
            }

            instance->deviceByIndex.push_back (&entryFor (ref));
        }

        auto plan = std::make_unique<InstancePlan>();
        plan->groupOfSlot.assign (static_cast<std::size_t> (instance->slotCount), nullptr);
        plan->argOfSlot.assign (static_cast<std::size_t> (instance->slotCount), noSlot);

        std::map<std::pair<int, std::string>, std::shared_ptr<Group>> groups;

        for (const auto& slot : chunk.slots.all())
        {
            if (! slot.bound() || slot.hostParamId < 0 || slot.hostParamId >= instance->slotCount)
                continue;

            if (! slot.active())
            {
                instance->warnings.push_back ("slot " + std::to_string (slot.hostParamId) + " (" + slot.path
                                              + ") has no range yet and neither sends nor receives");
                continue;
            }

            if (slot.deviceIndex < 0 || slot.deviceIndex >= static_cast<int> (instance->deviceByIndex.size())
                || instance->deviceByIndex[static_cast<std::size_t> (slot.deviceIndex)] == nullptr)
            {
                instance->warnings.push_back ("slot " + std::to_string (slot.hostParamId) + " (" + slot.path
                                              + ") names a device with no address");
                continue;
            }

            auto* entry = instance->deviceByIndex[static_cast<std::size_t> (slot.deviceIndex)];
            const auto groupKey = std::make_pair (slot.deviceIndex, slot.path);
            auto& group = groups[groupKey];

            if (group == nullptr)
            {
                group = std::make_shared<Group>();
                group->instanceId = instance->id;
                group->device = entry;
                group->path = slot.path;
                group->limiterKey = nextLimiterKey++;
                group->critical = entry->nodeIsCritical (slot.path);
            }

            const auto arg = static_cast<std::size_t> (std::max (0, slot.argIndex));

            if (group->slots.size() <= arg)
            {
                group->slots.resize (arg + 1);
                group->driven.resize (arg + 1, false);
                group->receiver.resize (arg + 1, -1);
            }

            group->slots[arg] = slot;
            group->critical = group->critical || slot.critical;

            if (slot.sends())
            {
                /*  Only a sending slot is posted by the audio thread. */
                group->driven[arg] = true;
                plan->groupOfSlot[static_cast<std::size_t> (slot.hostParamId)] = group.get();
                plan->argOfSlot[static_cast<std::size_t> (slot.hostParamId)] = static_cast<std::uint16_t> (arg);
            }

            if (slot.receives())
                group->receiver[arg] = slot.hostParamId;
        }

        std::map<DeviceEntry*, std::vector<std::shared_ptr<Group>>> perDevice;

        for (auto& [key, group] : groups)
        {
            /*  The message carries every argument of the node when the tree
                says how many; before the tree is in (a reload), the bound ones. */
            group->tags = group->device->nodeTags (group->path);
            const auto bound = group->argCount();
            const auto n = std::max (bound, group->tags.size());

            group->slots.resize (n);
            group->driven.resize (n, false);
            group->receiver.resize (n, -1);

            if (group->tags.size() < n)
            {
                group->tags.resize (n, 'f');

                for (std::size_t i = 0; i < bound; ++i)
                    if (group->driven[i] || group->receiver[i] >= 0)
                        group->tags[i] = group->slots[i].type;
            }

            group->known = std::make_unique<std::atomic<float>[]> (n);
            group->latest = std::make_unique<std::atomic<double>[]> (n);
            group->initial = group->device->initialValues (group->path, n);

            for (std::size_t i = 0; i < n; ++i)
            {
                const auto& v = group->initial[i];
                group->latest[i].store (v.isBool() ? (v.getBool() ? 1.0 : 0.0) : v.asDouble(), std::memory_order_relaxed);

                /*  Start from where the device is, so the first block only
                    sends what the host actually moved; on a rebuild, from
                    what the audio thread last posted. */
                float start = 0.0f;

                if (group->driven[i])
                {
                    if (const auto host = group->slots[i].hostValue (v))
                        start = *host;

                    const auto was = previous.find (std::make_pair (group->device, group->path));

                    if (was != previous.end() && i < was->second->argCount() && was->second->driven[i])
                        start = was->second->known[i].load (std::memory_order_relaxed);
                }

                group->known[i].store (start, std::memory_order_relaxed);
            }

            if (std::find (plan->devices.begin(), plan->devices.end(), group->device) == plan->devices.end())
                plan->devices.push_back (group->device);

            perDevice[group->device].push_back (group);
            plan->groups.push_back (group);
        }

        /*  Every device this instance used before learns its new groups (or none). */
        for (auto& [key, entry] : devices)
        {
            auto found = perDevice.find (entry.get());
            entry->setGroups (instance->id, found == perDevice.end() ? std::vector<std::shared_ptr<Group>> {} : found->second);
        }

        instance->plan.store (plan.get(), std::memory_order_release);
        instance->plans.push_back (std::move (plan));
    }

    void Registry::Impl::rebuildPlansFor (DeviceEntry* entry)
    {
        const std::lock_guard<std::mutex> guard (lock);

        for (auto& [id, instance] : instances)
        {
            if (instance->lastChunk == nullptr)
                continue;

            if (std::find (instance->deviceByIndex.begin(), instance->deviceByIndex.end(), entry) == instance->deviceByIndex.end())
                continue;

            applyBindings (instance.get(), *instance->lastChunk);
        }
    }

    bool Registry::setBindingsJson (InstanceRecord* instance, std::string_view chunkJson, std::string& error)
    {
        if (instance == nullptr)
        {
            error = "no instance";
            return false;
        }

        auto chunk = StateChunk::fromJson (chunkJson, instance->slotCount, error);

        if (! chunk.has_value())
            return false;

        if (! setBindings (instance, *chunk, error))
            return false;

        const std::lock_guard<std::mutex> guard (impl->lock);
        instance->warnings.insert (instance->warnings.end(), chunk->warnings.begin(), chunk->warnings.end());
        return true;
    }

    std::vector<std::string> Registry::warnings (InstanceRecord* instance) const
    {
        if (instance == nullptr)
            return {};

        const std::lock_guard<std::mutex> guard (impl->lock);
        return instance->warnings;
    }

    //==============================================================================
    void Registry::post (InstanceRecord* instance, int slot, float normalised) noexcept
    {
        if (instance == nullptr || slot < 0 || slot >= instance->slotCount)
            return;

        const auto* plan = instance->plan.load (std::memory_order_acquire);
        auto* group = plan->groupOfSlot[static_cast<std::size_t> (slot)];

        if (group == nullptr)
            return;

        const auto arg = plan->argOfSlot[static_cast<std::size_t> (slot)];
        group->known[arg].store (normalised, std::memory_order_relaxed);

        if (impl->directSend.load (std::memory_order_relaxed) && group->device->directSend (*instance, *group))
            return;

        group->device->ring.push ({ instance->id, static_cast<std::uint16_t> (slot), SlotChange::value, normalised });
    }

    void Registry::endBlock (InstanceRecord* instance) noexcept
    {
        if (instance == nullptr)
            return;

        const auto* plan = instance->plan.load (std::memory_order_acquire);

        for (auto* device : plan->devices)
        {
            device->ring.push ({ instance->id, 0, SlotChange::endOfBlock, 0.0f });

            /*  The one syscall of the audio path, by decision (2026-11-08):
                a non-blocking wake, once per block per device. */
            device->wake.signal();
        }
    }

    //==============================================================================
    void Registry::setReceiver (InstanceRecord* instance, void (*wake) (void* context), void* context)
    {
        if (instance == nullptr)
            return;

        const std::lock_guard<std::mutex> guard (impl->lock);

        if (impl->instances.count (instance->id) == 0)
            return;

        instance->wake = wake;
        instance->wakeContext = context;
    }

    int Registry::pollReceived (InstanceRecord* instance, int* slots, float* values, int capacity) noexcept
    {
        if (instance == nullptr || slots == nullptr || values == nullptr || capacity <= 0)
            return 0;

        int n = 0;
        SlotChange change;

        while (n < capacity && instance->incoming.pop (change))
        {
            slots[n] = change.slot;
            values[n] = change.normalised;
            ++n;
        }

        return n;
    }

    //==============================================================================
    bool Registry::testSend (InstanceRecord* instance, int slot, float normalised, std::string& error)
    {
        if (instance == nullptr || slot < 0 || slot >= instance->slotCount)
        {
            error = "no such slot";
            return false;
        }

        const auto* plan = instance->plan.load (std::memory_order_acquire);
        auto* group = plan->groupOfSlot[static_cast<std::size_t> (slot)];

        if (group == nullptr)
        {
            error = "slot " + std::to_string (slot) + " is not bound to a sending, active slot";
            return false;
        }

        group->known[plan->argOfSlot[static_cast<std::size_t> (slot)]].store (normalised, std::memory_order_relaxed);
        group->device->ring.push ({ instance->id, static_cast<std::uint16_t> (slot), SlotChange::value, normalised });
        group->device->ring.push ({ instance->id, 0, SlotChange::endOfBlock, 0.0f });
        group->device->wake.signal();
        return true;
    }

    std::optional<juce::var> Registry::deviceTree (InstanceRecord* instance, int deviceIndex, const std::string& path) const
    {
        if (instance == nullptr)
            return std::nullopt;

        DeviceEntry* entry = nullptr;

        {
            const std::lock_guard<std::mutex> guard (impl->lock);

            if (deviceIndex < 0 || deviceIndex >= static_cast<int> (instance->deviceByIndex.size()))
                return std::nullopt;

            entry = instance->deviceByIndex[static_cast<std::size_t> (deviceIndex)];
        }

        if (entry == nullptr)
            return std::nullopt;

        std::optional<juce::var> out;

        entry->withDevice ([&] (const oscquery::Device& d)
        {
            if (! d.treeLoaded())
                return;

            d.withTree ([&] (const oscquery::Tree& tree) { out = nodeAt (tree.toVar(), path); });
        });

        return out;
    }

    juce::var Registry::status (InstanceRecord* instance) const
    {
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> devices;

        {
            const std::lock_guard<std::mutex> guard (impl->lock);

            if (instance != nullptr)
            {
                for (auto* entry : instance->deviceByIndex)
                    devices.add (entry != nullptr ? entry->status() : juce::var());

                juce::StringArray warnings;

                for (const auto& w : instance->warnings)
                    warnings.add (juce::String (w));

                o->setProperty ("warnings", warnings);
                o->setProperty ("direct_sent", static_cast<juce::int64> (instance->directSent.load()));
                o->setProperty ("received", static_cast<juce::int64> (instance->received.load()));
                o->setProperty ("received_dropped", static_cast<juce::int64> (instance->incoming.dropped()));
                o->setProperty ("plans", static_cast<int> (instance->plans.size()));
            }
            else
            {
                for (const auto& [key, entry] : impl->devices)
                    devices.add (entry->status());

                o->setProperty ("instances", static_cast<int> (impl->instances.size()));
            }

            o->setProperty ("settings", impl->settings.toVar());
        }

        o->setProperty ("devices", devices);
        return juce::var (o);
    }

    bool Registry::setDeviceAddress (const std::string& name, const std::string& identityValue,
                                     const std::string& host, int port)
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        const auto found = impl->devices.find (keyOf (name, identityValue));

        if (found == impl->devices.end() || host.empty() || port <= 0)
            return false;

        found->second->setAddress (host, port);
        return true;
    }

    void Registry::setSettings (const RegistrySettings& settings)
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        impl->settings = settings;
        impl->directSend.store (settings.directSend, std::memory_order_relaxed);

        for (auto& [key, entry] : impl->devices)
            entry->setSettings (settings);
    }

    RegistrySettings Registry::settings() const
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        return impl->settings;
    }

    std::size_t Registry::deviceCount() const
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        return impl->devices.size();
    }

    std::size_t Registry::instanceCount() const
    {
        const std::lock_guard<std::mutex> guard (impl->lock);
        return impl->instances.size();
    }
}
