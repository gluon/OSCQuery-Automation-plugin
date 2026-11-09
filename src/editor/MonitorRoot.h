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
    THE MONITOR PLUGIN'S EDITOR (devplan Phase 6c): the MonitorView over a
    core::Instance, refreshed four times a second from the registry's
    process-wide status. The instance is the Monitor's own (zero slots: it
    owns nothing, and its being registered keeps the registry, the server
    and the Finder alive while only a Monitor is loaded), or any slot
    plugin's when it opens the same view (6d-2). A plain Component, as
    EditorRoot is, with the same size hints for the shims.
*/

#include "MonitorView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace oqa::core { class Instance; }

namespace oqa::editor
{
    struct MonitorHost
    {
        core::Instance& instance;
        juce::String coreNote;      ///< the loader's word: shared where, or alone why
    };

    class MonitorRoot final : public juce::Component,
                              private juce::Timer
    {
    public:
        explicit MonitorRoot (MonitorHost host);
        ~MonitorRoot() override;

        static constexpr int defaultWidth = 1100, defaultHeight = 600;
        static constexpr int minimumWidth = 760,  minimumHeight = 400;
        static constexpr int maximumWidth = 2400, maximumHeight = 1600;

        void resized() override;

        /** Read the status now rather than at the next tick (tests). */
        void refreshNow();

        const MonitorView& view() const noexcept { return monitor; }

    private:
        void timerCallback() override;

        MonitorHost host;
        MonitorView monitor;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonitorRoot)
    };
}
