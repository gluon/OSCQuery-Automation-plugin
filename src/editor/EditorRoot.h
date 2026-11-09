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
    THE EDITOR, FIRST CUT (devplan Phase 3e; PRD, UX: "two panes and no modal
    dialogs: a device browser on the left and the slot table on the right").

      left     the device list (this session's devices with the registry's
               word on them; what DNS-SD found and what was typed, probed by
               the Finder; the per-user recents), and the selected device's
               tree with search-as-you-type
      right    the slot table: one row per slot, range, curve and direction
               editable in place, a Test button
      bottom   the status bar: shared core library or alone, each device's
               state and rates, the core's warnings

    BINDING IS CLICK A SLOT, THEN CLICK A LEAF. A multi-argument leaf takes
    consecutive slots from the one selected. Every bind writes the hash of the
    tree shown into the chunk (Phase 5a: the "tree changed since the bind"
    notice compares with it). Drag-and-drop and the bulk operations are Phase 6.

    PHASE 5: the device list's *Identity…* arms the next leaf click to set the
    selected device's identity leaf instead of binding; a stale slot's *Rebind*
    binds it to where the device said the path went, or, when it said nothing,
    selects the slot and its device's tree for the next leaf click; *Export*
    and *Import* read and write the state chunk through a file chooser (no
    modal of our own: the system's).

    THE EDITOR IS A CLIENT OF THE CORE: every change goes through
    core::Instance, and the processor is told so it renames its parameters.
    Nothing network runs on the message thread but the Finder's and the
    recents' reads; the registry does the rest.
*/

#include "DeviceList.h"
#include "SlotTable.h"
#include "StatusBar.h"
#include "TreePane.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace oqa::core { class Instance; }

namespace oqa::editor
{
    /** What the editor needs from its processor, so it depends on no shim. */
    struct Host
    {
        core::Instance& instance;
        std::function<void()> bindingsChanged;      ///< tell the host's parameters
        std::function<float (int slot)> slotValue;  ///< the host's current normalised value
        juce::String coreNote;                      ///< the loader's word: shared where, or alone why
    };

    class EditorRoot final : public juce::AudioProcessorEditor,
                             private juce::Timer
    {
    public:
        EditorRoot (juce::AudioProcessor& processor, Host host);
        ~EditorRoot() override;

        void paint (juce::Graphics& g) override;
        void resized() override;

    private:
        void timerCallback() override;
        void deviceSelected (int sessionDeviceIndex);
        void leafClicked (const oscquery::Node& node);
        void bindSelectedSlotTo (const oscquery::Node& node, int deviceIndex);
        void rebind (int slot);
        void exportBindings();
        void importBindings();
        void tellHost();

        Host host;

        DeviceList devices;
        TreePane tree;
        SlotTable slots;
        StatusBar status;

        juce::StretchableLayoutManager layout;
        juce::StretchableLayoutResizerBar divider { &layout, 1, true };

        std::uint64_t shownRevision = 0;

        //  Phase 5.
        int identityDevice = -1;        ///< the device whose identity the next leaf click sets, or -1
        std::unique_ptr<juce::FileChooser> chooser;
        juce::String editorNote;        ///< the editor's own word for the warnings line (a refused import), until the next action

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorRoot)
    };
}
