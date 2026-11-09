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

#pragma once

/*
    THE DEVICE REGISTRY (PRD, Architecture: "a process-wide registry, so 64
    instances share one OSCQuery client and one socket per device"; devplan
    Phase 3). One entry per device the session refers to, keyed by NAME and the
    identity leaf's value, resolved to one host and port, owning one
    oscquery::Device, one sender and one network thread at realtime priority.
    Instances register, hand over their bindings, and from then on their audio
    threads post slot values into the device's ring and wake its thread once
    per block.

    THE SEND PIPELINE. A bound path of one instance is a Group: the message
    the device receives, every argument of the node, the ones being automated
    from their slots and the others from the last value known (the tree's at
    bind time). The audio thread stores the new normalised value into the
    group and pushes {instance, slot, value} into the device's ring, then an
    end-of-block marker per device and one wake. The network thread wakes,
    drains the ring, waits until every instance active in the last second has
    posted its marker or the gather deadline (0.5 ms) passes, then builds one
    bundle per device of every dirty group, splits at the MTU, stamps the
    immediate time tag or NTP now plus the device's offset, and sends: UDP
    through RawUdpSender, TCP through TcpSender when OSC_TRANSPORT says so,
    CRITICAL leaves on a UDP device as a websocket frame (spec L4). The
    per-path rate limiter sits before the bundle. The optional direct send
    (default off) encodes the group's message on the audio thread into the
    instance's own buffer and sends it at once; no bundle, no time tag.

    LIFETIMES. A plan (which slot goes to which group and device) is built
    for an instance on every change of its bindings and published through one
    atomic pointer; the old plans stay alive until the instance unregisters,
    because an audio thread may still be reading one. Groups are shared
    between the plan that made them and the device's list; the network thread
    copies that list under its lock at the start of a round and holds the
    copies for the round. A device entry lives as long as the registry: the
    sockets and the thread of a device the session once referred to are not
    worth the churn of closing on every releaseResources.

    THE RECEIVE PATH (Phase 4, 2026-11-09). Every group's path is LISTENed on
    its device, sending groups included: the stream is also what keeps the
    arguments no slot drives at the device's current value. A value arrives on
    the library's Device thread (never the audio thread): the groups at that
    path are found, the arguments no slot drives are refreshed, a driven
    argument echoing what was just sent is counted and dropped (the loop
    guard), and the rest is scaled to 0-1 by the receiving slot and pushed into
    its instance's incoming ring, with one wake to the instance's shim. Devices
    without LISTEN are served by one UDP receiver per process (4b).

    LOCK ORDER: the registry's lock, then a device entry's groups lock, or
    either alone; never the reverse. received() copies the matching groups
    under the groups lock, releases, and only then reaches the registry.

    This class is the implementation; RegistryApi.h is what a plugin binary
    calls through. The registry inside the shared library is the one every
    binary in the process shares; a binary that found no library uses its
    own, and says so.
*/

#include <core/StateChunk.h>

#include <juce_core/juce_core.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oqa::core
{
    struct RegistrySettings
    {
        bool directSend = false;           ///< encode and sendto on the audio thread; default off
        double defaultRateHz = 0.0;        ///< per-path rate limit; 0 is off
        double resendMs = 0.0;             ///< periodic resend of unchanged values; 0 is off
        int gatherDeadlineUs = 500;        ///< how long a device's thread waits for every instance's marker
        std::size_t mtu = 1400;            ///< bundles split above this
        int activeWindowMs = 1000;         ///< an instance that posted this recently is waited for

        //  Phase 4 (2026-11-09): record.
        int receivePort = 0;               ///< the UDP port for devices without LISTEN; 0 is ephemeral
        double loopGuardMs = 50.0;         ///< a value echoed by the device this soon after a send is ignored
        double gestureTimeoutMs = 200.0;   ///< the shim closes a host gesture after this much silence

        juce::var toVar() const;

        /** Fields present in `v` replace this one's; the rest stay. */
        void applyVar (const juce::var& v);
    };

    //==============================================================================
    class Registry
    {
    public:
        class InstanceRecord;

        Registry();
        ~Registry();

        Registry (const Registry&) = delete;
        Registry& operator= (const Registry&) = delete;

        /** This binary's one registry. */
        static Registry& shared();

        //==============================================================================
        InstanceRecord* registerInstance (const std::string& label, int slotCount);
        void unregisterInstance (InstanceRecord* instance);

        /*  The instance's bindings, from its chunk: devices are resolved (and
            started) and a plan built. False with the reason when the chunk
            names what cannot be bound at all; slots that cannot go are left
            out and the reason appended to `warnings`. Not for the audio thread. */
        bool setBindings (InstanceRecord* instance, const StateChunk& chunk, std::string& error);

        /** The same from the chunk's JSON, read against the instance's slot count. */
        bool setBindingsJson (InstanceRecord* instance, std::string_view chunkJson, std::string& error);

        /** What the last setBindings could not honour, in words. */
        std::vector<std::string> warnings (InstanceRecord* instance) const;

        //==============================================================================
        //  The audio thread.
        void post (InstanceRecord* instance, int slot, float normalised) noexcept;
        void endBlock (InstanceRecord* instance) noexcept;

        //==============================================================================
        //  Record (Phase 4). Values the device streams for an instance's receiving
        //  slots wait in the instance's incoming ring; `wake` is called on a
        //  registry thread, at most once per message delivered, and must only
        //  signal (the shim triggers an async update). pollReceived drains the
        //  ring into the caller's arrays, oldest first: nothing allocated, no lock.
        void setReceiver (InstanceRecord* instance, void (*wake) (void* context), void* context);
        int pollReceived (InstanceRecord* instance, int* slots, float* values, int capacity) noexcept;

        //==============================================================================
        bool testSend (InstanceRecord* instance, int slot, float normalised, std::string& error);

        /** The device's tree at `path`, as served; nullopt when not loaded or no such device. */
        std::optional<juce::var> deviceTree (InstanceRecord* instance, int deviceIndex, const std::string& path) const;

        /** Devices (and the settings) as JSON-able data; for an instance, its devices only. */
        juce::var status (InstanceRecord* instance = nullptr) const;

        bool setDeviceAddress (const std::string& name, const std::string& identityValue,
                               const std::string& host, int port);

        void setSettings (const RegistrySettings& settings);
        RegistrySettings settings() const;

        std::size_t deviceCount() const;
        std::size_t instanceCount() const;

        /** Stop every device's thread and forget every instance: for a process that is ending. */
        void shutdown();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
