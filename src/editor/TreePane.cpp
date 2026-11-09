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

#include "TreePane.h"

#include <oscquery/osc/Value.h>

namespace oqa::editor
{
    namespace
    {
        bool matches (const oscquery::Node& node, const juce::String& needle)
        {
            if (needle.isEmpty())
                return true;

            if (juce::String (node.fullPath).containsIgnoreCase (needle))
                return true;

            return node.description.has_value() && juce::String (*node.description).containsIgnoreCase (needle);
        }

        /** True when the node or anything under it matches. */
        bool subtreeMatches (const oscquery::Node& node, const juce::String& needle)
        {
            if (matches (node, needle))
                return true;

            for (const auto& [name, child] : node.children)
                if (subtreeMatches (child, needle))
                    return true;

            return false;
        }

        juce::String rangeText (const oscquery::Node& node)
        {
            juce::String out;

            for (std::size_t i = 0; i < node.argumentCount(); ++i)
            {
                const auto a = node.argument (i);

                if (i > 0)
                    out << "  ";

                if (a.range.has_value() && a.range->hasMinMax())
                    out << "[" << juce::String (*a.range->min) << " .. " << juce::String (*a.range->max) << "]";
                else if (a.range.has_value() && a.range->hasVals())
                    out << "{" << juce::String (static_cast<int> (a.range->vals.size())) << " values}";
                else if (a.tag == 'T' || a.tag == 'F')
                    out << "toggle";
                else
                    out << "no range";

                if (! a.unit.empty())
                    out << " " << juce::String (a.unit);
            }

            return out;
        }
    }

    //==============================================================================
    class TreePane::Item final : public juce::TreeViewItem
    {
    public:
        Item (TreePane& owner, const oscquery::Node& node, const juce::String& needle)
            : pane (owner), node (node)
        {
            for (const auto& [name, child] : node.children)
                if (subtreeMatches (child, needle))
                    addSubItem (new Item (owner, child, needle));

            setOpen (needle.isNotEmpty() || node.fullPath == "/" || node.fullPath.find ('/', 1) == std::string::npos);
        }

        bool mightContainSubItems() override { return node.isContainer(); }
        juce::String getUniqueName() const override { return juce::String (node.fullPath); }
        int getItemHeight() const override { return 22; }

        void paintItem (juce::Graphics& g, int width, int height) override
        {
            const auto& colours = getOwnerView()->getLookAndFeel();
            const bool writable = node.bindableForSend();
            const bool bindable = node.isLeaf() && (writable || node.bindableForRecord());

            if (isSelected())
                g.fillAll (colours.findColour (juce::TreeView::backgroundColourId).brighter (0.2f));

            g.setColour (colours.findColour (juce::ListBox::textColourId).withAlpha (node.isLeaf() && ! bindable ? 0.5f : 1.0f));
            g.setFont (juce::FontOptions (13.0f, node.isContainer() ? juce::Font::bold : juce::Font::plain));

            const auto name = juce::String (node.name().empty() ? std::string ("/") : node.name());
            g.drawText (name, 4, 0, width / 3, height, juce::Justification::centredLeft, true);

            if (node.isLeaf())
            {
                g.setFont (juce::FontOptions (12.0f));
                juce::String info = juce::String (*node.type) + "   " + rangeText (node);

                if (! writable)
                    info += node.bindableForRecord() ? "   read-only" : "   no access";

                if (node.critical)
                    info += "   critical";

                g.drawText (info, width / 3 + 4, 0, width * 2 / 3 - 8, height, juce::Justification::centredLeft, true);
            }
        }

        void itemClicked (const juce::MouseEvent&) override
        {
            if (node.isLeaf() && pane.onLeafClicked != nullptr)
                pane.onLeafClicked (node);
        }

    private:
        TreePane& pane;
        const oscquery::Node& node;
    };

    //==============================================================================
    TreePane::TreePane()
    {
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        addAndMakeVisible (title);

        search.setTextToShowWhenEmpty ("search path or description", juce::Colours::grey);
        search.onTextChange = [this] { rebuild(); };
        addAndMakeVisible (search);

        view.setDefaultOpenness (false);
        view.setRootItemVisible (false);
        addAndMakeVisible (view);
    }

    TreePane::~TreePane()
    {
        view.setRootItem (nullptr);
    }

    void TreePane::resized()
    {
        auto area = getLocalBounds();
        title.setBounds (area.removeFromTop (24));
        search.setBounds (area.removeFromTop (26).reduced (0, 2));
        view.setBounds (area);
    }

    void TreePane::setTree (std::optional<oscquery::Tree> newTree, const juce::String& deviceName)
    {
        tree = std::move (newTree);
        shownHash = tree.has_value() ? juce::String (tree->treeHash()) : juce::String();
        deviceLabel = deviceName;
        updateTitle();
        rebuild();
    }

    void TreePane::setPrompt (const juce::String& newPrompt)
    {
        prompt = newPrompt;
        updateTitle();
    }

    void TreePane::updateTitle()
    {
        auto text = tree.has_value() ? deviceLabel + ": " + juce::String (static_cast<int> (tree->leafCount())) + " leaves"
                                     : (deviceLabel.isEmpty() ? juce::String ("No device selected") : deviceLabel + ": no tree yet");

        if (prompt.isNotEmpty())
            text += "   -   " + prompt;

        title.setText (text, juce::dontSendNotification);
    }

    void TreePane::rebuild()
    {
        view.setRootItem (nullptr);
        root.reset();

        if (! tree.has_value())
            return;

        root = std::make_unique<Item> (*this, tree->root(), search.getText().trim());
        view.setRootItem (root.get());
    }
}
