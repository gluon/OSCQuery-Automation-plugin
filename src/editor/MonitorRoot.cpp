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

#include "MonitorRoot.h"

#include <core/Instance.h>
#include <oscquery/Recents.h>

namespace oqa::editor
{
    namespace
    {
        MonitorView::Actions actionsFor (core::Instance& instance)
        {
            MonitorView::Actions a;

            a.setDeviceTiming = [&instance] (const juce::String& name, const juce::String& identity, bool honours, double offsetMs)
            {
                instance.setDeviceTimingByName (name.toStdString(), identity.toStdString(), honours, offsetMs);
            };

            a.pointDevice = [&instance] (const juce::String& name, const juce::String& identity, const juce::String& host, int port)
            {
                return instance.pointDeviceByName (name.toStdString(), identity.toStdString(), host.toStdString(), port);
            };

            a.setSettings = [&instance] (const juce::String& json, juce::String& error)
            {
                std::string why;
                const bool ok = instance.setSettingsJson (json.toStdString(), why);
                error = juce::String (why);
                return ok;
            };

            a.clearRecents = []
            {
                oscquery::Recents recents;
                recents.load();
                int removed = 0;

                for (const auto& entry : recents.all())
                    if (recents.remove (entry.name, entry.identityValue))
                        ++removed;

                recents.save();
                return removed;
            };

            return a;
        }
    }

    MonitorRoot::MonitorRoot (MonitorHost h)
        : host (h),
          monitor (actionsFor (host.instance))
    {
        addAndMakeVisible (monitor);
        monitor.setNote (host.coreNote);

        /*  The Finder browses for the Monitor too: a device the registry does
            not drive yet is still a device to see coming (the editor's list
            shows it; the Monitor's info line counts it). */
        host.instance.finderEnsure();

        setSize (defaultWidth, defaultHeight);
        refreshNow();
        startTimer (250);
    }

    MonitorRoot::~MonitorRoot()
    {
        stopTimer();
    }

    void MonitorRoot::resized()
    {
        monitor.setBounds (getLocalBounds());
    }

    void MonitorRoot::refreshNow()
    {
        const auto status = juce::JSON::parse (juce::String (host.instance.processStatusJson()));
        const auto settings = juce::JSON::parse (juce::String (host.instance.settingsJson()));
        monitor.refresh (MonitorModel::fromStatus (status), settings);
    }

    void MonitorRoot::timerCallback()
    {
        refreshNow();
    }
}
