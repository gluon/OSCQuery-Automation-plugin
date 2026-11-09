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
    ONE PLUGIN INSTANCE'S CORE: its slots and devices (the state chunk), and
    its side of the registry. The format shim owns one and calls it from
    three places; the editor is a client of it.

      the host's lifecycle   connect() in prepareToPlay (again on every call:
                             Logic restarts its hosting process and every
                             instance re-initialises), disconnect() in
                             releaseResources and the destructor
      the audio thread       processBlock (values, count): posts each bound,
                             active, sending slot whose value moved, then ends
                             the block; no allocation, no lock, one wake per
                             device touched
      the editor             bind, unbind, ranges, curves, directions, names;
                             the Test button; the device's tree; the status
      record (Phase 4)       the registry wakes the shim once per message
                             received; the shim polls the values on the
                             message thread and pushes them to the host

    The registry is reached through an OqaCoreApi table: the shared core
    library's when the loader found one, this binary's own otherwise. The
    instance does not know which; the status says.

    Every change of a binding pushes the whole chunk to the registry, which
    rebuilds the instance's plan. The audio thread never reads the chunk: it
    posts normalised values and the registry's plan says where they go.
*/

#include <core/StateChunk.h>
#include <core/registry/RegistryApi.h>
#include <core/rt/RtCheck.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oqa::core
{
    class Instance
    {
    public:
        /** `api` outlives the instance: the loader's table, or staticCoreApi(). */
        Instance (const OqaCoreApi* api, int slotCount, std::string label);
        ~Instance();

        Instance (const Instance&) = delete;
        Instance& operator= (const Instance&) = delete;

        int slotCount() const noexcept { return count; }

        //==============================================================================
        //  The host's lifecycle.

        /** Register with the registry and hand over the bindings. Safe to repeat. */
        void connect();

        /** Leave the registry. Safe to repeat. */
        void disconnect();

        bool isConnected() const noexcept { return handle != nullptr; }

        //==============================================================================
        //  The state. Message thread.

        const StateChunk& chunk() const noexcept { return state; }

        /** Replace everything from a saved chunk; false with the reason when it is not one. */
        bool loadState (std::string_view json, std::string& error);
        std::string saveState() const;

        /*  A device for slots to point at: its index in the chunk, reusing an
            entry with the same name and identity value. */
        int addDevice (const DeviceRef& device);
        void setDeviceTiming (int deviceIndex, bool honoursTimetags, double offsetMs);

        bool bind (int slot, const oscquery::Node& node, std::size_t argument, int deviceIndex, std::string& why);
        bool bindAll (int firstSlot, const oscquery::Node& node, int deviceIndex, std::string& why);
        void unbind (int slot);

        void setRange (int slot, double min, double max);
        void setScaling (int slot, Scaling scaling);
        void setDirection (int slot, Direction direction);
        void setDisplayName (int slot, const std::string& name);

        /** Moves on every change of a binding: the shim renames its parameters when it does. */
        std::uint64_t revision() const noexcept { return bindingRevision.load (std::memory_order_acquire); }

        //==============================================================================
        //  The audio thread.

        /*  `normalised[i]` is slot i's host value. Posts what moved since the
            last block (everything, the first block after a change of
            bindings), then ends the block. A value equal to the last one
            received from the device for that slot is not posted: a slot whose
            direction is both does not echo the device back to itself. */
        void processBlock (const float* normalised, int valueCount) noexcept OQA_AUDIO_THREAD;

        //==============================================================================
        //  Record (Phase 4). Values the devices stream for this instance's
        //  receiving slots wait in the registry; `wake` is called on a registry
        //  thread, at most once per message, and must only signal (the shim
        //  triggers an async update). pollReceived, on the message thread,
        //  drains them oldest first and remembers each slot's last.

        void setReceiveWake (std::function<void()> wake);

        /** Calls `apply (slot, normalised)` for each value waiting, oldest first; returns how many. */
        int pollReceived (const std::function<void (int slot, float normalised)>& apply);

        /** The last value received for a slot, as 0-1; nullopt until one has. Any thread. */
        std::optional<float> lastReceived (int slot) const noexcept;

        /** Whether the registry this instance runs on has the receive entries (an older shared library has not). */
        bool canReceive() const noexcept;

        //==============================================================================
        //  The editor.

        bool testSend (int slot, float normalised, std::string& error);

        /** The device's tree at `path` as it serves it, or nullopt when not loaded. */
        std::optional<std::string> deviceTreeJson (int deviceIndex, const std::string& path = "/") const;

        /** The registry's view of this instance's devices, and its warnings, as JSON. */
        std::string statusJson() const;

        /** The registry's process-wide settings as JSON (direct_send, receive_port, gesture_timeout_ms...). */
        std::string settingsJson() const;

        /** Fields present in `json` replace the registry's; false with the reason when it is not JSON. */
        bool setSettingsJson (std::string_view json, std::string& error);

        /** What the registry could not honour in the last push, in words. */
        const std::vector<std::string>& warnings() const noexcept { return lastWarnings; }

    private:
        void push();
        void registerWake();
        std::string takeError (char* text) const;
        static void wakeTrampoline (void* context);

        const OqaCoreApi* api;
        int count;
        std::string label;
        OqaInstance* handle = nullptr;

        StateChunk state;
        std::vector<std::string> lastWarnings;
        std::atomic<std::uint64_t> bindingRevision { 0 };

        //  The audio thread's own.
        std::vector<float> lastPosted;
        std::atomic<bool> postEverything { true };

        //  Record: written on the message thread, read by the audio thread and the editor.
        std::function<void()> receiveWake;
        std::unique_ptr<std::atomic<float>[]> received;
    };
}
