/*
    This file is part of Go.dot — https://github.com/pob31/go.dot

    Copyright (C) 2026 Pierre-Olivier Boulant

    Go.dot is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version. Go.dot is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later

    Origin: go.dot cmake/JuceHeaderStub/JuceHeader.h at 1c8aaa0a, comment shortened.
*/

#pragma once

/*
    A stub `JuceHeader.h`, on the include path of the juce_simpleweb module and
    of nothing else.

    juce_simpleweb's .cpp files were written for the Projucer, which generates a
    global JuceHeader.h. A CMake build has none, so the module's .cpp files would
    fail on their first line. The two includes below are all those files reach
    for through it. cmake/OqaThirdParty.cmake puts this directory on the
    juce_simpleweb target's interface only, so `#include <JuceHeader.h>` anywhere
    in src/, lib/ or tests/ stays the error it should be.
*/

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
