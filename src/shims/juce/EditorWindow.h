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
    THE JUCE SHIM'S EDITOR WINDOW (Phase 5b-0): the AudioProcessorEditor JUCE's
    wrappers want, holding the one editor (editor::EditorRoot) that the CLAP
    shim puts straight into its host's window. Resizing and its limits are
    this window's; the editor only lays out.
*/

#include <editor/EditorRoot.h>

#include <juce_audio_processors/juce_audio_processors.h>

namespace oqa::shim
{
    class EditorWindow final : public juce::AudioProcessorEditor
    {
    public:
        EditorWindow (juce::AudioProcessor& processor, editor::Host host);

        void resized() override;

    private:
        editor::EditorRoot root;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorWindow)
    };
}
