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
    THE STATUS BAR (PRD, UX: "Per device: packets sent/received per second,
    websocket state, last error"; devplan 3c: the plugin "says so in the status
    bar" when it runs on its own core). Two lines: the core's word (shared
    library at a path, or running alone and why), then each device's state,
    bundles per second since the last refresh, drops, and last error; the
    core's warnings after that.
*/

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <vector>

namespace oqa::editor
{
    class StatusBar final : public juce::Component
    {
    public:
        StatusBar();

        void setCoreNote (const juce::String& note);
        void refresh (const juce::var& registryStatus, const std::vector<std::string>& warnings);

        void paint (juce::Graphics& g) override;

    private:
        juce::String coreNote;
        juce::String devicesLine;
        juce::String warningsLine;

        struct Seen { juce::int64 bundles = 0; double atMs = 0.0; };
        std::map<juce::String, Seen> lastSeen;
    };
}
