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
    StatusBar::StatusBar()
    {
        addAndMakeVisible (receiveLabel);
        addAndMakeVisible (portField);
        receiveLabel.setFont (juce::FontOptions (11.0f));
        portField.setFont (juce::FontOptions (11.0f));
        portField.setEditable (true, false, false);
        portField.setTooltip ("The UDP port this plugin receives OSC on, for a device without LISTEN: type it into the device. 0 lets the system choose.");
        portField.setColour (juce::Label::outlineColourId, juce::Colours::grey.withAlpha (0.5f));
        portField.onTextChange = [this]
        {
            const int port = juce::jlimit (0, 65535, portField.getText().getIntValue());

            if (onReceivePortChanged != nullptr)
                onReceivePortChanged (port);
        };
    }

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

                const auto received = static_cast<juce::int64> (d.getProperty ("values_received", 0));
                double receivedPerSecond = 0.0;

                if (seen.atMs > 0.0 && nowMs > seen.atMs)
                    receivedPerSecond = static_cast<double> (received - seen.received) * 1000.0 / (nowMs - seen.atMs);

                seen.received = received;

                juce::String part = name + ": " + d.getProperty ("state", "").toString();

                if (static_cast<bool> (d.getProperty ("sender_ready", false)))
                    part << ", " << juce::String (perSecond, 0) << " bundles/s to " << d.getProperty ("resolved", "").toString();

                /*  Record: what comes in, and how (the websocket, or UDP for
                    a device without LISTEN), and the loop guard's count. */
                const auto listen = d.getProperty ("listen", "").toString();
                part << ", " << juce::String (receivedPerSecond, 0) << " rx/s"
                     << (listen == "listening" ? " (LISTEN)" : listen == "unsupported" ? " (UDP, no LISTEN)" : "");

                const auto guarded = static_cast<juce::int64> (d.getProperty ("loop_guarded", 0));

                if (guarded > 0)
                    part << ", guard " << juce::String (guarded);

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

        for (auto& [name, seen] : lastSeen)
            seen.atMs = nowMs;

        devicesLine = parts.isEmpty() ? juce::String ("No device in this session") : parts.joinIntoString ("   |   ");

        /*  The receive line: the bound port (0 while the receiver is not
            running: no receiving slot yet), this host's addresses, the
            datagrams nothing claimed. The field shows the SETTING, refreshed
            only when it changes under us, so typing is not overwritten. */
        const int boundPort = static_cast<int> (registryStatus.getProperty ("receive_port", 0));
        const int portSetting = static_cast<int> (registryStatus.getProperty ("settings", {}).getProperty ("receive_port", 0));

        if (portSetting != shownPortSetting && ! portField.isBeingEdited())
        {
            shownPortSetting = portSetting;
            portField.setText (juce::String (portSetting), juce::dontSendNotification);
        }

        juce::StringArray addresses;

        if (const auto* list = registryStatus.getProperty ("receive_addresses", {}).getArray())
            for (const auto& a : *list)
                addresses.add (a.toString());

        receiveLine = boundPort > 0 ? "bound to " + juce::String (boundPort) + " on " + addresses.joinIntoString (", ")
                                    : juce::String ("not receiving over UDP (no receiving slot; 0 means an ephemeral port once one is bound)");

        const auto unrouted = static_cast<juce::int64> (registryStatus.getProperty ("udp_unrouted", 0));

        if (unrouted > 0)
            receiveLine << "; " << juce::String (unrouted) << " datagrams for no bound path";

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
        const int line = area.getHeight() / 4;
        g.setFont (juce::FontOptions (11.0f));

        g.setColour (colours.findColour (juce::Label::textColourId).withAlpha (0.8f));
        g.drawText (coreNote, area.removeFromTop (line), juce::Justification::centredLeft, true);

        g.setColour (colours.findColour (juce::Label::textColourId));
        g.drawText (devicesLine, area.removeFromTop (line), juce::Justification::centredLeft, true);

        /*  The receive line, after the label and the field laid out in resized(). */
        auto receive = area.removeFromTop (line);
        receive.removeFromLeft (portField.getRight() + 6);
        g.setColour (colours.findColour (juce::Label::textColourId).withAlpha (0.9f));
        g.drawText (receiveLine, receive, juce::Justification::centredLeft, true);

        g.setColour (juce::Colours::orange);
        g.drawText (warningsLine, area, juce::Justification::centredLeft, true);
    }

    void StatusBar::resized()
    {
        auto area = getLocalBounds().reduced (6, 2);
        const int line = area.getHeight() / 4;
        area.removeFromTop (line * 2);
        auto receive = area.removeFromTop (line);
        receiveLabel.setBounds (receive.removeFromLeft (76));
        portField.setBounds (receive.removeFromLeft (60).reduced (0, 1));
    }
}
