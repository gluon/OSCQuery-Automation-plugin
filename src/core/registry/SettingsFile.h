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
    THE REGISTRY'S SETTINGS ON DISK (devplan Phase 6a-1; PRD, Architecture:
    "recents in the standard per-user settings location"; devplan Phase 6:
    "Global settings ... in the per-user settings file, editable from the
    Monitor"). One JSON file beside the recents store, in the same per-user
    folder, holding RegistrySettings::toVar(): read once when the registry is
    made, written whenever the settings change. Plain juce::JSON, whose number
    text is locale-independent (the fr_FR run says so); not a second
    juce::PropertiesFile on the recents' file, because two of those overwrite
    each other's keys.

    OQA_SETTINGS_FILE in the environment names another file: the tests set it
    to one of their own so no test touches the user's, and a bench can point a
    host at a file for one session.
*/

#include <juce_core/juce_core.h>

#include <optional>
#include <string>

namespace oqa::core
{
    class SettingsFile
    {
    public:
        /** The per-user file: beside the recents store, in the same folder. */
        static juce::File defaultFile();

        /** OQA_SETTINGS_FILE when set, else defaultFile(). */
        static juce::File chosen();

        /*  The file's JSON object. nullopt with `error` empty when the file does
            not exist (nothing saved yet), with `error` set when it exists and is
            not a JSON object. */
        static std::optional<juce::var> load (const juce::File& file, std::string& error);

        /** Write `settings` as JSON, making the folder; false with the reason. */
        static bool save (const juce::File& file, const juce::var& settings, std::string& error);
    };
}
