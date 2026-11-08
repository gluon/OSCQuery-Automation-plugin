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
    THE DEVICES THIS USER HAS USED (PRD, Discovery: "Recent devices list,
    persisted in the plugin's global settings (not the session), keyed by NAME
    plus last known host:port"; devplan Phase 2: "the standard JUCE
    PropertiesFile paths; the identity leaf's path and value kept alongside
    when set").

    Per user, not per session: the session's chunk carries each device too,
    but a sandboxed instance with a registry of its own (PRD, Architecture)
    finds the last address here. One entry per device: NAME, and the identity
    leaf's value when the user designated one, since two units of one model may
    share a NAME (spec draft C2). Newest first, capped.

    The file is a juce::PropertiesFile at JUCE's per-user settings location
    for "Pix et Bel / OSCQuery Automation Plugin", holding the list as one JSON
    value: a path the Monitor's global settings (Phase 6) will share. A test
    points it at a file of its own.

    Decimals never enter it (ports and times are integers), and the JSON writer
    is locale-independent anyway; the fr_FR run says so.
*/

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace oqa::oscquery
{
    class Recents
    {
    public:
        struct Entry
        {
            std::string name;
            std::string host;
            int port = 0;

            /** The identity leaf, when the user designated one (PRD, re-identification). */
            std::optional<std::string> identityPath;
            std::optional<std::string> identityValue;

            /** Milliseconds since the epoch, as juce::Time::currentTimeMillis(). */
            juce::int64 lastSeenMs = 0;

            /** What one entry is: its NAME, and its identity value when set. */
            bool sameDevice (const Entry& other) const
            {
                return name == other.name && identityValue == other.identityValue;
            }
        };

        struct Options
        {
            /** Empty: the standard per-user file (defaultFile()). */
            juce::File file;

            int capacity = 32;
        };

        /** The standard per-user file, capacity 32. */
        Recents();
        explicit Recents (Options options);
        ~Recents();

        Recents (const Recents&) = delete;
        Recents& operator= (const Recents&) = delete;

        /** Where the per-user file is on this platform. */
        static juce::File defaultFile();

        juce::File file() const;

        /** Reads the file. False when it exists and cannot be read; a missing file is an empty list. */
        bool load();

        /** Writes the file. False when it cannot be written. */
        bool save();

        /*  Puts `entry` first, stamped now if lastSeenMs is 0, replacing the
            entry for the same device; drops the oldest past capacity; saves. */
        bool touch (Entry entry);

        /** Removes every entry for that device. True when one was there. */
        bool remove (const std::string& name, const std::optional<std::string>& identityValue = std::nullopt);

        std::vector<Entry> all() const;
        std::optional<Entry> find (const std::string& name, const std::optional<std::string>& identityValue = std::nullopt) const;
        std::size_t size() const;

        /** The list as JSON, the shape the file holds. */
        juce::var toVar() const;

        /** Replaces the list from JSON of that shape. False, and nothing changed, if it is not a list. */
        bool fromVar (const juce::var& list);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
