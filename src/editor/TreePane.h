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
    THE TREE PANE (PRD, UX: "Tree view of the selected device with
    search-as-you-type on path and DESCRIPTION. Leaves show type, range and
    unit inline; non-writable leaves are shown but marked").

    The tree is the device's own, as the registry holds it (core::Instance
    hands it over as JSON and this parses it into oscquery::Tree, so a click
    on a leaf has the Node to bind with). A search term narrows the view to
    the paths (and descriptions) containing it, opened out. Clicking a leaf
    calls onLeafClicked with the node; what that means (bind the selected
    slot) is the editor's business. *Copy JSON* puts the tree as the device
    serves it on the clipboard, for a bug report to its maker (PRD, UX, 6d-1).
*/

#include <oscquery/tree/Tree.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

namespace oqa::editor
{
    class TreePane final : public juce::Component
    {
    public:
        TreePane();
        ~TreePane() override;

        /** A new tree (or none); rebuilds the view. */
        void setTree (std::optional<oscquery::Tree> tree, const juce::String& deviceName);

        /** What a dragged leaf's description starts with; the rest is its path (6d-2). */
        static constexpr const char* dragPrefix = "oqa-leaf:";

        /** True when the pane holds a tree with this hash, so a refresh can be skipped. */
        bool holds (const juce::String& treeHash) const noexcept { return shownHash == treeHash; }

        /** The hash of the tree shown, empty for none: what a bind writes into the chunk (Phase 5a). */
        juce::String currentHash() const noexcept { return shownHash; }

        /** A word in the title about what the next click does ("click a read-only leaf..."); empty for none. */
        void setPrompt (const juce::String& prompt);

        std::function<void (const oscquery::Node& node)> onLeafClicked;

        void resized() override;

    private:
        class Item;

        void rebuild();
        void updateTitle();

        std::optional<oscquery::Tree> tree;
        juce::String shownHash;
        juce::String deviceLabel;
        juce::String prompt;

        juce::Label title { {}, "No device selected" };
        juce::TextButton copyButton { "Copy JSON" };
        juce::TextEditor search;
        juce::TreeView view;
        std::unique_ptr<juce::TreeViewItem> root;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TreePane)
    };
}
