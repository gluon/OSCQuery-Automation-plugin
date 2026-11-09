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
    THE MONITOR VIEW (devplan Phase 6c and 6e): the registry's view, which
    the Monitor plugin's editor is and which any instance may open (Phase
    6d-2's Monitor… button).

      top       the server line: NAME, ports, announced or not, writes taken
                and refused (PRD, Headless use); the receive port; the
                settings file
      devices   one row per device the registry drives: name, address (edit
                it to point the device by hand: PRD, Re-identification,
                "pointing once in the Monitor"), state, rates, timing (the
                tick and the offset, by hand and so process-wide: PRD,
                Transport), last error
      slots     every bound slot of every instance: instance label and
                number, slot id, name, device, path, direction, state in
                its shade
      settings  the per-user settings file's fields (6a-1), each applied on
                commit: default rate limit, resend, loop guard, gesture
                timeout, receive port, the server's enable, ports, allow-
                list and name, direct send with its warning; Clear recents
      pattern   a slot number, a path with $instance and a device NAME,
                bound in every slot plugin at once (6d-2): each instance
                substitutes its own number, as the server's commands do

    Owns nothing: every change goes through the Actions, which the root
    wires to a core::Instance (a Monitor's or any instance's).
*/

#include "MonitorModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace oqa::editor
{
    class MonitorView final : public juce::Component
    {
    public:
        struct Actions
        {
            std::function<void (const juce::String& name, const juce::String& identity, bool honoursTimetags, double offsetMs)> setDeviceTiming;
            std::function<bool (const juce::String& name, const juce::String& identity, const juce::String& host, int port)> pointDevice;
            std::function<bool (const juce::String& settingsJson, juce::String& error)> setSettings;
            std::function<int()> clearRecents;

            /** A command for the instance of this id, as the server would queue it (6d-2): bind a pattern across instances. */
            std::function<bool (juce::int64 instanceId, int slot, const juce::String& kind, const juce::String& text, const juce::String& text2, double number)> commandInstance;
        };

        explicit MonitorView (Actions actions);
        ~MonitorView() override;

        /** The rows and the settings as of now; fields being edited are left alone. */
        void refresh (const MonitorModel& model, const juce::var& settings);

        /** A word for the note line, until the next refresh with another. */
        void setNote (const juce::String& note);

        void paint (juce::Graphics& g) override;
        void resized() override;

        const MonitorModel& model() const noexcept { return shown; }

    private:
        class DeviceRows;
        class SlotRows;
        class SettingsForm;

        Actions actions;
        MonitorModel shown;

        juce::Label serverLabel, infoLabel, noteLabel;
        juce::Label devicesTitle { {}, "Devices" }, slotsTitle { {}, "Slots" }, settingsTitle { {}, "Settings" };

        //  Apply a pattern across instances (6d-2; PRD, UX: "apply the same
        //  path pattern across instances with $instance").
        juce::Label patternLabel { {}, "Bind slot" };
        juce::Label patternSlot, patternPath, patternDevice;
        juce::TextButton patternApply { "to every instance" };
        void applyPattern();
        std::unique_ptr<DeviceRows> deviceRows;
        std::unique_ptr<SlotRows> slotRows;
        juce::TableListBox devices, slots;
        std::unique_ptr<SettingsForm> settings;
        juce::Viewport settingsViewport;

        friend class DeviceRows;
        friend class SlotRows;
        friend class SettingsForm;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonitorView)
    };
}
