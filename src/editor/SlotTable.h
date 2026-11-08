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
    follows the PRD's failure table as far as Phase 3 knows it: unbound grey,
    active green, bound but without a range (inactive until min and max are
    entered) amber, bound to a device the registry cannot reach red.
*/

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
        juce::Colour stateColour (int row) const;
        bool deviceReachable (int deviceIndex) const;
        void changed();

        class RangeEditor;
        class ChoiceEditor;
        class ButtonCell;

        core::Instance& instance;
        std::function<float (int)> slotValue;
        juce::var status;

        juce::Label title { {}, "Slots" };
        juce::TableListBox table { "slots", this };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotTable)
    };
}
