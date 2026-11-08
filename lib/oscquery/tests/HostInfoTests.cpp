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
    HOST_INFO as the Core profile reads it (spec draft C1): the three required
    fields, the fallbacks for OSC_IP and WS_PORT, EXTENSIONS never inferred.

    The shapes below are what the bench serves: the mock device's HOST_INFO,
    and WFS-DIY's as it is today (Phase 2 must cope with it as it is), which
    lists LISTEN and not the PATH_CHANGED it sends.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"
#include "MockDevice.h"

#include <oscquery/HostInfo.h>

#include <string>

using namespace oqa::oscquery;

namespace
{
    juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
    {
        auto* o = new juce::DynamicObject();

        for (const auto& p : properties)
            o->setProperty (p.first, p.second);

        return juce::var (o);
    }
}

//==============================================================================
TEST_CASE ("host info: the mock device's HOST_INFO reads back whole")
{
    oqatest::MockDevice device;
    REQUIRE (device.loadTree (juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"))));
    REQUIRE (device.start());

    std::string error;
    const auto info = parseHostInfo (device.hostInfo(), error);
    INFO (error);
    REQUIRE (info.has_value());

    CHECK (info->name == device.options().name.toStdString());
    CHECK (info->oscPort == device.oscPort());
    CHECK (info->oscTransport == "UDP");
    CHECK_FALSE (info->oscOverTcp());
    CHECK (info->meetsC1());
    CHECK (info->missing.empty());

    //  No OSC_IP: OSC goes where HTTP came from.
    CHECK_FALSE (info->oscIp.has_value());
    CHECK (info->oscHost ("192.168.1.20") == "192.168.1.20");

    REQUIRE (info->wsPort.has_value());
    CHECK (*info->wsPort == device.httpPort());
    CHECK (info->websocketPort (device.httpPort()) == device.httpPort());
    CHECK (info->websocketHost ("192.168.1.20") == "192.168.1.20");

    CHECK (info->extensionsListed);
    CHECK (info->has ("LISTEN"));
    CHECK (info->has ("PATH_RENAMED"));
    CHECK_FALSE (info->has ("SOMETHING_ELSE"));
    CHECK (info->offersWebsocket());

    //  And the same from the bytes a GET would return.
    const auto fromText = parseHostInfo (juce::JSON::toString (device.hostInfo()).toStdString(), error);
    REQUIRE (fromText.has_value());
    CHECK (fromText->name == info->name);
    CHECK (fromText->extensions == info->extensions);
}

TEST_CASE ("host info: WFS-DIY's shape, which lists LISTEN and not the PATH_CHANGED it sends")
{
    //  Source/Network/OSCQueryServer.cpp buildHostInfoJson, read 2026-11-08.
    const auto wfs = object ({ { "NAME", "WFS-DIY" },
                               { "OSC_PORT", 9001 },
                               { "OSC_TRANSPORT", "UDP" },
                               { "WS_PORT", 5005 },
                               { "EXTENSIONS", object ({ { "ACCESS", true }, { "VALUE", true }, { "RANGE", true },
                                                         { "DESCRIPTION", true }, { "TYPE", true },
                                                         { "CLIPMODE", true }, { "LISTEN", true } }) } });

    std::string error;
    const auto info = parseHostInfo (wfs, error);
    REQUIRE (info.has_value());

    CHECK (info->name == "WFS-DIY");
    CHECK (info->oscPort == 9001);
    CHECK (info->meetsC1());
    CHECK (info->has ("LISTEN"));
    CHECK_FALSE (info->has ("PATH_CHANGED"));       // never inferred: the device did not list it
    CHECK (info->offersWebsocket());
    CHECK (info->oscHost ("10.0.0.5") == "10.0.0.5");
    CHECK (info->websocketPort (5005) == 5005);
}

TEST_CASE ("host info: OSC_IP and WS_IP are honoured when given, and EXTENSIONS may be a list")
{
    std::string error;

    //  As text, which is what a device sends. OSC_PORT as a string is wrong and unambiguous.
    const auto info = parseHostInfo (std::string_view (R"({
        "NAME": "Renderer", "OSC_PORT": "8000", "OSC_TRANSPORT": "tcp",
        "OSC_IP": "10.1.1.2", "WS_IP": "10.1.1.3", "WS_PORT": 9999,
        "EXTENSIONS": ["LISTEN", "RANGE"] })"), error);
    INFO (error);
    REQUIRE (info.has_value());
    CHECK (info->oscPort == 8000);
    CHECK (info->oscTransport == "TCP");
    CHECK (info->oscOverTcp());
    CHECK (info->oscHost ("1.2.3.4") == "10.1.1.2");
    CHECK (info->websocketHost ("1.2.3.4") == "10.1.1.3");
    CHECK (info->websocketPort (80) == 9999);
    CHECK (info->extensionsListed);
    CHECK (info->has ("LISTEN"));
    CHECK (info->has ("RANGE"));
    CHECK (info->meetsC1());
}

TEST_CASE ("host info: a device missing the Core fields is still read, and says what it lacks")
{
    std::string error;

    const auto bare = parseHostInfo (object ({}), error);
    REQUIRE (bare.has_value());
    CHECK_FALSE (bare->meetsC1());
    CHECK (bare->missing == std::vector<std::string> { "NAME", "OSC_PORT", "OSC_TRANSPORT" });
    CHECK (bare->name.empty());
    CHECK (bare->oscPort == 0);
    CHECK (bare->oscTransport == "UDP");                 // the default, so a sender still has one
    CHECK_FALSE (bare->extensionsListed);
    CHECK_FALSE (bare->offersWebsocket());
    CHECK_FALSE (bare->has ("LISTEN"));
    CHECK (bare->websocketPort (5005) == 5005);

    const auto nameOnly = parseHostInfo (object ({ { "NAME", "Thing" } }), error);
    REQUIRE (nameOnly.has_value());
    CHECK (nameOnly->missing == std::vector<std::string> { "OSC_PORT", "OSC_TRANSPORT" });

    //  Not an object at all is the one refusal.
    CHECK_FALSE (parseHostInfo (juce::var (3), error).has_value());
    CHECK_FALSE (error.empty());

    error.clear();
    CHECK_FALSE (parseHostInfo (std::string_view ("[]"), error).has_value());
    CHECK_FALSE (error.empty());

    error.clear();
    CHECK_FALSE (parseHostInfo (std::string_view ("{ not json"), error).has_value());
    CHECK (error.find ("JSON") != std::string::npos);
}
