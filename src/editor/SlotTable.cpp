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

#include "SlotTable.h"
#include "TreePane.h"

#include <optional>

namespace oqa::editor
{
    //==============================================================================
    /** Two small text fields: min and max. Enter commits. */
    class SlotTable::RangeEditor final : public juce::Component
    {
    public:
        explicit RangeEditor (SlotTable& owner) : table (owner)
        {
            for (auto* e : { &minField, &maxField })
            {
                e->setFont (juce::FontOptions (12.0f));
                e->setJustification (juce::Justification::centredRight);
                e->onReturnKey = [this] { commit(); };
                e->onFocusLost = [this] { commit(); };
                addAndMakeVisible (*e);
            }
        }

        void setRow (int newRow)
        {
            row = newRow;
            const auto& slot = table.instance.chunk().slots[row];
            const auto slotStatus = table.slotStatus (row);
            deviceMin = slotStatus.hasProperty ("device_range_min") ? std::optional<double> (static_cast<double> (slotStatus["device_range_min"])) : std::nullopt;
            deviceMax = slotStatus.hasProperty ("device_range_max") ? std::optional<double> (static_cast<double> (slotStatus["device_range_max"])) : std::nullopt;

            minField.setText (slot.rangeMin.has_value() ? juce::String (*slot.rangeMin) : juce::String(), juce::dontSendNotification);
            maxField.setText (slot.rangeMax.has_value() ? juce::String (*slot.rangeMax) : juce::String(), juce::dontSendNotification);
            const bool editable = slot.bound() && ! slot.isToggle();
            minField.setEnabled (editable);
            maxField.setEnabled (editable);

            /*  The device's own RANGE greyed as the hint (PRD, UX: "RANGE from
                the device shown greyed when overridden"); the user's values in
                amber when they differ from it; clearing both goes back to it. */
            const bool overridden = slot.rangeOverridden && deviceMin.has_value();
            minField.setTextToShowWhenEmpty (editable ? (deviceMin.has_value() ? juce::String (*deviceMin) : juce::String ("min")) : juce::String(), juce::Colours::grey);
            maxField.setTextToShowWhenEmpty (editable ? (deviceMax.has_value() ? juce::String (*deviceMax) : juce::String ("max")) : juce::String(), juce::Colours::grey);
            const auto colour = overridden ? juce::Colours::orange : getLookAndFeel().findColour (juce::TextEditor::textColourId);

            for (auto* e : { &minField, &maxField })
            {
                e->applyColourToAllText (colour, true);
                e->setTooltip (deviceMin.has_value()
                                   ? "The device's RANGE is " + juce::String (*deviceMin) + " .. " + juce::String (*deviceMax)
                                       + (overridden ? "; yours stands. Clear both fields to go back to the device's." : ".")
                                   : juce::String ("No RANGE from the device: enter min and max."));
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (1);
            minField.setBounds (area.removeFromLeft (area.getWidth() / 2 - 2));
            area.removeFromLeft (4);
            maxField.setBounds (area);
        }

    private:
        void commit()
        {
            const auto& slot = table.instance.chunk().slots[row];
            const auto minText = minField.getText().trim();
            const auto maxText = maxField.getText().trim();

            /*  Both cleared: back to the device's own range. */
            if (minText.isEmpty() && maxText.isEmpty() && slot.rangeOverridden && deviceMin.has_value() && deviceMax.has_value())
            {
                table.instance.clearRangeOverride (row, *deviceMin, *deviceMax);
                table.changed();
                return;
            }

            if (minText.isEmpty() || maxText.isEmpty())
                return;

            const auto min = minText.getDoubleValue();
            const auto max = maxText.getDoubleValue();

            if (slot.rangeMin == min && slot.rangeMax == max)
                return;

            if (min == max)
                return;

            table.instance.setRange (row, min, max);
            table.changed();
        }

        SlotTable& table;
        int row = 0;
        std::optional<double> deviceMin, deviceMax;
        juce::TextEditor minField, maxField;
    };

    /** The path, typed or pasted in place: double-click, type, Enter; $instance allowed; empty unbinds (6d-2). */
    class SlotTable::PathEditor final : public juce::Component
    {
    public:
        explicit PathEditor (SlotTable& owner) : table (owner)
        {
            label.setFont (juce::FontOptions (12.0f));
            label.setEditable (false, true, false);
            label.setMinimumHorizontalScale (0.6f);
            label.onTextChange = [this]
            {
                if (table.onPathTyped != nullptr)
                    table.onPathTyped (row, label.getText().trim());
            };
            label.onEditorShow = [this]
            {
                /*  The pattern is what the user typed; editing starts from it. */
                const auto& slot = table.instance.chunk().slots[row];

                if (auto* e = label.getCurrentTextEditor())
                    e->setText (juce::String (slot.pathPattern.empty() ? slot.path : slot.pathPattern), false);
            };
            addAndMakeVisible (label);
        }

        void setRow (int newRow)
        {
            row = newRow;
            label.setText (table.cellText (row, Column::path), juce::dontSendNotification);
            label.setTooltip (table.getCellTooltip (row, Column::path)
                              + (table.getCellTooltip (row, Column::path).isEmpty() ? "" : " ")
                              + "Double-click to type or paste a path; $instance stands for this instance's number"
                              + (table.instance.chunk().slots[row].pathPattern.empty() ? juce::String (".")
                                                                                        : " (pattern " + juce::String (table.instance.chunk().slots[row].pathPattern) + ")."));
            const bool patterned = ! table.instance.chunk().slots[row].pathPattern.empty();
            label.setColour (juce::Label::textColourId,
                             patterned ? juce::Colours::lightskyblue : getLookAndFeel().findColour (juce::ListBox::textColourId));
        }

        void resized() override { label.setBounds (getLocalBounds()); }

    private:
        SlotTable& table;
        int row = 0;
        juce::Label label;
    };

    /** The display name, edited in place: double-click, type, Enter. */
    class SlotTable::NameEditor final : public juce::Component
    {
    public:
        explicit NameEditor (SlotTable& owner) : table (owner)
        {
            label.setFont (juce::FontOptions (12.0f));
            label.setEditable (false, true, false);
            label.setTooltip ("The lane's name in the host; double-click to rename.");
            label.onTextChange = [this]
            {
                const auto text = label.getText().trim();

                if (text.isNotEmpty() && text != juce::String (table.instance.chunk().slots[row].displayName))
                {
                    table.instance.setDisplayName (row, text.toStdString());
                    table.changed();
                }
            };
            addAndMakeVisible (label);
        }

        void setRow (int newRow)
        {
            row = newRow;
            const auto& slot = table.instance.chunk().slots[row];
            label.setText (slot.bound() ? juce::String (slot.displayName) : juce::String ("(unbound)"), juce::dontSendNotification);
            label.setEnabled (slot.bound());
            label.setColour (juce::Label::textColourId, getLookAndFeel().findColour (juce::ListBox::textColourId).withAlpha (slot.bound() ? 1.0f : 0.5f));
        }

        void resized() override { label.setBounds (getLocalBounds()); }

    private:
        SlotTable& table;
        int row = 0;
        juce::Label label;
    };

    /** A combo box for the curve or the direction. */
    class SlotTable::ChoiceEditor final : public juce::Component
    {
    public:
        ChoiceEditor (SlotTable& owner, Column which) : table (owner), column (which)
        {
            if (column == Column::scaling)
            {
                box.addItem ("linear", 1);
                box.addItem ("log", 2);
                box.addItem ("exp", 3);
            }
            else
            {
                box.addItem ("send", 1);
                box.addItem ("receive", 2);
                box.addItem ("both", 3);
            }

            box.onChange = [this] { commit(); };
            addAndMakeVisible (box);
        }

        void setRow (int newRow)
        {
            row = newRow;
            const auto& slot = table.instance.chunk().slots[row];
            const int id = column == Column::scaling ? static_cast<int> (slot.scaling) + 1 : static_cast<int> (slot.direction) + 1;
            box.setSelectedId (id, juce::dontSendNotification);
            box.setEnabled (slot.bound());
        }

        void resized() override { box.setBounds (getLocalBounds().reduced (1)); }

    private:
        void commit()
        {
            const int id = box.getSelectedId();

            if (id <= 0)
                return;

            if (column == Column::scaling)
                table.instance.setScaling (row, static_cast<core::Scaling> (id - 1));
            else
                table.instance.setDirection (row, static_cast<core::Direction> (id - 1));

            table.changed();
        }

        SlotTable& table;
        Column column;
        int row = 0;
        juce::ComboBox box;
    };

    /** Test (Rebind on a stale slot), or Clear. */
    class SlotTable::ButtonCell final : public juce::Component
    {
    public:
        ButtonCell (SlotTable& owner, Column which) : table (owner), column (which), button (which == Column::test ? "Test" : "x")
        {
            button.onClick = [this] { act(); };
            addAndMakeVisible (button);
        }

        void setRow (int newRow)
        {
            row = newRow;
            const auto& slot = table.instance.chunk().slots[row];
            button.setEnabled (slot.bound());

            if (column == Column::test)
            {
                stale = table.slotStale (row);
                button.setButtonText (stale ? "Rebind" : "Test");
                button.setTooltip (stale ? table.slotStatus (row).getProperty ("detail", "").toString() : juce::String());
            }
        }

        void resized() override { button.setBounds (getLocalBounds().reduced (2)); }

    private:
        void act()
        {
            if (column == Column::test)
            {
                if (stale)
                {
                    if (table.onRebind != nullptr)
                        table.onRebind (row);

                    return;
                }

                std::string error;

                if (! table.instance.testSend (row, table.slotValue (row), error) && table.onNote != nullptr)
                    table.onNote ("Test: " + juce::String (error));
            }
            else
            {
                table.instance.unbind (row);
                table.changed();
            }
        }

        bool stale = false;

        SlotTable& table;
        Column column;
        int row = 0;
        juce::TextButton button;
    };

    //==============================================================================
    SlotTable::SlotTable (core::Instance& instanceIn, std::function<float (int)> slotValueIn)
        : instance (instanceIn), slotValue (std::move (slotValueIn))
    {
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        addAndMakeVisible (title);

        exportButton.setTooltip ("Write this instance's bindings to a JSON file: the state chunk, for another instance, DAW or client.");
        exportButton.onClick = [this] { if (onExport != nullptr) onExport(); };
        addAndMakeVisible (exportButton);

        importButton.setTooltip ("Replace this instance's bindings from a JSON file written by Export or by tools/convert-mapping.py.");
        importButton.onClick = [this] { if (onImport != nullptr) onImport(); };
        addAndMakeVisible (importButton);

        instanceLabel.setFont (juce::FontOptions (12.0f));
        instanceLabel.setTooltip ("The number $instance stands for in this instance's paths (ToscA-style per-track numbering). Kept in the session; every slot bound through a pattern follows it.");
        addAndMakeVisible (instanceLabel);
        instanceField.setFont (juce::FontOptions (12.0f));
        instanceField.setEditable (true, false, false);
        instanceField.setJustificationType (juce::Justification::centred);
        instanceField.setColour (juce::Label::outlineColourId, juce::Colours::grey.withAlpha (0.5f));
        instanceField.setTooltip (instanceLabel.getTooltip());
        instanceField.onTextChange = [this]
        {
            const int number = instanceField.getText().getIntValue();

            if (number >= 1 && number != instance.instanceNumber())
            {
                instance.setInstanceNumber (number);

                if (onNote != nullptr && ! instance.warnings().empty())
                    onNote (juce::String (instance.warnings().back()));

                changed();
            }

            updateInstanceNumber();
        };
        addAndMakeVisible (instanceField);
        updateInstanceNumber();

        monitorButton.setTooltip ("The registry's view: every device, every slot across instances, rates, errors, the server, the settings. Not modal.");
        monitorButton.onClick = [this] { if (onMonitor != nullptr) onMonitor(); };
        addAndMakeVisible (monitorButton);

        clearStaleButton.setTooltip ("Unbind every slot whose path the device no longer serves (missing, retyped or moved). The lanes keep their automation.");
        clearStaleButton.onClick = [this]
        {
            const auto cleared = instance.clearStale();

            if (onNote != nullptr)
                onNote (cleared > 0 ? juce::String (cleared) + " stale slot(s) unbound" : juce::String ("no stale slot"));

            changed();
        };
        addAndMakeVisible (clearStaleButton);

        auto& header = table.getHeader();
        header.addColumn ("#", Column::index, 30, 30, 40);
        header.addColumn ("Name", Column::name, 150, 60, 400);
        header.addColumn ("Device", Column::device, 100, 40, 300);
        header.addColumn ("Path", Column::path, 200, 60, 600);
        header.addColumn ("Range", Column::range, 120, 90, 200);
        header.addColumn ("Curve", Column::scaling, 70, 60, 100);
        header.addColumn ("Direction", Column::direction, 80, 70, 110);
        header.addColumn ("Value", Column::value, 90, 50, 200);
        header.addColumn ("", Column::test, 50, 50, 50);
        header.addColumn ("", Column::clear, 30, 30, 30);
        header.setStretchToFitActive (true);

        table.setRowHeight (24);
        table.setMultipleSelectionEnabled (false);
        addAndMakeVisible (table);
    }

    SlotTable::~SlotTable() = default;

    void SlotTable::resized()
    {
        auto area = getLocalBounds();
        auto top = area.removeFromTop (24);
        monitorButton.setBounds (top.removeFromRight (80).reduced (0, 2));
        top.removeFromRight (8);
        importButton.setBounds (top.removeFromRight (80).reduced (0, 2));
        top.removeFromRight (4);
        exportButton.setBounds (top.removeFromRight (80).reduced (0, 2));
        top.removeFromRight (4);
        clearStaleButton.setBounds (top.removeFromRight (90).reduced (0, 2));
        top.removeFromRight (8);
        instanceField.setBounds (top.removeFromRight (44).reduced (0, 3));
        instanceLabel.setBounds (top.removeFromRight (66));
        title.setBounds (top);
        table.setBounds (area);
    }

    juce::var SlotTable::slotStatus (int row) const
    {
        if (const auto* slots = status.getProperty ("slots", {}).getArray())
            for (const auto& s : *slots)
                if (static_cast<int> (s.getProperty ("id", -1)) == row)
                    return s;

        return {};
    }

    juce::String SlotTable::slotState (int row) const
    {
        return slotStatus (row).getProperty ("state", "").toString();
    }

    bool SlotTable::slotStale (int row) const
    {
        const auto state = slotState (row);
        return state == "missing" || state == "retyped" || state == "renamed";
    }

    void SlotTable::updateInstanceNumber()
    {
        if (! instanceField.isBeingEdited())
            instanceField.setText (juce::String (instance.instanceNumber()), juce::dontSendNotification);
    }

    //==============================================================================
    //  A leaf dragged from the tree (6d-2).
    int SlotTable::rowUnderDrag (const SourceDetails& details) const
    {
        const auto inTable = table.getLocalPoint (this, details.localPosition);
        const int row = table.getRowContainingPosition (inTable.x, inTable.y);
        return row >= 0 && row < instance.slotCount() ? row : -1;
    }

    bool SlotTable::isInterestedInDragSource (const SourceDetails& details)
    {
        return details.description.toString().startsWith (TreePane::dragPrefix);
    }

    void SlotTable::itemDragEnter (const SourceDetails& details) { itemDragMove (details); }

    void SlotTable::itemDragMove (const SourceDetails& details)
    {
        const int row = rowUnderDrag (details);

        if (row != dropRow)
        {
            dropRow = row;
            table.repaint();
        }
    }

    void SlotTable::itemDragExit (const SourceDetails&)
    {
        dropRow = -1;
        table.repaint();
    }

    void SlotTable::itemDropped (const SourceDetails& details)
    {
        const int row = rowUnderDrag (details);
        dropRow = -1;
        table.repaint();

        if (row >= 0 && onLeafDropped != nullptr)
            onLeafDropped (row, details.description.toString().substring (juce::String (TreePane::dragPrefix).length()));
    }

    void SlotTable::refresh (const juce::var& registryStatus)
    {
        status = registryStatus;
        updateInstanceNumber();

        bool anyStale = false;

        for (int row = 0; row < instance.slotCount() && ! anyStale; ++row)
            anyStale = slotStale (row);

        clearStaleButton.setEnabled (anyStale);
        table.updateContent();
        table.repaint();
    }

    void SlotTable::refreshValues()
    {
        table.repaintRow (-1);
        table.repaint();
    }

    void SlotTable::changed()
    {
        if (onChanged != nullptr)
            onChanged();
    }

    //==============================================================================
    int SlotTable::getNumRows()
    {
        return instance.slotCount();
    }

    juce::var SlotTable::deviceStatus (int deviceIndex) const
    {
        const auto* devices = status.getProperty ("devices", {}).getArray();

        if (devices == nullptr || deviceIndex < 0 || deviceIndex >= devices->size())
            return {};

        return (*devices)[deviceIndex];
    }

    bool SlotTable::deviceReachable (int deviceIndex) const
    {
        return static_cast<bool> (deviceStatus (deviceIndex).getProperty ("reachable", false));
    }

    /*  A receiving slot on a device without LISTEN, before the first datagram
        has come in: record waits for the device to be told this plugin's port
        (PRD; devplan Phase 4: "the editor says on such a device that record is
        waiting for the device to be told the port"). */
    bool SlotTable::waitingForPort (const core::Slot& slot) const
    {
        if (! slot.receives() || ! slot.active())
            return false;

        const auto device = deviceStatus (slot.deviceIndex);
        return device.getProperty ("listen", "").toString() == "unsupported"
            && static_cast<juce::int64> (device.getProperty ("udp_received", 0)) == 0;
    }

    presentation::Standing SlotTable::standingOf (int row) const
    {
        const auto& slot = instance.chunk().slots[row];
        const auto s = slotStatus (row);
        const auto device = deviceStatus (slot.deviceIndex);

        presentation::Standing out;
        out.bound = slot.bound();
        out.active = slot.active();
        out.state = s.getProperty ("state", "").toString().toStdString();
        out.detail = s.getProperty ("detail", "").toString().toStdString();
        out.renamedTo = s.getProperty ("renamed_to", "").toString().toStdString();
        out.deviceReachable = ! slot.bound() || deviceReachable (slot.deviceIndex);
        out.waitingForPort = waitingForPort (slot);
        out.deviceLastSeenMs = static_cast<juce::int64> (device.getProperty ("last_seen_ms", -1));
        out.loopGuarded = static_cast<juce::int64> (device.getProperty ("loop_guarded", 0));
        out.rateLimited = static_cast<juce::int64> (device.getProperty ("rate_limited", 0));
        out.datagramsDropped = static_cast<juce::int64> (device.getProperty ("datagrams_dropped", 0));
        return out;
    }

    juce::Colour SlotTable::stateColour (int row) const
    {
        switch (presentation::shadeOf (standingOf (row)))
        {
            case presentation::Shade::grey:      return juce::Colours::grey;
            case presentation::Shade::amber:     return juce::Colours::orange;
            case presentation::Shade::red:       return juce::Colours::red;
            case presentation::Shade::orangeRed: return juce::Colours::orangered;
            case presentation::Shade::green:     return juce::Colours::limegreen;
        }

        return juce::Colours::grey;
    }

    juce::String SlotTable::getCellTooltip (int row, int column)
    {
        if (row < 0 || row >= instance.slotCount())
            return {};

        if (column == Column::index || column == Column::value || column == Column::path || column == Column::device)
            return juce::String (presentation::tooltipOf (standingOf (row)));

        return {};
    }

    juce::String SlotTable::cellText (int row, int column) const
    {
        const auto& chunk = instance.chunk();
        const auto& slot = chunk.slots[row];

        switch (column)
        {
            case Column::index:  return juce::String (row + 1);
            case Column::name:   return slot.bound() ? juce::String (slot.displayName) : juce::String ("(unbound)");
            case Column::device:
                if (slot.deviceIndex >= 0 && slot.deviceIndex < static_cast<int> (chunk.devices.size()))
                    return juce::String (chunk.devices[static_cast<std::size_t> (slot.deviceIndex)].name);
                return {};
            case Column::path:
            {
                if (! slot.bound())
                    return {};

                auto text = juce::String (slot.path) + (slot.argIndex > 0 || chunk.slots.groupOf (row).size() > 1
                                                            ? " [" + juce::String (slot.argIndex) + "]" : juce::String());

                /*  A moved path: the old one the lane holds, and where the device says it went. */
                const auto movedTo = slotStatus (row).getProperty ("renamed_to", "").toString();

                if (movedTo.isNotEmpty())
                    text += "  ->  " + movedTo;

                return text;
            }
            case Column::value:
            {
                if (! slot.bound())
                    return {};

                const auto normalised = slotValue (row);

                if (slot.isToggle())
                    return normalised >= 0.5f ? "on" : "off";

                if (! slot.active())
                    return juce::String (normalised, 3) + " (no range)";

                /*  Stale or unauthorised (Phase 5a): the registry's words. */
                const auto state = slotState (row);

                if (slotStale (row))
                    return "stale: " + slotStatus (row).getProperty ("detail", "").toString();

                if (state == "unauthorised")
                    return slotStatus (row).getProperty ("detail", "").toString();

                if (state == "pending")
                    return slotStatus (row).getProperty ("detail", "").toString();

                if (waitingForPort (slot))
                    return "waiting for UDP port " + juce::String (static_cast<int> (status.getProperty ("receive_port", 0)));

                /*  What the device last said, beside the host's value, so a lane
                    in Read mode (which ignores the push) still shows it. */
                juce::String received;

                if (slot.receives())
                    if (const auto last = instance.lastReceived (row))
                    {
                        const auto rx = slot.toDevice (*last);
                        received = "  rx " + (slot.isInteger() ? juce::String (static_cast<int> (rx)) : juce::String (rx, 3));
                    }

                const auto units = slot.toDevice (normalised);
                return received + " " + (slot.isInteger() ? juce::String (static_cast<int> (units)) : juce::String (units, 3))
                     + (slot.unit.empty() ? juce::String() : " " + juce::String (slot.unit));
            }
            default: return {};
        }
    }

    void SlotTable::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected)
    {
        const auto& colours = getLookAndFeel();

        if (selected)
            g.fillAll (colours.findColour (juce::ListBox::backgroundColourId).brighter (0.25f));
        else if (row % 2 == 1)
            g.fillAll (colours.findColour (juce::ListBox::backgroundColourId).brighter (0.05f));

        /*  The row a dragged leaf would bind. */
        if (row == dropRow)
        {
            g.setColour (juce::Colours::lightskyblue.withAlpha (0.25f));
            g.fillRect (0, 0, width, height);
            g.setColour (juce::Colours::lightskyblue);
            g.drawRect (0, 0, width, height, 2);
        }
    }

    void SlotTable::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
    {
        if (row < 0 || row >= instance.slotCount())
            return;

        const auto& colours = getLookAndFeel();

        if (column == Column::index)
        {
            g.setColour (stateColour (row));
            g.fillEllipse (3.0f, height / 2.0f - 4.0f, 8.0f, 8.0f);
            g.setColour (colours.findColour (juce::ListBox::textColourId));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (cellText (row, column), 14, 0, width - 14, height, juce::Justification::centredLeft, true);
            return;
        }

        const auto& slot = instance.chunk().slots[row];
        g.setColour (colours.findColour (juce::ListBox::textColourId).withAlpha (slot.bound() ? 1.0f : 0.5f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (cellText (row, column), 4, 0, width - 8, height,
                    column == Column::value ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
    }

    juce::Component* SlotTable::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
    {
        switch (column)
        {
            case Column::path:
            {
                auto* editor = dynamic_cast<PathEditor*> (existing);

                if (editor == nullptr)
                {
                    delete existing;
                    editor = new PathEditor (*this);
                }

                editor->setRow (row);
                return editor;
            }

            case Column::name:
            {
                auto* editor = dynamic_cast<NameEditor*> (existing);

                if (editor == nullptr)
                {
                    delete existing;
                    editor = new NameEditor (*this);
                }

                editor->setRow (row);
                return editor;
            }

            case Column::range:
            {
                auto* editor = dynamic_cast<RangeEditor*> (existing);

                if (editor == nullptr)
                {
                    delete existing;
                    editor = new RangeEditor (*this);
                }

                editor->setRow (row);
                return editor;
            }

            case Column::scaling:
            case Column::direction:
            {
                auto* editor = dynamic_cast<ChoiceEditor*> (existing);

                if (editor == nullptr)
                {
                    delete existing;
                    editor = new ChoiceEditor (*this, static_cast<Column> (column));
                }

                editor->setRow (row);
                return editor;
            }

            case Column::test:
            case Column::clear:
            {
                auto* cell = dynamic_cast<ButtonCell*> (existing);

                if (cell == nullptr)
                {
                    delete existing;
                    cell = new ButtonCell (*this, static_cast<Column> (column));
                }

                cell->setRow (row);
                return cell;
            }

            default:
                delete existing;
                return nullptr;
        }
    }

    int SlotTable::getColumnAutoSizeWidth (int column)
    {
        int widest = 40;
        juce::Font font (juce::FontOptions (12.0f));

        for (int row = 0; row < getNumRows(); ++row)
            widest = juce::jmax (widest, static_cast<int> (juce::GlyphArrangement::getStringWidth (font, cellText (row, column))) + 12);

        return widest;
    }
}
