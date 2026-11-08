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
    A SYNTHETIC TREE OF ANY SIZE, for the large-tree check (spec draft L5: a
    tree of 10,000 leaves served within 2 s) and for a mock that has to be big.

    A fixture by construction, not a typed address: every path here is made
    of counters, names no device, and means nothing. It lives in tests/, where
    the devplan allows an address to appear, and nowhere else.

    Shape: `fanout` containers under the root, each holding `fanout` ranged
    float leaves, repeated until `leaves` leaves exist. 10,000 leaves at a
    fanout of 100 is 100 containers of 100, 10,101 nodes.
*/

#include <juce_core/juce_core.h>

#include <algorithm>

namespace oqatest
{
    inline juce::var generateTree (int leaves, int fanout = 100)
    {
        fanout = std::max (1, fanout);
        leaves = std::max (0, leaves);

        auto* root = new juce::DynamicObject();
        root->setProperty ("FULL_PATH", "/");
        root->setProperty ("ACCESS", 0);
        root->setProperty ("DESCRIPTION", "generated: " + juce::String (leaves) + " leaves");

        auto* contents = new juce::DynamicObject();
        int made = 0;

        for (int c = 0; made < leaves; ++c)
        {
            const auto containerName = "g" + juce::String (c);
            const auto containerPath = "/" + containerName;

            auto* container = new juce::DynamicObject();
            container->setProperty ("FULL_PATH", containerPath);
            container->setProperty ("ACCESS", 0);

            auto* children = new juce::DynamicObject();

            for (int l = 0; l < fanout && made < leaves; ++l, ++made)
            {
                const auto leafName = "p" + juce::String (l);

                auto* leaf = new juce::DynamicObject();
                leaf->setProperty ("FULL_PATH", containerPath + "/" + leafName);
                leaf->setProperty ("TYPE", "f");
                leaf->setProperty ("ACCESS", 3);
                leaf->setProperty ("VALUE", juce::Array<juce::var> { static_cast<double> (made) * 0.5 });

                auto* range = new juce::DynamicObject();
                range->setProperty ("MIN", 0.0);
                range->setProperty ("MAX", 1000.0);
                leaf->setProperty ("RANGE", juce::Array<juce::var> { juce::var (range) });
                leaf->setProperty ("DESCRIPTION", "generated leaf " + juce::String (made));

                children->setProperty (juce::Identifier (leafName), juce::var (leaf));
            }

            container->setProperty ("CONTENTS", juce::var (children));
            contents->setProperty (juce::Identifier (containerName), juce::var (container));
        }

        root->setProperty ("CONTENTS", juce::var (contents));
        return juce::var (root);
    }
}
