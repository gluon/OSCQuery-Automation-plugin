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
    THE DEVICE LIST (PRD, UX, Device browser: "Device list from DNS-SD plus
    manual entries and recents; each with name, host, identity value when set,
    reachability dot"; Discovery: "Devices that fail the probe show as
    unreachable, not hidden").

    THREE SOURCES, ONE LIST. This session's devices first, with the registry's
    state (reachable, tree loaded, unreachable); then what the Finder has:
    DNS-SD's and the typed entries, each probed with HOST_INFO before it is
    listed with its NAME; then the per-user recents. A row that is already a
    session device is shown once. Selecting a row that is not yet in the
    session adds it (that is how a device joins the chunk), so the tree pane
    can show it: the registry fetches the tree.

    MANUAL ENTRY is a host:port field and a button, because multicast often
    does not cross a show network's VLANs (PRD), and because on this very
    machine nothing announced on the same host is found (Phase 2 bench).
*/

#include <core/Instance.h>

#include <oscquery/Finder.h>
#include <oscquery/Recents.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace oqa::editor
{
    class DeviceList final : public juce::Component,
                             private juce::ListBoxModel
    {
    public:
        explicit DeviceList (core::Instance& instance);
        ~DeviceList() override;

        /** Called with the chunk's device index when a device is chosen. */
        std::function<void (int deviceIndex)> onDeviceSelected;

        /** Re-read the three sources and repaint; called by the editor's timer. */
        void refresh (const juce::var& registryStatus);

        void resized() override;

        /** The session device index selected, or -1. */
        int selectedDevice() const noexcept { return selected; }

    private:
        struct Row
        {
            enum class Source { session, found, manual, recent };
            Source source = Source::session;
            juce::String name;
            juce::String host;
            int port = 0;
            juce::String state;        ///< words for the row's right-hand side
            bool reachable = false;
            int deviceIndex = -1;      ///< the chunk's, when source == session
        };

        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void addManual();

        core::Instance& instance;
        oscquery::Finder finder;
        oscquery::Recents recents;

        std::vector<Row> rows;
        int selected = -1;

        juce::Label title { {}, "Devices" };
        juce::ListBox list { "devices", this };
        juce::TextEditor manualEntry;
        juce::TextButton addButton { "Add" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceList)
    };
}
