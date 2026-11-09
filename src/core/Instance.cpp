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

#include <core/Instance.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace oqa::core
{
    namespace
    {
        constexpr float notReceived = std::numeric_limits<float>::quiet_NaN();

        /** Whether the table reaches as far as `member`: an older library's stops short. */
        template <typename Member>
        bool tableHas (const OqaCoreApi* api, Member OqaCoreApi::* member)
        {
            if (api == nullptr)
                return false;

            const auto offset = reinterpret_cast<const char*> (&(api->*member)) - reinterpret_cast<const char*> (api);
            return api->size >= static_cast<std::size_t> (offset) + sizeof (Member) && (api->*member) != nullptr;
        }
    }

    Instance::Instance (const OqaCoreApi* apiIn, int slotCountIn, std::string labelIn)
        : api (apiIn), count (std::max (0, slotCountIn)), label (std::move (labelIn)), state (count)
    {
        lastPosted.assign (static_cast<std::size_t> (count), 0.0f);
        received = std::make_unique<std::atomic<float>[]> (static_cast<std::size_t> (count));

        for (int i = 0; i < count; ++i)
            received[static_cast<std::size_t> (i)].store (notReceived, std::memory_order_relaxed);
    }

    Instance::~Instance()
    {
        disconnect();
    }

    //==============================================================================
    void Instance::connect()
    {
        if (api == nullptr)
            return;

        if (handle == nullptr)
        {
            handle = api->instanceRegister (label.c_str(), count);
            registerWake();
        }

        push();
    }

    void Instance::disconnect()
    {
        if (handle != nullptr && api != nullptr)
            api->instanceUnregister (handle);

        handle = nullptr;
    }

    //==============================================================================
    bool Instance::canReceive() const noexcept
    {
        return tableHas (api, &OqaCoreApi::instanceSetReceiver) && tableHas (api, &OqaCoreApi::instancePollReceived);
    }

    void Instance::wakeTrampoline (void* context)
    {
        /*  A registry thread, under the registry's lock: the instance is
            registered, so alive. The wake only signals, by contract. */
        auto* self = static_cast<Instance*> (context);

        if (self->receiveWake)
            self->receiveWake();
    }

    void Instance::registerWake()
    {
        if (handle != nullptr && canReceive())
            api->instanceSetReceiver (handle, receiveWake ? &Instance::wakeTrampoline : nullptr, this);
    }

    void Instance::setReceiveWake (std::function<void()> wake)
    {
        receiveWake = std::move (wake);
        registerWake();
    }

    int Instance::pollReceived (const std::function<void (int, float)>& apply)
    {
        if (handle == nullptr || ! canReceive())
            return 0;

        int slots[64];
        float values[64];
        int total = 0;

        for (;;)
        {
            const int n = api->instancePollReceived (handle, slots, values, 64);

            for (int i = 0; i < n; ++i)
            {
                if (slots[i] >= 0 && slots[i] < count)
                {
                    received[static_cast<std::size_t> (slots[i])].store (values[i], std::memory_order_relaxed);

                    if (apply)
                        apply (slots[i], values[i]);
                }
            }

            total += n;

            if (n < 64)
                return total;
        }
    }

    std::optional<float> Instance::lastReceived (int slot) const noexcept
    {
        if (slot < 0 || slot >= count)
            return std::nullopt;

        const auto value = received[static_cast<std::size_t> (slot)].load (std::memory_order_relaxed);
        return std::isnan (value) ? std::nullopt : std::optional<float> (value);
    }

    //==============================================================================
    std::string Instance::takeError (char* text) const
    {
        if (text == nullptr)
            return {};

        std::string out (text);
        api->freeString (text);
        return out;
    }

    void Instance::push()
    {
        lastWarnings.clear();
        lastWarnings.insert (lastWarnings.end(), state.warnings.begin(), state.warnings.end());

        if (handle != nullptr)
        {
            char* error = nullptr;

            if (api->instanceSetBindings (handle, state.toJson().c_str(), &error) != 0)
                lastWarnings.push_back (takeError (error));

            /*  The registry's own words about the push. */
            if (char* status = api->instanceStatusJson (handle))
            {
                const auto var = juce::JSON::parse (juce::String (status));
                api->freeString (status);

                if (const auto* warnings = var.getProperty ("warnings", {}).getArray())
                    for (const auto& w : *warnings)
                        lastWarnings.push_back (w.toString().toStdString());
            }
        }

        postEverything.store (true, std::memory_order_release);
        bindingRevision.fetch_add (1, std::memory_order_acq_rel);
    }

    //==============================================================================
    bool Instance::loadState (std::string_view json, std::string& error)
    {
        auto loaded = StateChunk::fromJson (json, count, error);

        if (! loaded.has_value())
            return false;

        state = std::move (*loaded);
        push();
        return true;
    }

    std::string Instance::saveState() const
    {
        return state.toJson();
    }

    int Instance::addDevice (const DeviceRef& device)
    {
        for (std::size_t i = 0; i < state.devices.size(); ++i)
            if (state.devices[i].name == device.name && state.devices[i].identityValue == device.identityValue)
            {
                state.devices[i].lastHost = device.lastHost;
                state.devices[i].lastPort = device.lastPort;

                if (! device.treeHash.empty())
                    state.devices[i].treeHash = device.treeHash;

                push();
                return static_cast<int> (i);
            }

        state.devices.push_back (device);
        push();
        return static_cast<int> (state.devices.size()) - 1;
    }

    void Instance::setDeviceTiming (int deviceIndex, bool honoursTimetags, double offsetMs)
    {
        if (deviceIndex < 0 || deviceIndex >= static_cast<int> (state.devices.size()))
            return;

        state.devices[static_cast<std::size_t> (deviceIndex)].honoursTimetags = honoursTimetags;
        state.devices[static_cast<std::size_t> (deviceIndex)].offsetMs = std::isfinite (offsetMs) ? offsetMs : 0.0;
        push();
    }

    bool Instance::bind (int slot, const oscquery::Node& node, std::size_t argument, int deviceIndex, std::string& why)
    {
        if (slot < 0 || slot >= count)
        {
            why = "no slot " + std::to_string (slot);
            return false;
        }

        if (deviceIndex < 0 || deviceIndex >= static_cast<int> (state.devices.size()))
        {
            why = "no device " + std::to_string (deviceIndex) + " in this instance";
            return false;
        }

        if (! state.slots[slot].bind (node, argument, deviceIndex, why))
            return false;

        push();
        return true;
    }

    bool Instance::bindAll (int firstSlot, const oscquery::Node& node, int deviceIndex, std::string& why)
    {
        if (deviceIndex < 0 || deviceIndex >= static_cast<int> (state.devices.size()))
        {
            why = "no device " + std::to_string (deviceIndex) + " in this instance";
            return false;
        }

        if (! state.slots.bindAll (firstSlot, node, deviceIndex, why))
            return false;

        push();
        return true;
    }

    void Instance::unbind (int slot)
    {
        if (slot < 0 || slot >= count)
            return;

        state.slots[slot].unbind();
        state.slotExtras.erase (slot);
        push();
    }

    void Instance::setRange (int slot, double min, double max)
    {
        if (slot < 0 || slot >= count)
            return;

        state.slots[slot].setRange (min, max);
        push();
    }

    void Instance::setScaling (int slot, Scaling scaling)
    {
        if (slot < 0 || slot >= count)
            return;

        state.slots[slot].scaling = scaling;
        push();
    }

    void Instance::setDirection (int slot, Direction direction)
    {
        if (slot < 0 || slot >= count)
            return;

        state.slots[slot].direction = direction;
        push();
    }

    void Instance::setDisplayName (int slot, const std::string& name)
    {
        if (slot < 0 || slot >= count)
            return;

        state.slots[slot].displayName = name;
        push();
    }

    //==============================================================================
    void Instance::processBlock (const float* normalised, int valueCount) noexcept
    {
        if (handle == nullptr || normalised == nullptr)
            return;

        const auto n = std::min (valueCount, count);
        const bool everything = postEverything.exchange (false, std::memory_order_acq_rel);

        for (int i = 0; i < n; ++i)
        {
            const float value = normalised[i];
            auto& last = lastPosted[static_cast<std::size_t> (i)];

            if (everything || value != last)
            {
                last = value;

                /*  The host handing back what the device just sent (a slot
                    whose direction is both) is not a move to send. NaN, for a
                    slot that never received, compares false. */
                const auto fromDevice = received[static_cast<std::size_t> (i)].load (std::memory_order_relaxed);

                if (! everything && std::abs (value - fromDevice) <= 1.0e-6f)
                    continue;

                api->instancePost (handle, i, value);
            }
        }

        api->instanceEndBlock (handle);
    }

    //==============================================================================
    bool Instance::testSend (int slot, float normalised, std::string& error)
    {
        if (handle == nullptr)
        {
            error = "not connected to the registry";
            return false;
        }

        char* text = nullptr;

        if (api->instanceTestSend (handle, slot, normalised, &text) == 0)
            return true;

        error = takeError (text);
        return false;
    }

    std::optional<std::string> Instance::deviceTreeJson (int deviceIndex, const std::string& path) const
    {
        if (handle == nullptr)
            return std::nullopt;

        char* text = api->instanceDeviceTreeJson (handle, deviceIndex, path.c_str());

        if (text == nullptr)
            return std::nullopt;

        return takeError (text);
    }

    std::string Instance::statusJson() const
    {
        if (handle == nullptr)
            return "{}";

        return takeError (api->instanceStatusJson (handle));
    }
}
