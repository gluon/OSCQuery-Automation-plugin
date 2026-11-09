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
    RECORD OVER CLAP, 5b-3, against the mock device: a value the device
    streams for a receiving slot becomes PARAM_GESTURE_BEGIN then PARAM_VALUE
    in the next process, PARAM_GESTURE_END once the gesture has been idle
    past the timeout; the same through flush while the plugin is inactive,
    the registry's wake having asked the host for it; the timer asks again
    while a gesture is open; what was received is not sent back to the
    device; a burst over eight slots arrives whole and in order.
*/

#include <doctest/doctest.h>

#include "TestHost.h"
#include "TestSupport.h"
#include "core/RegistryRig.h"

#include <core/StateChunk.h>
#include <shims/clap/StatusExtension.h>

#include <chrono>
#include <string>
#include <thread>

using namespace oqatest::claphost;
using oqatest::Rig;
using oqatest::waitUntil;
using oqatest::bundlesReceived;
using oqatest::messagesIn;
using oqatest::isRangedFloat;
using oqa::oscquery::Node;

namespace
{
    constexpr const char* eightSlots = "io.github.pob31.oqa.Q008";

    juce::var statusOf (PluginHandle& plugin)
    {
        const auto* ext = plugin.extension<oqa_clap_status_t> (OQA_CLAP_EXT_STATUS);
        REQUIRE (ext != nullptr);
        return juce::JSON::parse (juce::String (ext->status_json (plugin.get())));
    }

    /** Tick until the registry's websocket to the mock streams, and the LISTEN's first values are in. */
    void waitListening (PluginHandle& plugin, int paths)
    {
        REQUIRE (waitUntil ([&]
        {
            plugin.host.runTimers (plugin.get());
            const auto device = statusOf (plugin).getProperty ("devices", {})[0];
            return device.getProperty ("listen", "").toString() == "listening"
                && static_cast<int> (device.getProperty ("values_received", 0)) >= paths;
        }));
        std::this_thread::sleep_for (std::chrono::milliseconds (30));
    }

    struct Ready
    {
        RecordingHost host;
        PluginHandle plugin;
        ProcessRig block;

        explicit Ready (const char* id = eightSlots) : plugin (host, id)
        {
            REQUIRE (plugin);
            REQUIRE (plugin->init (plugin.get()));
            REQUIRE (plugin->activate (plugin.get(), 48000.0, 1, 64));
            RecordingHost::AudioThreadScope audio (host);
            REQUIRE (plugin->start_processing (plugin.get()));
        }

        ~Ready()
        {
            if (active)
            {
                if (! stopped)
                {
                    RecordingHost::AudioThreadScope audio (host);
                    plugin->stop_processing (plugin.get());
                }
                plugin->deactivate (plugin.get());
            }
        }

        void load (const std::string& json)
        {
            const auto* state = plugin.extension<clap_plugin_state_t> (CLAP_EXT_STATE);
            REQUIRE (state != nullptr);
            StringIStream in (json);
            REQUIRE (state->load (plugin.get(), &in.stream));
        }

        /** Run blocks until `out` holds an event of `type` for `slot`, or time runs out. */
        bool runUntilEvent (std::uint16_t type, clap_id slot, int timeoutMs = 5000)
        {
            return waitUntil ([&]
            {
                block.run (plugin);
                return hasEvent (type, slot);
            }, timeoutMs);
        }

        bool hasEvent (std::uint16_t type, clap_id slot) const
        {
            for (std::size_t i = 0; i < block.out.events.size(); ++i)
                if (block.out.header (i)->type == type && block.out.as<clap_event_param_gesture_t> (i)->param_id == slot)
                    return true;

            return false;
        }

        int countEvents (std::uint16_t type) const
        {
            int n = 0;
            for (std::size_t i = 0; i < block.out.events.size(); ++i)
                if (block.out.header (i)->type == type) ++n;
            return n;
        }

        void drainFirstValues (int slots)
        {
            /*  On LISTEN each path's current value is delivered once, inside
                a gesture; let them in and let the gestures close. */
            REQUIRE (waitUntil ([&] { block.run (plugin); return countEvents (CLAP_EVENT_PARAM_VALUE) >= slots; }));
            REQUIRE (waitUntil ([&] { block.run (plugin); return countEvents (CLAP_EVENT_PARAM_GESTURE_END) >= slots; }));
            block.out.clear();
        }

        bool active = true;
        bool stopped = false;
    };

    std::string receivingChunk (Rig& rig, const char* name, const Node& leaf, int slot, oqa::core::Direction direction)
    {
        oqa::core::StateChunk chunk (8);
        auto ref = rig.device (name);
        ref.treeHash = rig.tree.treeHash();
        chunk.devices.push_back (ref);
        std::string why;
        REQUIRE (chunk.slots[slot].bind (leaf, 0, 0, why));
        chunk.slots[slot].direction = direction;
        return chunk.toJson();
    }
}

//==============================================================================
TEST_CASE ("clap record: a value the device streams becomes GESTURE_BEGIN then VALUE in process, scaled by the slot; END after the idle timeout; nothing goes back to the device")
{
    Rig rig;
    const auto& gain = rig.leafWhere ([] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1 && ! n.unit.empty() && ! n.unit[0].empty(); });

    Ready r;
    r.load (receivingChunk (rig, "record-process", gain, 3, oqa::core::Direction::both));
    waitListening (r.plugin, 1);
    r.drainFirstValues (1);
    rig.mock.clearReceivedOsc();

    const auto& arg = gain.argument (0);
    const auto units = *arg.range->min + 0.25 * (*arg.range->max - *arg.range->min);
    REQUIRE (rig.mock.setValue (juce::String (gain.fullPath), { units }));

    REQUIRE (r.runUntilEvent (CLAP_EVENT_PARAM_VALUE, 3));

    /*  BEGIN before VALUE, both for slot 3, the value in 0-1, live. */
    REQUIRE (r.block.out.events.size() >= 2);
    CHECK (r.block.out.header (0)->type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
    CHECK (r.block.out.as<clap_event_param_gesture_t> (0)->param_id == 3);
    CHECK (r.block.out.header (1)->type == CLAP_EVENT_PARAM_VALUE);
    CHECK ((r.block.out.header (1)->flags & CLAP_EVENT_IS_LIVE) != 0);
    const auto* value = r.block.out.as<clap_event_param_value_t> (1);
    CHECK (value->param_id == 3);
    CHECK (value->value == doctest::Approx (0.25).epsilon (0.001));

    const auto* params = r.plugin.extension<clap_plugin_params_t> (CLAP_EXT_PARAMS);
    double current = 0;
    REQUIRE (params->get_value (r.plugin.get(), 3, &current));
    CHECK (current == doctest::Approx (0.25).epsilon (0.001));

    /*  The gesture closes once the device has been quiet past the timeout
        (200 ms by default), in a later block. */
    REQUIRE (r.runUntilEvent (CLAP_EVENT_PARAM_GESTURE_END, 3));
    CHECK (r.countEvents (CLAP_EVENT_PARAM_GESTURE_BEGIN) == 1);
    CHECK (r.countEvents (CLAP_EVENT_PARAM_GESTURE_END) == 1);

    /*  What came from the device was not sent back to it. */
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    CHECK (messagesIn (bundlesReceived (rig.mock)) == 0);
}

TEST_CASE ("clap record: while the host is not processing (active, asleep) the wake asks it to flush, and the flush carries the events; the timer keeps asking until the gesture has closed")
{
    /*  The case for the flush path is a host that stopped processing an
        active plugin (the transport asleep). A deactivated instance stays
        registered and inactive since Phase 6; the END it owes is the next
        case's. */
    Rig rig;
    const auto& gain = rig.leafWhere ([] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1; });

    Ready r;
    r.load (receivingChunk (rig, "record-flush", gain, 0, oqa::core::Direction::receive));
    waitListening (r.plugin, 1);
    r.drainFirstValues (1);

    {
        RecordingHost::AudioThreadScope audio (r.host);
        r.plugin->stop_processing (r.plugin.get());
    }
    r.stopped = true;

    const auto requestsBefore = r.host.flushRequests.load();
    const auto& arg = gain.argument (0);
    REQUIRE (rig.mock.setValue (juce::String (gain.fullPath), { *arg.range->max }));
    REQUIRE (waitUntil ([&] { return r.host.flushRequests.load() > requestsBefore; }));

    /*  Active, so flush is the audio thread's (clap/ext/params.h). */
    const auto* params = r.plugin.extension<clap_plugin_params_t> (CLAP_EXT_PARAMS);
    InEvents in;
    OutEvents out;
    {
        RecordingHost::AudioThreadScope audio (r.host);
        params->flush (r.plugin.get(), &in.list, &out.list);
    }

    REQUIRE (out.events.size() >= 2);
    CHECK (out.header (0)->type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
    CHECK (out.header (1)->type == CLAP_EVENT_PARAM_VALUE);
    CHECK (out.as<clap_event_param_value_t> (1)->value == doctest::Approx (1.0));

    /*  A gesture is open and the host is not processing: the timer asks
        for a flush each tick until one, after the timeout, closes it. */
    const auto requestsAfterFlush = r.host.flushRequests.load();
    r.host.runTimers (r.plugin.get());
    CHECK (r.host.flushRequests.load() > requestsAfterFlush);

    std::this_thread::sleep_for (std::chrono::milliseconds (250));
    out.clear();
    {
        RecordingHost::AudioThreadScope audio (r.host);
        params->flush (r.plugin.get(), &in.list, &out.list);
    }
    REQUIRE (out.events.size() == 1);
    CHECK (out.header (0)->type == CLAP_EVENT_PARAM_GESTURE_END);

    const auto requestsClosed = r.host.flushRequests.load();
    r.host.runTimers (r.plugin.get());
    CHECK (r.host.flushRequests.load() == requestsClosed);
}

TEST_CASE ("clap record: a gesture open at deactivate is ended at the next flush")
{
    Rig rig;
    const auto& gain = rig.leafWhere ([] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1; });

    Ready r;
    r.load (receivingChunk (rig, "record-deactivate", gain, 5, oqa::core::Direction::receive));
    waitListening (r.plugin, 1);
    r.drainFirstValues (1);

    const auto& arg = gain.argument (0);
    REQUIRE (rig.mock.setValue (juce::String (gain.fullPath), { *arg.range->min }));
    REQUIRE (r.runUntilEvent (CLAP_EVENT_PARAM_GESTURE_BEGIN, 5));
    CHECK (! r.hasEvent (CLAP_EVENT_PARAM_GESTURE_END, 5));

    {
        RecordingHost::AudioThreadScope audio (r.host);
        r.plugin->stop_processing (r.plugin.get());
    }
    r.stopped = true;
    const auto requestsBefore = r.host.flushRequests.load();
    r.plugin->deactivate (r.plugin.get());
    r.active = false;
    CHECK (r.host.flushRequests.load() > requestsBefore);

    const auto* params = r.plugin.extension<clap_plugin_params_t> (CLAP_EXT_PARAMS);
    InEvents in;
    OutEvents out;
    params->flush (r.plugin.get(), &in.list, &out.list);
    REQUIRE (out.events.size() >= 1);
    CHECK (out.header (0)->type == CLAP_EVENT_PARAM_GESTURE_END);
    CHECK (out.as<clap_event_param_gesture_t> (0)->param_id == 5);
}

TEST_CASE ("clap record: a burst over eight slots arrives whole, each slot's values in order")
{
    Rig rig;
    std::vector<const Node*> leaves;

    for (const auto* leaf : rig.tree.leaves())
        if (isRangedFloat (*leaf) && leaf->argumentCount() == 1 && leaves.size() < 8)
            leaves.push_back (leaf);

    REQUIRE (leaves.size() >= 2);

    oqa::core::StateChunk chunk (8);
    auto ref = rig.device ("record-burst");
    ref.treeHash = rig.tree.treeHash();
    chunk.devices.push_back (ref);

    for (std::size_t i = 0; i < leaves.size(); ++i)
    {
        std::string why;
        REQUIRE (chunk.slots[static_cast<int> (i)].bind (*leaves[i], 0, 0, why));
        chunk.slots[static_cast<int> (i)].direction = oqa::core::Direction::receive;
    }

    Ready r;
    r.load (chunk.toJson());
    waitListening (r.plugin, static_cast<int> (leaves.size()));

    r.drainFirstValues (static_cast<int> (leaves.size()));

    constexpr int steps = 20;

    for (int step = 1; step <= steps; ++step)
        for (std::size_t i = 0; i < leaves.size(); ++i)
        {
            const auto& arg = leaves[i]->argument (0);
            REQUIRE (rig.mock.setValue (juce::String (leaves[i]->fullPath), { *arg.range->min + (step / double (steps)) * (*arg.range->max - *arg.range->min) }));
        }

    const auto expected = static_cast<int> (leaves.size()) * steps;
    REQUIRE (waitUntil ([&] { r.block.run (r.plugin); return r.countEvents (CLAP_EVENT_PARAM_VALUE) >= expected; }));
    CHECK (r.countEvents (CLAP_EVENT_PARAM_VALUE) == expected);

    std::vector<double> last (leaves.size(), -1.0);

    for (std::size_t i = 0; i < r.block.out.events.size(); ++i)
    {
        if (r.block.out.header (i)->type != CLAP_EVENT_PARAM_VALUE)
            continue;

        const auto* e = r.block.out.as<clap_event_param_value_t> (i);
        INFO ("slot " << e->param_id);
        CHECK (e->value > last[e->param_id]);
        last[e->param_id] = e->value;
    }

    for (const auto v : last)
        CHECK (v == doctest::Approx (1.0).epsilon (0.001));
}
