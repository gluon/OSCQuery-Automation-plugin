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
    THE DONE-WHEN'S SECOND CLAUSE, IN CI (devplan Phase 6: "a script binds a
    slot over the plugin's OSCQuery server with no editor open and the host
    lane renames"): the module's registry serves; a datagram to the slot's
    path binds it; the plugin asks the host for its main thread, applies the
    command there, and the host hears rescan (INFO) and sees the lane's new
    name, the slot no longer hidden. No gui was created.
*/

#include <doctest/doctest.h>

#include "TestHost.h"
#include "TestSupport.h"
#include "core/RegistryRig.h"
#include "support/HttpGet.h"

#include <core/StateChunk.h>
#include <core/server/ServerNamespace.h>
#include <oscquery/net/UdpEndpoint.h>
#include <oscquery/osc/Codec.h>
#include <shims/clap/StatusExtension.h>

#include <string>

using namespace oqatest::claphost;
namespace ns = oqa::core::server::ns;
using oqatest::Rig;
using oqatest::waitUntil;
using oqatest::isRangedFloat;
using oqa::oscquery::Node;
namespace osc = oqa::oscquery::osc;

namespace
{
    constexpr const char* eightSlots = "io.github.pob31.oqa.Q008";

    juce::var processStatus (PluginHandle& plugin)
    {
        const auto* ext = plugin.extension<oqa_clap_status_t> (OQA_CLAP_EXT_STATUS);
        REQUIRE (ext != nullptr);
        return juce::JSON::parse (juce::String (ext->process_status_json (plugin.get())));
    }
}

//==============================================================================
TEST_CASE ("clap server: a datagram to a slot's path binds it with no gui, and the host hears the rename")
{
    Rig rig;
    const auto& gain = rig.leafWhere ([] (const Node& n) { return isRangedFloat (n) && n.argumentCount() == 1; });

    RecordingHost host;
    PluginHandle plugin (host, eightSlots);
    REQUIRE (plugin);
    REQUIRE (plugin->init (plugin.get()));
    REQUIRE (plugin->activate (plugin.get(), 48000.0, 1, 64));

    /*  A session naming the device, nothing bound. */
    oqa::core::StateChunk chunk (8);
    auto ref = rig.device ("clap-server");
    ref.treeHash = rig.tree.treeHash();
    chunk.devices.push_back (ref);

    const auto* state = plugin.extension<clap_plugin_state_t> (CLAP_EXT_STATE);
    REQUIRE (state != nullptr);
    StringIStream in (chunk.toJson());
    REQUIRE (state->load (plugin.get(), &in.stream));

    /*  The module's registry serves (the harness's settings file says so). */
    const auto status = processStatus (plugin);
    const auto server = status.getProperty ("server", {});
    const auto port = static_cast<int> (server.getProperty ("port", 0));
    const auto oscPort = static_cast<int> (server.getProperty ("osc_port", 0));
    REQUIRE (port > 0);
    REQUIRE (oscPort > 0);

    juce::int64 id = -1;

    if (const auto* instances = status.getProperty ("instances", {}).getArray())
        for (const auto& i : *instances)
            if (i.getProperty ("label", "").toString().contains ("(CLAP)") && static_cast<int> (i.getProperty ("slot_count", 0)) == 8)
                id = std::max (id, static_cast<juce::int64> (i.getProperty ("id", -1)));

    REQUIRE (id >= 0);
    const auto slotPath = ns::slotPath (static_cast<unsigned long> (id), 3) + "/" + ns::path;
    REQUIRE (waitUntil ([&] { return oqatest::get (port, slotPath).status == 200; }));

    /*  Before: hidden, (unbound). */
    const auto* params = plugin.extension<clap_plugin_params_t> (CLAP_EXT_PARAMS);
    REQUIRE (params != nullptr);
    clap_param_info_t info {};
    REQUIRE (params->get_info (plugin.get(), 3, &info));
    CHECK ((info.flags & CLAP_PARAM_IS_HIDDEN) != 0);
    host.forget();
    const auto callbacksBefore = host.callbackRequests.load();

    /*  The script's datagram. */
    osc::Packet p;
    p.address = slotPath;
    p.args = { osc::Value::string (gain.fullPath) };
    std::string error;
    const auto bytes = osc::encode (p, error);
    REQUIRE_MESSAGE (bytes.has_value(), error);
    oqa::oscquery::net::UdpEndpoint sender;
    REQUIRE (sender.send ("127.0.0.1", oscPort, *bytes));

    /*  The plugin asks for the main thread; the host gives it; the lane renames. */
    REQUIRE (waitUntil ([&] { return host.callbackRequests.load() > callbacksBefore; }));
    plugin->on_main_thread (plugin.get());

    CHECK ((host.rescanFlags() & CLAP_PARAM_RESCAN_INFO) != 0);
    REQUIRE (params->get_info (plugin.get(), 3, &info));
    CHECK ((info.flags & CLAP_PARAM_IS_HIDDEN) == 0);
    CHECK (std::string (info.name) == gain.fullPath.substr (1));
    CHECK (std::string (info.module) == "clap-server" + gain.fullPath.substr (0, gain.fullPath.rfind ('/')));

    /*  And the served tree says so. */
    REQUIRE (waitUntil ([&]
    {
        const auto reply = oqatest::get (port, slotPath);
        return reply.status == 200 && juce::JSON::parse (juce::String (reply.body)).getProperty ("VALUE", {})[0].toString().toStdString() == gain.fullPath;
    }));

    {
        RecordingHost::AudioThreadScope audio (host);
        REQUIRE (plugin->start_processing (plugin.get()));
        plugin->stop_processing (plugin.get());
    }
    plugin->deactivate (plugin.get());
}
