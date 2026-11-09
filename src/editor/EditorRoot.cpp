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

#include <core/Identity.h>
#include <core/Instance.h>

namespace oqa::editor
{
    EditorRoot::EditorRoot (Host hostIn)
        : host (std::move (hostIn)),
          devices (host.instance),
          slots (host.instance, host.slotValue)
    {
        addAndMakeVisible (devices);
        addAndMakeVisible (tree);
        addAndMakeVisible (slots);
        addAndMakeVisible (status);
        addAndMakeVisible (divider);

        devices.onDeviceSelected = [this] (int index) { deviceSelected (index); };
        devices.onIdentityArm = [this] (int deviceIndex, bool arm)
        {
            identityDevice = arm ? deviceIndex : -1;
            tree.setPrompt (arm ? "click a read-only leaf to make it the identity" : juce::String());
        };
        tree.onLeafClicked = [this] (const oscquery::Node& node) { leafClicked (node); };
        slots.onChanged = [this] { tellHost(); };
        slots.onRebind = [this] (int slot) { rebind (slot); };
        slots.onExport = [this] { exportBindings(); };
        slots.onImport = [this] { importBindings(); };

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

        setSize (defaultWidth, defaultHeight);

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

        /*  The registry's warnings as they stand now (a plan rebuilt when the
            tree lands, a device re-identified: Phase 5), not as they were at
            the last push; the instance's own when the registry has none. */
        std::vector<std::string> warnings;

        if (const auto* live = statusVar.getProperty ("warnings", {}).getArray())
            for (const auto& w : *live)
                warnings.push_back (w.toString().toStdString());
        else
            warnings = host.instance.warnings();

        if (editorNote.isNotEmpty())
            warnings.insert (warnings.begin(), editorNote.toStdString());

        devices.refresh (statusVar);
        status.refresh (statusVar, warnings);

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

    void EditorRoot::tellHost()
    {
        editorNote.clear();

        if (host.bindingsChanged != nullptr)
            host.bindingsChanged();
    }

    void EditorRoot::leafClicked (const oscquery::Node& node)
    {
        /*  Armed for the identity (Phase 5b): a read-only leaf with a value is
            what tells this device from another of the same name. Anything
            else leaves the arm as it is. */
        if (identityDevice >= 0)
        {
            if (node.bindableForRecord() && node.hasValue && ! node.value.empty())
            {
                host.instance.setDeviceIdentity (identityDevice, node.fullPath, core::identityText (node.value));
                identityDevice = -1;
                tree.setPrompt ({});
                devices.identityDone();
                tellHost();
            }

            return;
        }

        bindSelectedSlotTo (node, devices.selectedDevice());
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

        /*  The tree this bind was made against (Phase 5a): the notice "the
            tree changed since the bind" compares the live tree with it. */
        if (const auto hash = tree.currentHash(); hash.isNotEmpty())
            host.instance.setDeviceTreeHash (deviceIndex, hash.toStdString());

        tellHost();

        /*  Ready for the next leaf: the slot after the ones just bound. */
        const int next = slot + static_cast<int> (juce::jmax<std::size_t> (1, node.argumentCount()));

        if (next < host.instance.slotCount())
            slots.selectSlot (next);
    }

    /*  A stale slot's Rebind (Phase 5a; PRD: "a renamed path shows stale and
        is rebound in one click"): where the device said the path went, the
        slot is bound there now; where it said nothing, the slot is selected
        and its device's tree shown, for the next leaf click. */
    void EditorRoot::rebind (int slot)
    {
        const auto& chunk = host.instance.chunk();

        if (slot < 0 || slot >= host.instance.slotCount() || ! chunk.slots[slot].bound())
            return;

        const auto bound = chunk.slots[slot];
        const auto movedTo = slots.slotStatus (slot).getProperty ("renamed_to", "").toString().toStdString();

        if (! movedTo.empty())
        {
            if (const auto json = host.instance.deviceTreeJson (bound.deviceIndex, movedTo))
            {
                std::string error;
                const auto parsed = juce::JSON::parse (juce::String (*json));

                if (const auto node = oscquery::Tree::parseNode (parsed, movedTo, error))
                {
                    std::string why;

                    if (host.instance.bind (slot, *node, static_cast<std::size_t> (juce::jmax (0, bound.argIndex)), bound.deviceIndex, why))
                    {
                        if (const auto whole = host.instance.deviceTreeJson (bound.deviceIndex))
                            if (const auto wholeTree = oscquery::Tree::parse (*whole, error))
                                host.instance.setDeviceTreeHash (bound.deviceIndex, wholeTree->treeHash());

                        tellHost();
                        return;
                    }
                }
            }
        }

        slots.selectSlot (slot);
        deviceSelected (bound.deviceIndex);
    }

    void EditorRoot::exportBindings()
    {
        const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("oscquery-bindings.json");
        chooser = std::make_unique<juce::FileChooser> ("Export bindings", suggested, "*.json");

        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();

                                  if (file == juce::File())
                                      return;

                                  file.replaceWithText (juce::String (host.instance.saveState()), false, false, "\n");
                              });
    }

    void EditorRoot::importBindings()
    {
        const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        chooser = std::make_unique<juce::FileChooser> ("Import bindings", start, "*.json");

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();

                                  if (file == juce::File())
                                      return;

                                  /*  A file that is not a chunk is refused with
                                      the reason where the warnings go; the
                                      bindings stay as they were. */
                                  std::string error;
                                  const bool ok = host.instance.loadState (file.loadFileAsString().toStdString(), error);
                                  tellHost();

                                  if (! ok)
                                      editorNote = "import refused: " + file.getFileName() + ": " + juce::String (error);
                              });
    }
}
