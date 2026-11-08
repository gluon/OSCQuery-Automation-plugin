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

#include "StatusBar.h"

namespace oqa::editor
{
    StatusBar::StatusBar() = default;

    void StatusBar::setCoreNote (const juce::String& note)
    {
        coreNote = note;
        repaint();
    }

    void StatusBar::refresh (const juce::var& registryStatus, const std::vector<std::string>& warnings)
    {
        const auto nowMs = juce::Time::getMillisecondCounterHiRes();
        juce::StringArray parts;

        if (const auto* devices = registryStatus.getProperty ("devices", {}).getArray())
        {
            for (const auto& d : *devices)
            {
                if (d.isVoid())
                    continue;

                const auto name = d.getProperty ("name", "?").toString();
                const auto bundles = static_cast<juce::int64> (d.getProperty ("bundles_sent", 0));
                auto& seen = lastSeen[name];
                double perSecond = 0.0;

                if (seen.atMs > 0.0 && nowMs > seen.atMs)
                    perSecond = static_cast<double> (bundles - seen.bundles) * 1000.0 / (nowMs - seen.atMs);

                seen.bundles = bundles;
                seen.atMs = nowMs;

                juce::String part = name + ": " + d.getProperty ("state", "").toString();

                if (static_cast<bool> (d.getProperty ("sender_ready", false)))
                    part << ", " << juce::String (perSecond, 0) << " bundles/s to " << d.getProperty ("resolved", "").toString();

                const auto dropped = static_cast<juce::int64> (d.getProperty ("datagrams_dropped", 0));
                const auto unsent = static_cast<juce::int64> (d.getProperty ("unsent_no_route", 0));

                if (dropped > 0)
                    part << ", " << juce::String (dropped) << " dropped";

                if (unsent > 0)
                    part << ", " << juce::String (unsent) << " unsent";

                const auto error = d.getProperty ("last_error", "").toString();

                if (error.isNotEmpty())
                    part << " (" << error << ")";

                parts.add (part);
            }
        }

        devicesLine = parts.isEmpty() ? juce::String ("No device in this session") : parts.joinIntoString ("   |   ");

        juce::StringArray words;

        for (const auto& w : warnings)
            words.add (juce::String (w));

        warningsLine = words.joinIntoString ("   |   ");
        repaint();
    }

    void StatusBar::paint (juce::Graphics& g)
    {
        const auto& colours = getLookAndFeel();
        g.fillAll (colours.findColour (juce::ResizableWindow::backgroundColourId).darker (0.3f));

        auto area = getLocalBounds().reduced (6, 2);
        g.setFont (juce::FontOptions (11.0f));

        g.setColour (colours.findColour (juce::Label::textColourId).withAlpha (0.8f));
        g.drawText (coreNote, area.removeFromTop (area.getHeight() / 3), juce::Justification::centredLeft, true);

        g.setColour (colours.findColour (juce::Label::textColourId));
        g.drawText (devicesLine, area.removeFromTop (area.getHeight() / 2), juce::Justification::centredLeft, true);

        g.setColour (juce::Colours::orange);
        g.drawText (warningsLine, area, juce::Justification::centredLeft, true);
    }
}
