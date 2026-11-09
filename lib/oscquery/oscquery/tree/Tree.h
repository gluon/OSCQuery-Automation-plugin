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
    A DEVICE'S TREE, as `GET /` returns it, and the operations a client needs
    on it afterwards: find a path, replace a subtree that `GET /<path>` or a
    PATH_CHANGED re-read brought back, drop one that PATH_REMOVED named, move
    one that PATH_RENAMED named, count it, and hash it.

    THE TREE HASH (spec draft C5, marked (proposed) there): SHA-256, lowercase
    hex, of one line `FULL_PATH TAB TYPE LF` per leaf, the lines sorted by byte
    value. Every leaf with a TYPE enters it, whatever its ACCESS, so that two
    clients the device treats differently (C6) compute the same hash; RANGE,
    VALUE, DESCRIPTION and UNIT stay out, so that a range following
    configuration does not read as a changed device. The plugin's state chunk
    needs a tree hash whether or not the spec's reviewers keep this definition;
    if they change it, this function changes with it.

    NOT THREAD-SAFE BY ITSELF. Device owns one and hands out copies.
*/

#include <oscquery/tree/Node.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oqa::oscquery
{
    class Tree
    {
    public:
        /** An empty tree: a loaded root with no children. */
        Tree();

        /*  A tree from a `GET /` body (or any node, which becomes the root).
            nullopt, with `error`, when the value is not a JSON object. A root
            without FULL_PATH is "/". */
        static std::optional<Tree> parse (const juce::var& root, std::string& error);
        static std::optional<Tree> parse (std::string_view json, std::string& error);

        /*  One node and its descendants from JSON. `pathIfAbsent` names it when
            the device left FULL_PATH out, as some do below the root. */
        static std::optional<Node> parseNode (const juce::var& object, const std::string& pathIfAbsent,
                                              std::string& error);

        //==============================================================================
        const Node& root() const noexcept { return rootNode; }
        Node& root() noexcept             { return rootNode; }

        /** The node at `path` ("/" for the root), or null. */
        const Node* find (std::string_view path) const;
        Node* find (std::string_view path);

        /*  Replaces the node at `node.fullPath` with `node`, children included;
            the root when the path is "/". False when the parent does not exist.
            A parent whose CONTENTS were not loaded becomes loaded by this. */
        bool merge (Node node);

        /** merge() of a parsed subtree, named by its FULL_PATH or `pathIfAbsent`. */
        bool merge (const juce::var& subtree, const std::string& pathIfAbsent, std::string& error);

        /** Drops the node at `path` and its subtree. False if absent, or the root. */
        bool remove (std::string_view path);

        /*  Moves the node at `oldPath` to `newPath`, rewriting FULL_PATH down
            its subtree. False if `oldPath` is absent or the root, if `newPath`
            exists, or if its parent does not. */
        bool rename (std::string_view oldPath, std::string_view newPath);

        //==============================================================================
        /** Every node, the root included. */
        std::size_t nodeCount() const;

        /** Every node with a TYPE. */
        std::size_t leafCount() const;

        /** Every node with a TYPE, in path order. Pointers into this tree. */
        std::vector<const Node*> leaves() const;

        /** Every node, depth first, parents before children. */
        void forEach (const std::function<void (const Node&)>& visit) const;

        /** The C5 hash described above. */
        std::string treeHash() const;

        /** The tree as JSON in `GET /` shape, extras included. */
        juce::var toVar() const;

        /** One node and its descendants in the same shape (a subtree GET, C7). */
        static juce::var toVar (const Node& node);

    private:
        Node rootNode;
    };

    /** Parent path of `path`: "/a/b" -> "/a", "/a" -> "/", "/" -> "". */
    std::string parentPathOf (std::string_view path);

    /** Last segment of `path`: "/a/b" -> "b", "/" -> "". */
    std::string lastSegmentOf (std::string_view path);
}
