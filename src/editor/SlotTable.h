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
    THE SLOT TABLE (PRD, UX: "One row per slot: index, display name, device,
    path, range, scaling, direction, current value, state colour"; "Range and
    scaling editable inline"; "A Test button per slot that sends the current
    value once").

    The selected row is the slot the next leaf click binds. The state colour
    follows the PRD's failure table: unbound grey, active green, bound but
    without a range (inactive until min and max are entered) amber, bound to a
    device the registry cannot reach orange-red, receiving from a device
    without LISTEN that has not yet been told this plugin's UDP port amber with
    the port in the value cell; since Phase 5: stale (the path gone, retyped or
    moved, from the registry's `slots`) red with the old path shown and, where
    the device said where it went, the new one beside it, the Test button
    reading Rebind; unauthorised (ACCESS as reported to this client excludes
    write) amber, the value cell saying so. A receiving slot's value cell also
    shows what the device last said ("rx"), beside the host's value, because a
    lane in Read mode ignores the push and shows its own.

    EXPORT AND IMPORT (Phase 5c) are two buttons by the title; the file is the
    state chunk itself. The editor does the choosing and the writing.

    PHASE 6d-1: the name is edited in place (double-click); the range fields
    show the device's own RANGE greyed as their hint when the user has
    overridden it, and clearing both goes back to it; *Clear stale* unbinds
    every stale slot; every row has a tooltip in the PRD's failure table's
    words (SlotPresentation.h); a refused Test says why through onNote.
*/

#include "SlotPresentation.h"

#include <core/Instance.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace oqa::editor
{
    class SlotTable final : public juce::Component,
                            private juce::TableListBoxModel
    {
    public:
        SlotTable (core::Instance& instance, std::function<float (int)> slotValue);
        ~SlotTable() override;

        /** Something about the bindings or the devices changed. */
        void refresh (const juce::var& registryStatus);

        /** The current values only, cheap, for the timer. */
        void refreshValues();

        int selectedSlot() const noexcept { return table.getSelectedRow(); }
        void selectSlot (int slot) { table.selectRow (slot); }

        /** After the editor changed a binding: tell the host too. */
        std::function<void()> onChanged;

        /** The Rebind button of a stale slot (Phase 5): the editor rebinds, or arms the slot for the next leaf. */
        std::function<void (int slot)> onRebind;

        std::function<void()> onExport;
        std::function<void()> onImport;

        /** A word for the editor's warnings line (a Test that failed, a stale clear's count). */
        std::function<void (const juce::String&)> onNote;

        /** The registry's word on a bound slot: {id, state, detail, renamed_to}, or void. */
        juce::var slotStatus (int row) const;

        /** The registry's state of a slot: "ok", "pending", "missing", "retyped", "renamed", "unauthorised", "inactive", or "". */
        juce::String slotState (int row) const;

        bool slotStale (int row) const;

        void resized() override;

    private:
        enum Column
        {
            index = 1, name, device, path, range, scaling, direction, value, test, clear
        };

        int getNumRows() override;
        void paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected) override;
        void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool selected) override;
        juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
        int getColumnAutoSizeWidth (int column) override;

        juce::String cellText (int row, int column) const;
        juce::String getCellTooltip (int row, int column) override;
        presentation::Standing standingOf (int row) const;
        juce::Colour stateColour (int row) const;
        juce::var deviceStatus (int deviceIndex) const;
        bool deviceReachable (int deviceIndex) const;
        bool waitingForPort (const core::Slot& slot) const;
        void changed();

        class RangeEditor;
        class ChoiceEditor;
        class ButtonCell;
        class NameEditor;

        core::Instance& instance;
        std::function<float (int)> slotValue;
        juce::var status;

        juce::Label title { {}, "Slots" };
        juce::TextButton clearStaleButton { "Clear stale" };
        juce::TextButton exportButton { "Export..." };
        juce::TextButton importButton { "Import..." };
        juce::TableListBox table { "slots", this };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotTable)
    };
}
