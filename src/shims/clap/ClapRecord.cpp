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

/*
    RECORD (Phase 5b-3): what the devices streamed for this instance's
    receiving slots, drained on the audio thread into the host's out-events,
    as ClapPlugin.h describes. Nothing here allocates or locks: the drain is
    a lock-free ring pop in the registry, the tracker is pre-sized, the
    callbacks are function pointers.
*/

#include "ClapPlugin.h"

#include <juce_core/juce_core.h>

#include <chrono>

namespace oqa::clapshim
{
    namespace
    {
        /*  The one clock read per block: steady, monotonic, no blocking (an
            exception to the lipogram recorded as the wake notify is). */
        double nowMs() noexcept
        {
            using namespace std::chrono;
            return duration<double, std::milli> (steady_clock::now().time_since_epoch()).count();
        }

        struct Drain
        {
            ClapPlugin* plugin;
            const clap_output_events_t* out;
            double now;
        };

        void pushGesture (const clap_output_events_t* out, clap_id id, std::uint16_t type) noexcept
        {
            if (out == nullptr)
                return;

            clap_event_param_gesture_t e {};
            e.header.size = sizeof (e);
            e.header.time = 0;
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = type;
            e.header.flags = CLAP_EVENT_IS_LIVE;
            e.param_id = id;
            out->try_push (out, &e.header);
        }

        void pushValue (const clap_output_events_t* out, clap_id id, double value) noexcept
        {
            if (out == nullptr)
                return;

            clap_event_param_value_t e {};
            e.header.size = sizeof (e);
            e.header.time = 0;
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_PARAM_VALUE;
            e.header.flags = CLAP_EVENT_IS_LIVE;
            e.param_id = id;
            e.cookie = nullptr;
            e.note_id = -1;
            e.port_index = -1;
            e.channel = -1;
            e.key = -1;
            e.value = value;
            out->try_push (out, &e.header);
        }
    }

    void ClapPlugin::drainReceived (const clap_output_events_t* out) noexcept
    {
        Drain drain { this, out, nowMs() };

        /*  ENDs owed from a deactivate that closed gestures with no flush at
            hand. */
        for (int i = 0; i < count; ++i)
            if (endPending[static_cast<std::size_t> (i)].exchange (false, std::memory_order_relaxed))
                pushGesture (out, static_cast<clap_id> (i), CLAP_EVENT_PARAM_GESTURE_END);

        /*  Each value the registry holds for one of our receiving slots: the
            gesture first, when this is its first value, then the value, as a
            hand on the fader would. The value is ours too, so get_value and
            the next block agree with the device, and the core's echo guard
            sends nothing back. */
        core.pollReceived ([] (int slot, float normalised, void* context)
        {
            auto& d = *static_cast<Drain*> (context);
            d.plugin->values[static_cast<std::size_t> (slot)].store (normalised, std::memory_order_relaxed);

            if (d.plugin->gestures.received (slot, d.now))
                pushGesture (d.out, static_cast<clap_id> (slot), CLAP_EVENT_PARAM_GESTURE_BEGIN);

            pushValue (d.out, static_cast<clap_id> (slot), static_cast<double> (normalised));
        }, &drain);

        /*  A gesture a device left idle past the timeout closes. */
        gestures.expire (drain.now, gestureTimeoutMs.load (std::memory_order_relaxed), [] (int slot, void* context)
        {
            auto& d = *static_cast<Drain*> (context);
            pushGesture (d.out, static_cast<clap_id> (slot), CLAP_EVENT_PARAM_GESTURE_END);
        }, &drain);

        openGestures.store (gestures.openCount(), std::memory_order_relaxed);
    }

    void ClapPlugin::refreshSettings() noexcept
    {
        const auto settings = juce::JSON::parse (juce::String (core.settingsJson()));

        if (settings.hasProperty ("gesture_timeout_ms"))
            gestureTimeoutMs.store (juce::jlimit (10.0, 60000.0, static_cast<double> (settings["gesture_timeout_ms"])), std::memory_order_relaxed);
    }
}
