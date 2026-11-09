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
    THE FIVE VARIANTS THE FACTORY LISTS (PRD, Binding model: "8, 16, 32, 64
    and 128 variants as separate plugin IDs so the host sees a fixed count").

    PLUGIN IDENTITY, FIXED FOR GOOD (decided 2026-11-09): the CLAP id of a
    variant is the bundle id the JUCE shim gives the same code,
    io.github.pob31.oqa.<code>. A session saved against one of these ids is
    orphaned if it changes.
*/

#include <clap/clap.h>

namespace oqa::clapshim
{
    struct Variant
    {
        const char* code;   ///< Q008 ... Q128, the plugin code the JUCE shim uses for the same slot count
        int slots;
        const char* id;     ///< the CLAP id
        const char* name;   ///< what the host shows
    };

    inline constexpr Variant variants[] = {
        { "Q008",   8, "io.github.pob31.oqa.Q008", "OSCQuery Automation 8"   },
        { "Q016",  16, "io.github.pob31.oqa.Q016", "OSCQuery Automation 16"  },
        { "Q032",  32, "io.github.pob31.oqa.Q032", "OSCQuery Automation 32"  },
        { "Q064",  64, "io.github.pob31.oqa.Q064", "OSCQuery Automation 64"  },
        { "Q128", 128, "io.github.pob31.oqa.Q128", "OSCQuery Automation 128" },
    };

    inline constexpr int variantCount = static_cast<int> (sizeof (variants) / sizeof (variants[0]));

    /** The descriptor of variant `index`, or nullptr beyond the five. Valid until the entry's deinit. */
    const clap_plugin_descriptor_t* descriptor (int index) noexcept;

    /** The variant with this CLAP id, or nullptr. */
    const Variant* variantFor (const char* id) noexcept;
}
