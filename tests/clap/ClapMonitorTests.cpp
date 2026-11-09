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
    THE MONITOR AS A CLAP (devplan Phase 6c): the sixth plugin is created,
    initialised, activated, processed with audio through, deactivated and
    destroyed like the five; it has no params extension; it is registered with
    the registry as an instance of zero slots, so the process-wide status
    (and the Monitor view) lists it, while the plugin's own server leaves it
    out of its tree; its state is a chunk of no slots that loads back.
*/

#include <doctest/doctest.h>

#include "TestHost.h"
#include "TestSupport.h"

#include <shims/clap/StatusExtension.h>

#include <juce_core/juce_core.h>

#include <string>

using namespace oqatest::claphost;

namespace
{
    constexpr const char* monitorId = "io.github.pob31.oqa.QMon";
    constexpr const char* eightSlots = "io.github.pob31.oqa.Q008";

    juce::var processStatus (PluginHandle& plugin)
    {
        const auto* ext = plugin.extension<oqa_clap_status_t> (OQA_CLAP_EXT_STATUS);
        REQUIRE (ext != nullptr);
        return juce::JSON::parse (juce::String (ext->process_status_json (plugin.get())));
    }

    int instancesOfSlotCount (const juce::var& status, int slotCount)
    {
        int n = 0;

        if (const auto* instances = status.getProperty ("instances", {}).getArray())
            for (const auto& i : *instances)
                if (static_cast<int> (i.getProperty ("slot_count", -1)) == slotCount)
                    ++n;

        return n;
    }
}

//==============================================================================
TEST_CASE ("clap monitor: zero parameters, audio through, registered as an instance of zero slots beside a slot plugin's")
{
    RecordingHost host;
    PluginHandle monitor (host, monitorId);
    REQUIRE (monitor);
    REQUIRE (monitor->init (monitor.get()));

    /*  No parameters: no params extension, rather than one that counts zero. */
    CHECK (monitor.extension<clap_plugin_params_t> (CLAP_EXT_PARAMS) == nullptr);

    const auto* gui = monitor.extension<clap_plugin_gui_t> (CLAP_EXT_GUI);
    CHECK (gui != nullptr);

    REQUIRE (monitor->activate (monitor.get(), 48000.0, 1, 256));

    ProcessRig rig (32);
    for (uint32_t i = 0; i < 32; ++i) { rig.inputData[0][i] = 0.5f; rig.inputData[1][i] = static_cast<float> (i); }

    {
        RecordingHost::AudioThreadScope audio (host);
        REQUIRE (monitor->start_processing (monitor.get()));
    }

    CHECK (rig.run (monitor) == CLAP_PROCESS_CONTINUE);
    CHECK (rig.outputData[0][5] == doctest::Approx (0.5f));
    CHECK (rig.outputData[1][31] == doctest::Approx (31.0f));

    {
        RecordingHost::AudioThreadScope audio (host);
        monitor->stop_processing (monitor.get());
    }

    /*  A slot plugin beside it: the Monitor's process-wide status lists both. */
    RecordingHost otherHost;
    PluginHandle eight (otherHost, eightSlots);
    REQUIRE (eight);
    REQUIRE (eight->init (eight.get()));

    const auto status = processStatus (monitor);
    REQUIRE (status.isObject());
    CHECK (instancesOfSlotCount (status, 0) >= 1);
    CHECK (instancesOfSlotCount (status, 8) >= 1);
    CHECK (static_cast<int> (status.getProperty ("instance_count", 0)) >= 2);

    /*  State: a chunk with no slots, saved and loaded back. */
    const auto* state = monitor.extension<clap_plugin_state_t> (CLAP_EXT_STATE);
    REQUIRE (state != nullptr);
    StringOStream out;
    REQUIRE (state->save (monitor.get(), &out.stream));
    const auto chunk = juce::JSON::parse (juce::String (out.data));
    REQUIRE (chunk.isObject());
    CHECK (chunk.getProperty ("slots", juce::var()).size() == 0);
    StringIStream in (out.data);
    REQUIRE (state->load (monitor.get(), &in.stream));

    monitor->deactivate (monitor.get());
}
