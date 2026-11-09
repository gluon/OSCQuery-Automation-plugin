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

#include "EditorRoot.h"

#include <core/Instance.h>

namespace oqa::editor
{
    EditorRoot::EditorRoot (juce::AudioProcessor& processor, Host hostIn)
        : juce::AudioProcessorEditor (processor),
          host (std::move (hostIn)),
          devices (host.instance),
          slots (host.instance, host.slotValue)
    {
        addAndMakeVisible (devices);
        addAndMakeVisible (tree);
        addAndMakeVisible (slots);
        addAndMakeVisible (status);
        addAndMakeVisible (divider);

        devices.onDeviceSelected = [this] (int index) { deviceSelected (index); };
        tree.onLeafClicked = [this] (const oscquery::Node& node) { bindSelectedSlotTo (node, devices.selectedDevice()); };
        slots.onChanged = [this]
        {
            if (host.bindingsChanged != nullptr)
                host.bindingsChanged();
        };

        status.setCoreNote (host.coreNote);
        status.onReceivePortChanged = [this] (int port)
        {
            /*  The registry's setting, process-wide: every instance's status
                shows the port the next tick. Not in the chunk (Phase 6 keeps
                the settings per user). */
            std::string error;
            host.instance.setSettingsJson ("{\"receive_port\": " + std::to_string (port) + "}", error);
        };

        layout.setItemLayout (0, 220, -0.6, -0.38);     // the browser
        layout.setItemLayout (1, 6, 6, 6);              // the divider
        layout.setItemLayout (2, 300, -0.8, -0.62);     // the slots

        setResizable (true, true);
        setResizeLimits (720, 420, 2400, 1600);
        setSize (1100, 620);

        timerCallback();
        startTimerHz (8);
    }

    EditorRoot::~EditorRoot()
    {
        stopTimer();
    }

    void EditorRoot::paint (juce::Graphics& g)
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }

    void EditorRoot::resized()
    {
        auto area = getLocalBounds();
        status.setBounds (area.removeFromBottom (72));
        area.reduce (6, 6);

        juce::Component* columns[] = { &devices, &divider, &slots };
        layout.layOutComponents (columns, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);

        /*  The browser column splits: devices above, the tree below. */
        auto browser = devices.getBounds();
        devices.setBounds (browser.removeFromTop (juce::jmax (120, browser.getHeight() / 3)));
        browser.removeFromTop (6);
        tree.setBounds (browser);
    }

    //==============================================================================
    void EditorRoot::timerCallback()
    {
        const auto statusVar = juce::JSON::parse (juce::String (host.instance.statusJson()));

        devices.refresh (statusVar);
        status.refresh (statusVar, host.instance.warnings());

        if (host.instance.revision() != shownRevision)
        {
            shownRevision = host.instance.revision();
            slots.refresh (statusVar);
        }
        else
        {
            slots.refreshValues();
        }

        /*  The selected device's tree, once the registry has it (or again if it changed). */
        const int selected = devices.selectedDevice();

        if (selected >= 0)
        {
            if (const auto json = host.instance.deviceTreeJson (selected))
            {
                std::string error;
                auto parsed = oscquery::Tree::parse (*json, error);

                if (parsed.has_value() && ! tree.holds (juce::String (parsed->treeHash())))
                {
                    const auto& chunk = host.instance.chunk();
                    const auto name = selected < static_cast<int> (chunk.devices.size())
                                        ? juce::String (chunk.devices[static_cast<std::size_t> (selected)].name) : juce::String();
                    tree.setTree (std::move (parsed), name);
                }
            }
        }
    }

    void EditorRoot::deviceSelected (int sessionDeviceIndex)
    {
        const auto& chunk = host.instance.chunk();
        const auto name = sessionDeviceIndex >= 0 && sessionDeviceIndex < static_cast<int> (chunk.devices.size())
                            ? juce::String (chunk.devices[static_cast<std::size_t> (sessionDeviceIndex)].name) : juce::String();

        /*  Show what is known now; the timer fills the tree in when it lands. */
        if (const auto json = host.instance.deviceTreeJson (sessionDeviceIndex))
        {
            std::string error;
            tree.setTree (oscquery::Tree::parse (*json, error), name);
        }
        else
        {
            tree.setTree (std::nullopt, name);
        }

        if (host.bindingsChanged != nullptr)
            host.bindingsChanged();
    }

    void EditorRoot::bindSelectedSlotTo (const oscquery::Node& node, int deviceIndex)
    {
        const int slot = slots.selectedSlot();

        if (slot < 0 || deviceIndex < 0)
            return;

        std::string why;
        bool ok = false;

        if (node.argumentCount() > 1)
            ok = host.instance.bindAll (slot, node, deviceIndex, why);
        else
            ok = host.instance.bind (slot, node, 0, deviceIndex, why);

        if (! ok)
        {
            /*  Not a modal: the reason goes where the warnings go. The status
                bar shows the instance's warnings on the next tick. */
            return;
        }

        if (host.bindingsChanged != nullptr)
            host.bindingsChanged();

        /*  Ready for the next leaf: the slot after the ones just bound. */
        const int next = slot + static_cast<int> (juce::jmax<std::size_t> (1, node.argumentCount()));

        if (next < host.instance.slotCount())
            slots.selectSlot (next);
    }
}
