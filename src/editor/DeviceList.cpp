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

#include "DeviceList.h"

namespace oqa::editor
{
    DeviceList::DeviceList (core::Instance& instanceIn)
        : instance (instanceIn)
    {
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        addAndMakeVisible (title);

        list.setRowHeight (26);
        addAndMakeVisible (list);

        manualEntry.setTextToShowWhenEmpty ("host:port", juce::Colours::grey);
        manualEntry.onReturnKey = [this] { addManual(); };
        addAndMakeVisible (manualEntry);

        addButton.onClick = [this] { addManual(); };
        addAndMakeVisible (addButton);

        recents.load();

        oscquery::Finder::Options options;
        options.browse = true;
        finder.start (options);

        for (const auto& entry : recents.all())
            finder.addManual (entry.host, entry.port);
    }

    DeviceList::~DeviceList()
    {
        finder.stop();
    }

    void DeviceList::resized()
    {
        auto area = getLocalBounds();
        title.setBounds (area.removeFromTop (24));

        auto entry = area.removeFromBottom (28).reduced (0, 2);
        addButton.setBounds (entry.removeFromRight (50));
        entry.removeFromRight (4);
        manualEntry.setBounds (entry);

        list.setBounds (area.withTrimmedBottom (4));
    }

    void DeviceList::addManual()
    {
        const auto text = manualEntry.getText().trim();
        const auto colon = text.lastIndexOfChar (':');

        if (colon <= 0)
            return;

        const auto host = text.substring (0, colon).trim();
        const auto port = text.substring (colon + 1).getIntValue();

        if (host.isEmpty() || port <= 0 || port > 65535)
            return;

        finder.addManual (host.toStdString(), port);
        finder.refresh();
        manualEntry.clear();
    }

    //==============================================================================
    void DeviceList::refresh (const juce::var& registryStatus)
    {
        rows.clear();

        /*  This session's devices, with the registry's word. */
        const auto& chunk = instance.chunk();
        const auto* devices = registryStatus.getProperty ("devices", {}).getArray();

        for (std::size_t i = 0; i < chunk.devices.size(); ++i)
        {
            const auto& ref = chunk.devices[i];
            Row row;
            row.source = Row::Source::session;
            row.name = juce::String (ref.name);
            row.host = juce::String (ref.lastHost);
            row.port = ref.lastPort;
            row.deviceIndex = static_cast<int> (i);

            if (devices != nullptr && static_cast<int> (i) < devices->size())
            {
                const auto& d = (*devices)[static_cast<int> (i)];
                row.reachable = static_cast<bool> (d.getProperty ("reachable", false));
                const auto state = d.getProperty ("state", "").toString();
                const bool treeLoaded = static_cast<bool> (d.getProperty ("tree_loaded", false));
                row.state = state == "unreachable" ? "unreachable"
                          : (treeLoaded ? "ready" : (row.reachable ? "reading tree" : state));
            }
            else
            {
                row.state = "unresolved";
            }

            rows.push_back (row);
        }

        const auto alreadyListed = [this] (const juce::String& host, int port, const juce::String& name)
        {
            for (const auto& r : rows)
                if ((r.host == host && r.port == port) || (name.isNotEmpty() && r.name == name && r.source == Row::Source::session))
                    return true;

            return false;
        };

        for (const auto& candidate : finder.candidates())
        {
            const auto host = juce::String (candidate.host);
            const auto name = candidate.hostInfo.has_value() ? juce::String (candidate.hostInfo->name) : juce::String();

            if (alreadyListed (host, candidate.port, name))
                continue;

            Row row;
            row.source = candidate.manual ? Row::Source::manual : Row::Source::found;
            row.name = name.isNotEmpty() ? name : host + ":" + juce::String (candidate.port);
            row.host = host;
            row.port = candidate.port;
            row.reachable = candidate.reachable;
            row.state = candidate.reachable ? (candidate.manual ? "typed" : "found")
                      : (candidate.probed ? "unreachable" : "probing");
            rows.push_back (row);
        }

        for (const auto& entry : recents.all())
        {
            const auto host = juce::String (entry.host);

            if (alreadyListed (host, entry.port, juce::String (entry.name)))
                continue;

            Row row;
            row.source = Row::Source::recent;
            row.name = juce::String (entry.name);
            row.host = host;
            row.port = entry.port;
            row.reachable = false;
            row.state = "recent";
            rows.push_back (row);
        }

        list.updateContent();
        list.repaint();
    }

    int DeviceList::getNumRows()
    {
        return static_cast<int> (rows.size());
    }

    void DeviceList::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int> (rows.size()))
            return;

        const auto& row = rows[static_cast<std::size_t> (rowNumber)];
        const auto& colours = getLookAndFeel();

        if (rowIsSelected)
            g.fillAll (colours.findColour (juce::ListBox::backgroundColourId).brighter (0.2f));

        const auto dot = row.source == Row::Source::session
                           ? (row.reachable ? juce::Colours::limegreen : juce::Colours::orangered)
                           : (row.reachable ? juce::Colours::cornflowerblue : juce::Colours::grey);

        g.setColour (dot);
        g.fillEllipse (6.0f, height / 2.0f - 4.0f, 8.0f, 8.0f);

        g.setColour (colours.findColour (juce::ListBox::textColourId).withAlpha (row.reachable || row.source == Row::Source::session ? 1.0f : 0.6f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (row.name, 20, 0, width / 2 - 20, height, juce::Justification::centredLeft, true);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (row.host + ":" + juce::String (row.port), width / 2, 0, width / 2 - 70, height, juce::Justification::centredLeft, true);
        g.drawText (row.state, width - 72, 0, 68, height, juce::Justification::centredRight, true);
    }

    void DeviceList::listBoxItemClicked (int rowNumber, const juce::MouseEvent&)
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int> (rows.size()))
            return;

        auto row = rows[static_cast<std::size_t> (rowNumber)];

        if (row.source != Row::Source::session)
        {
            /*  Joining the session: the chunk gets the device, the registry
                starts it, and the recents remember it. A candidate not yet
                reachable is still added: the registry will say unreachable. */
            core::DeviceRef ref;
            ref.name = row.name.toStdString();
            ref.lastHost = row.host.toStdString();
            ref.lastPort = row.port;
            row.deviceIndex = instance.addDevice (ref);

            oscquery::Recents::Entry entry;
            entry.name = ref.name;
            entry.host = ref.lastHost;
            entry.port = ref.lastPort;
            recents.touch (entry);
        }

        selected = row.deviceIndex;

        if (onDeviceSelected != nullptr)
            onDeviceSelected (selected);
    }
}
