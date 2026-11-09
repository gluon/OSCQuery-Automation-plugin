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
    THE RECENTS STORE (devplan Phase 2), against a file of the test's own in a
    temporary folder, never the user's. Run under C and fr_FR like the rest.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"

#include <oscquery/Recents.h>

#include <string>

using namespace oqa::oscquery;

namespace
{
    struct TempFile
    {
        juce::File file { juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("oqa-recents-" + juce::String (juce::Random::getSystemRandom().nextInt64()))
                            .getChildFile ("recents.settings") };

        ~TempFile() { file.getParentDirectory().deleteRecursively(); }

        Recents::Options options (int capacity = 32) const
        {
            Recents::Options o;
            o.file = file;
            o.capacity = capacity;
            return o;
        }
    };

    Recents::Entry device (const std::string& name, const std::string& host, int port)
    {
        Recents::Entry e;
        e.name = name;
        e.host = host;
        e.port = port;
        return e;
    }
}

//==============================================================================
TEST_CASE ("recents: touched devices come back newest first from the file, identity leaf included")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    TempFile temp;

    {
        Recents recents (temp.options());
        CHECK (recents.file() == temp.file);
        REQUIRE (recents.load());                  // no file yet: an empty list
        CHECK (recents.size() == 0);

        REQUIRE (recents.touch (device ("Renderer A", "192.168.1.20", 5005)));
        CHECK (temp.file.existsAsFile());

        auto b = device ("Renderer B", "192.168.1.21", 5005);
        b.identityPath = "/system/serial";
        b.identityValue = "SN-0002";
        REQUIRE (recents.touch (b));

        REQUIRE (recents.size() == 2);
        CHECK (recents.all()[0].name == "Renderer B");
        CHECK (recents.all()[1].name == "Renderer A");
        CHECK (recents.all()[0].lastSeenMs > 0);
        CHECK (recents.all()[1].lastSeenMs > 0);
    }

    //  A fresh object reads what the first one wrote.
    Recents again (temp.options());
    REQUIRE (again.load());
    REQUIRE (again.size() == 2);

    const auto b = again.find ("Renderer B");
    REQUIRE (b.has_value());
    CHECK (b->host == "192.168.1.21");
    CHECK (b->port == 5005);
    CHECK (b->identityPath == std::optional<std::string> ("/system/serial"));
    CHECK (b->identityValue == std::optional<std::string> ("SN-0002"));
    CHECK (b->lastSeenMs > 0);

    const auto a = again.find ("Renderer A");
    REQUIRE (a.has_value());
    CHECK_FALSE (a->identityPath.has_value());
    CHECK_FALSE (a->identityValue.has_value());

    CHECK_FALSE (again.find ("Nobody").has_value());
}

TEST_CASE ("recents: a device's timing, set once per rig, comes back from the file with its decimal")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    TempFile temp;

    {
        Recents recents (temp.options());
        REQUIRE (recents.load());
        auto e = device ("Renderer T", "192.168.1.30", 5005);
        e.honoursTimetags = true;
        e.offsetMs = -12.5;
        REQUIRE (recents.touch (e));
    }

    Recents again (temp.options());
    REQUIRE (again.load());
    const auto t = again.find ("Renderer T");
    REQUIRE (t.has_value());
    CHECK (t->honoursTimetags);
    CHECK (t->offsetMs == doctest::Approx (-12.5));
    CHECK (temp.file.loadFileAsString().contains ("-12.5"));
}

TEST_CASE ("recents: touching a known device moves it first with its new address; same NAME, other identity, is another device")
{
    TempFile temp;
    Recents recents (temp.options());

    REQUIRE (recents.touch (device ("Renderer", "10.0.0.1", 5005)));
    REQUIRE (recents.touch (device ("Desk", "10.0.0.2", 8000)));
    REQUIRE (recents.touch (device ("Renderer", "172.22.1.50", 5005)));    // moved to the venue

    REQUIRE (recents.size() == 2);
    CHECK (recents.all()[0].name == "Renderer");
    CHECK (recents.all()[0].host == "172.22.1.50");
    CHECK (recents.all()[1].name == "Desk");

    //  Two units of one model, told apart by the identity leaf.
    auto unit1 = device ("Amp", "10.0.0.3", 5005);
    unit1.identityValue = "0001";
    auto unit2 = device ("Amp", "10.0.0.4", 5005);
    unit2.identityValue = "0002";
    REQUIRE (recents.touch (unit1));
    REQUIRE (recents.touch (unit2));
    CHECK (recents.size() == 4);

    CHECK (recents.find ("Amp", std::string ("0001"))->host == "10.0.0.3");
    CHECK (recents.find ("Amp", std::string ("0002"))->host == "10.0.0.4");
    CHECK (recents.find ("Amp")->host == "10.0.0.4");                      // the newest of that name

    //  Removing by name alone takes both; by identity, one.
    CHECK (recents.remove ("Amp", std::string ("0001")));
    CHECK (recents.size() == 3);
    CHECK_FALSE (recents.find ("Amp", std::string ("0001")).has_value());
    CHECK (recents.remove ("Amp"));
    CHECK (recents.size() == 2);
    CHECK_FALSE (recents.remove ("Amp"));
    CHECK_FALSE (recents.remove ("Nobody"));

    //  And the file agrees.
    Recents again (temp.options());
    REQUIRE (again.load());
    CHECK (again.size() == 2);
}

TEST_CASE ("recents: the list is capped at its capacity, oldest first out")
{
    TempFile temp;
    Recents recents (temp.options (3));

    for (int i = 0; i < 5; ++i)
        REQUIRE (recents.touch (device ("Device " + std::to_string (i), "10.0.0." + std::to_string (i), 5005)));

    REQUIRE (recents.size() == 3);
    CHECK (recents.all()[0].name == "Device 4");
    CHECK (recents.all()[2].name == "Device 2");
    CHECK_FALSE (recents.find ("Device 0").has_value());
}

TEST_CASE ("recents: the JSON shape round-trips, and rubbish is refused without harm")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    TempFile temp;
    Recents recents (temp.options());

    auto e = device ("Renderer", "10.0.0.1", 5005);
    e.identityPath = "/id";
    e.identityValue = "x";
    e.lastSeenMs = 1762600000000LL;
    REQUIRE (recents.touch (e));

    const auto json = juce::JSON::toString (recents.toVar(), true).toStdString();
    CHECK (json.find ("\"name\": \"Renderer\"") != std::string::npos);
    CHECK (json.find ("5005") != std::string::npos);
    CHECK (json.find ("1762600000000") != std::string::npos);
    CHECK (json.find (',') != json.rfind (','));              // separators, never a decimal comma: no decimals at all

    Recents other (temp.options());
    REQUIRE (other.fromVar (juce::JSON::parse (juce::String (json))));
    REQUIRE (other.size() == 1);
    CHECK (other.all()[0].lastSeenMs == 1762600000000LL);
    CHECK (other.all()[0].identityValue == std::optional<std::string> ("x"));

    CHECK_FALSE (other.fromVar (juce::var (42)));
    CHECK_FALSE (other.fromVar (juce::JSON::parse ("{\"not\": \"a list\"}")));
    CHECK (other.size() == 1);

    //  Entries without a name are skipped, not kept as blanks.
    REQUIRE (other.fromVar (juce::JSON::parse ("[{\"host\": \"x\"}, {\"name\": \"Kept\", \"host\": \"y\", \"port\": 1}]")));
    REQUIRE (other.size() == 1);
    CHECK (other.all()[0].name == "Kept");

    //  The default location is somewhere per user, and named for the company.
    const auto standard = Recents::defaultFile();
    CHECK (standard.getFullPathName().contains ("Pix et Bel"));
    CHECK (standard.getFileName() == "OSCQuery Automation Plugin.settings");
}
