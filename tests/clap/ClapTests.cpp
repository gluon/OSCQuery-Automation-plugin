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
    THE CLAP SHIM, 5b-1: the module loads, its factory lists the five (six with the Monitor, 6c)
    variants with the ids decided on 2026-11-09, each is created and taken
    through its lifecycle with audio passing through, and the state chunk
    goes in and out through CLAP's streams byte for byte, the parameters'
    values with it.
*/

#include <doctest/doctest.h>

#include "TestHost.h"
#include "TestSupport.h"

#include <core/StateChunk.h>

#include <string>

using namespace oqatest::claphost;

namespace
{
    std::string stateFixture (const char* name)
    {
        return juce::File (juce::String (OQA_TEST_STATE_DIR)).getChildFile (name).loadFileAsString().toStdString();
    }

    const char* idOf (int slots)
    {
        switch (slots)
        {
            case 0:   return "io.github.pob31.oqa.QMon";
            case 8:   return "io.github.pob31.oqa.Q008";
            case 16:  return "io.github.pob31.oqa.Q016";
            case 32:  return "io.github.pob31.oqa.Q032";
            case 64:  return "io.github.pob31.oqa.Q064";
            default:  return "io.github.pob31.oqa.Q128";
        }
    }
}

//==============================================================================
TEST_CASE ("clap entry: the module loads, is CLAP 1.x, and its factory lists the five variants and the Monitor with the agreed ids")
{
    const auto& module = LoadedModule::get();
    REQUIRE_MESSAGE (module.entry != nullptr, module.error);
    REQUIRE_MESSAGE (module.factory != nullptr, module.error);
    MESSAGE ("module: " << module.path);

    CHECK (module.entry->clap_version.major == 1);
    REQUIRE (module.factory->get_plugin_count (module.factory) == 6);

    const int slots[] = { 8, 16, 32, 64, 128 };

    for (uint32_t i = 0; i < 5; ++i)
    {
        const auto* d = module.factory->get_plugin_descriptor (module.factory, i);
        REQUIRE (d != nullptr);
        INFO ("descriptor " << i << ": " << d->id);
        CHECK (std::string (d->id) == idOf (slots[i]));
        CHECK (std::string (d->name) == "OSCQuery Automation " + std::to_string (slots[i]));
        CHECK (std::string (d->vendor) == "Pix et Bel");
        CHECK (std::string (d->version).empty() == false);
        REQUIRE (d->features != nullptr);
        CHECK (std::string (d->features[0]) == CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
    }

    /*  The Monitor (6c): sixth, zero parameters, an analyzer to the host. */
    {
        const auto* d = module.factory->get_plugin_descriptor (module.factory, 5);
        REQUIRE (d != nullptr);
        CHECK (std::string (d->id) == idOf (0));
        CHECK (std::string (d->name) == "OSCQuery Automation Monitor");
        CHECK (std::string (d->vendor) == "Pix et Bel");
        REQUIRE (d->features != nullptr);
        CHECK (std::string (d->features[0]) == CLAP_PLUGIN_FEATURE_ANALYZER);
    }

    CHECK (module.factory->get_plugin_descriptor (module.factory, 6) == nullptr);
    CHECK (module.entry->get_factory ("not.a.factory") == nullptr);

    RecordingHost host;
    CHECK (module.factory->create_plugin (module.factory, host.clapHost(), "io.github.pob31.oqa.Q999") == nullptr);
}

TEST_CASE ("clap lifecycle: each variant is created, initialised, activated, processed with audio through, deactivated and destroyed")
{
    for (const int slots : { 8, 16, 32, 64, 128 })
    {
        INFO ("variant of " << slots << " slots");
        RecordingHost host;
        PluginHandle plugin (host, idOf (slots));
        REQUIRE (plugin);
        CHECK (std::string (plugin->desc->id) == idOf (slots));

        REQUIRE (plugin->init (plugin.get()));

        /*  The 50 ms tick the shim asks the host for. */
        REQUIRE (host.timers.size() == 1);
        CHECK (host.timers[0].periodMs == 50);

        const auto* ports = plugin.extension<clap_plugin_audio_ports_t> (CLAP_EXT_AUDIO_PORTS);
        REQUIRE (ports != nullptr);
        CHECK (ports->count (plugin.get(), true) == 1);
        CHECK (ports->count (plugin.get(), false) == 1);
        clap_audio_port_info_t info {};
        REQUIRE (ports->get (plugin.get(), 0, true, &info));
        CHECK (info.channel_count == 2);
        CHECK (std::string (info.port_type) == CLAP_PORT_STEREO);
        CHECK (info.in_place_pair == 0);

        REQUIRE (plugin->activate (plugin.get(), 48000.0, 1, 512));

        ProcessRig rig (64);
        for (uint32_t i = 0; i < 64; ++i) { rig.inputData[0][i] = 0.25f * static_cast<float> (i); rig.inputData[1][i] = -0.5f; }

        {
            RecordingHost::AudioThreadScope audio (host);
            REQUIRE (plugin->start_processing (plugin.get()));
        }

        CHECK (rig.run (plugin) == CLAP_PROCESS_CONTINUE);
        CHECK (rig.outputData[0][63] == doctest::Approx (0.25f * 63));
        CHECK (rig.outputData[1][7] == doctest::Approx (-0.5f));

        {
            RecordingHost::AudioThreadScope audio (host);
            plugin->stop_processing (plugin.get());
        }

        plugin->deactivate (plugin.get());
        host.runTimers (plugin.get());
    }
}

TEST_CASE ("clap state: a chunk goes in through clap_istream and comes back through clap_ostream byte for byte, in small pieces, values included")
{
    RecordingHost host;
    PluginHandle plugin (host, idOf (8));
    REQUIRE (plugin);
    REQUIRE (plugin->init (plugin.get()));

    const auto* state = plugin.extension<clap_plugin_state_t> (CLAP_EXT_STATE);
    REQUIRE (state != nullptr);

    SUBCASE ("a chunk without values comes back with every slot's value, all zero, and nothing else changed")
    {
        const auto text = stateFixture ("v1-receive.json");
        StringIStream in (text, 7);
        REQUIRE (state->load (plugin.get(), &in.stream));
        CHECK (in.calls > 10);

        StringOStream out (5);
        REQUIRE (state->save (plugin.get(), &out.stream));
        CHECK (out.calls > 10);

        std::string error;
        auto expected = oqa::core::StateChunk::fromJson (text, 8, error);
        REQUIRE_MESSAGE (expected.has_value(), error);
        expected->values.assign (8, 0.0f);
        CHECK (out.data == expected->toJson());
    }

    SUBCASE ("a chunk with values restores them, and saves them back as they were")
    {
        const auto text = stateFixture ("v1-values.json");
        StringIStream in (text);
        REQUIRE (state->load (plugin.get(), &in.stream));

        StringOStream out;
        REQUIRE (state->save (plugin.get(), &out.stream));

        std::string error;
        const auto expected = oqa::core::StateChunk::fromJson (text, 8, error);
        REQUIRE_MESSAGE (expected.has_value(), error);
        CHECK (out.data == expected->toJson());

        const auto var = juce::JSON::parse (juce::String (out.data));
        const auto* values = var.getProperty ("values", {}).getArray();
        REQUIRE (values != nullptr);
        REQUIRE (values->size() == 8);
        CHECK (static_cast<double> ((*values)[1]) == doctest::Approx (0.75));
        CHECK (static_cast<double> ((*values)[7]) == doctest::Approx (0.125));
    }

    SUBCASE ("what is not a chunk is refused")
    {
        StringIStream in ("{ \"hello\": 1 }");
        CHECK_FALSE (state->load (plugin.get(), &in.stream));

        StringIStream garbage ("not json at all");
        CHECK_FALSE (state->load (plugin.get(), &garbage.stream));
    }
}
