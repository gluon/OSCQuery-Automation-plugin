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

#include "EditorWindow.h"

namespace oqa::shim
{
    EditorWindow::EditorWindow (juce::AudioProcessor& processor, std::unique_ptr<juce::Component> rootIn, SizeHints hints)
        : juce::AudioProcessorEditor (processor),
          root (std::move (rootIn))
    {
        addAndMakeVisible (*root);

        setResizable (true, true);
        setResizeLimits (hints.minimumWidth, hints.minimumHeight, hints.maximumWidth, hints.maximumHeight);
        setSize (hints.defaultWidth, hints.defaultHeight);
    }

    void EditorWindow::resized()
    {
        root->setBounds (getLocalBounds());
    }
}
