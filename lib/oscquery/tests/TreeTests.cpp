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

/*
    THE TREE MODEL, read from the generic fixture (devplan Phase 2: "Tree
    model ... multi-argument types split into per-argument views; a bindable
    predicate per the PRD; a treeHash() over bindable leaves").

    NO ADDRESS IS TYPED against the fixture: every node is found by what it is.
    The hand-built trees further down name two-letter paths, which are fixtures
    of this file, as the devplan allows a test to hold. Run under C and fr_FR:
    RANGE and VALUE are decimals.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"

#include <oscquery/tree/Tree.h>

#include <juce_cryptography/juce_cryptography.h>

#include <functional>
#include <string>
#include <vector>

using namespace oqa::oscquery;

namespace
{
    juce::var loadFixture()
    {
        return juce::JSON::parse (oqatest::fixture ("trees/generic-device.json"));
    }

    void collect (const juce::var& node, std::vector<juce::var>& out)
    {
        out.push_back (node);

        if (auto* contents = node.getProperty ("CONTENTS", {}).getDynamicObject())
            for (const auto& child : contents->getProperties())
                collect (child.value, out);
    }

    std::vector<juce::var> nodesOf (const juce::var& root)
    {
        std::vector<juce::var> nodes;
        collect (root, nodes);
        return nodes;
    }

    std::string pathOf (const juce::var& node)
    {
        return node.getProperty ("FULL_PATH", {}).toString().toStdString();
    }

    std::string typeOf (const juce::var& node)
    {
        return node.getProperty ("TYPE", {}).toString().toStdString();
    }

    int accessOf (const juce::var& node)
    {
        return static_cast<int> (node.getProperty ("ACCESS", 3));
    }

    bool isLeaf (const juce::var& node) { return node.hasProperty ("TYPE"); }
    bool isContainer (const juce::var& node) { return ! isLeaf (node) && node.hasProperty ("CONTENTS"); }

    juce::var first (const juce::var& root, const std::function<bool (const juce::var&)>& what,
                     const char* description)
    {
        for (const auto& node : nodesOf (root))
            if (what (node))
                return node;

        FAIL ("the fixture has no " << description);
        return {};
    }

    Tree parsed (const juce::var& root)
    {
        std::string error;
        auto tree = Tree::parse (root, error);
        INFO (error);
        REQUIRE (tree.has_value());
        return *tree;
    }

    //  A tree from JSON text written here, for the cases that need a shape the
    //  fixture does not have.
    Tree fromJson (const char* json)
    {
        std::string error;
        auto tree = Tree::parse (std::string_view (json), error);
        INFO (error);
        REQUIRE (tree.has_value());
        return *tree;
    }
}

//==============================================================================
TEST_CASE ("tree: every node of the fixture is found at its path, with its type and access")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    const auto fixture = loadFixture();
    const auto tree = parsed (fixture);

    const auto nodes = nodesOf (fixture);
    REQUIRE (nodes.size() > 10);
    CHECK (tree.nodeCount() == nodes.size());

    std::size_t leaves = 0;

    for (const auto& expected : nodes)
    {
        const auto path = pathOf (expected);
        INFO ("node " << path);

        const auto* node = tree.find (path);
        REQUIRE (node != nullptr);
        CHECK (node->fullPath == path);
        CHECK (node->isLeaf() == isLeaf (expected));
        CHECK (node->loaded);

        if (isLeaf (expected))
        {
            ++leaves;
            REQUIRE (node->type.has_value());
            CHECK (*node->type == typeOf (expected));
            CHECK (node->argumentCount() == typeOf (expected).size());
        }

        if (expected.hasProperty ("ACCESS"))
        {
            REQUIRE (node->access.has_value());
            CHECK (*node->access == accessOf (expected));
        }

        if (expected.hasProperty ("DESCRIPTION"))
        {
            REQUIRE (node->description.has_value());
            CHECK (*node->description == expected["DESCRIPTION"].toString().toStdString());
        }

        CHECK (node->critical == static_cast<bool> (expected.getProperty ("CRITICAL", false)));
    }

    CHECK (tree.leafCount() == leaves);
    CHECK (tree.leaves().size() == leaves);

    CHECK (tree.find ("/") == &tree.root());
    CHECK (tree.find ("") == &tree.root());
    CHECK (tree.find ("/no/such/path") == nullptr);
}

TEST_CASE ("tree: a multi-argument leaf is one argument per type tag, range and unit included")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    const auto fixture = loadFixture();
    const auto tree = parsed (fixture);

    const auto triple = first (fixture, [] (auto& n) { return typeOf (n) == "fff"; }, "fff leaf");
    const auto* node = tree.find (pathOf (triple));
    REQUIRE (node != nullptr);

    REQUIRE (node->argumentCount() == 3);
    REQUIRE (node->range.size() == 3);
    REQUIRE (node->hasValue);
    REQUIRE (node->value.size() == 3);

    const auto* expectedRanges = triple["RANGE"].getArray();
    const auto* expectedValues = triple["VALUE"].getArray();
    const auto* expectedUnits = triple["UNIT"].getArray();
    REQUIRE (expectedRanges != nullptr);
    REQUIRE (expectedValues != nullptr);

    for (std::size_t i = 0; i < 3; ++i)
    {
        INFO ("argument " << i);
        const auto argument = node->argument (i);

        CHECK (argument.tag == 'f');
        REQUIRE (argument.range.has_value());
        REQUIRE (argument.range->hasMinMax());

        const auto& expectedRange = expectedRanges->getReference (static_cast<int> (i));
        CHECK (*argument.range->min == doctest::Approx (static_cast<double> (expectedRange["MIN"])));
        CHECK (*argument.range->max == doctest::Approx (static_cast<double> (expectedRange["MAX"])));

        REQUIRE (argument.value.has_value());
        CHECK (argument.value->isFloat32());
        CHECK (argument.value->getFloat32()
                 == doctest::Approx (static_cast<double> (expectedValues->getReference (static_cast<int> (i)))));

        if (expectedUnits != nullptr)
            CHECK (argument.unit == expectedUnits->getReference (static_cast<int> (i)).toString().toStdString());
    }

    //  Past the last argument there is nothing, and no exception.
    const auto beyond = node->argument (3);
    CHECK_FALSE (beyond.range.has_value());
    CHECK_FALSE (beyond.value.has_value());
}

TEST_CASE ("tree: values are typed by TYPE, not by what JSON happened to carry")
{
    const auto fixture = loadFixture();
    const auto tree = parsed (fixture);

    const auto integer = first (fixture, [] (auto& n) { return typeOf (n) == "i" && n.hasProperty ("VALUE"); },
                                "i leaf with a value");
    const auto* i = tree.find (pathOf (integer));
    REQUIRE (i != nullptr);
    REQUIRE (i->value.size() == 1);
    CHECK (i->value[0].isInt32());
    CHECK (i->value[0].getInt32() == static_cast<int> (integer["VALUE"][0]));

    const auto toggle = first (fixture, [] (auto& n) { return typeOf (n) == "T" && n.hasProperty ("VALUE"); },
                               "T leaf with a value");
    const auto* t = tree.find (pathOf (toggle));
    REQUIRE (t != nullptr);
    REQUIRE (t->value.size() == 1);
    CHECK (t->value[0].isBool());
    CHECK (t->value[0].getBool() == static_cast<bool> (toggle["VALUE"][0]));

    const auto text = first (fixture, [] (auto& n) { return typeOf (n) == "s" && n.hasProperty ("VALUE"); },
                             "s leaf with a value");
    const auto* s = tree.find (pathOf (text));
    REQUIRE (s != nullptr);
    REQUIRE (s->value.size() == 1);
    CHECK (s->value[0].isString());
    CHECK (s->value[0].getString() == text["VALUE"][0].toString().toStdString());

    //  VALS of an i node are int32 too.
    const auto enumerated = first (fixture, [] (auto& n)
                                   {
                                       return typeOf (n) == "i" && n.hasProperty ("RANGE")
                                           && n["RANGE"][0].hasProperty ("VALS");
                                   }, "i leaf with VALS");
    const auto* e = tree.find (pathOf (enumerated));
    REQUIRE (e != nullptr);
    REQUIRE (e->range.size() == 1);
    REQUIRE (e->range[0].has_value());
    REQUIRE (e->range[0]->hasVals());
    CHECK_FALSE (e->range[0]->hasMinMax());

    for (const auto& value : e->range[0]->vals)
        CHECK (value.isInt32());

    //  And the conversion table on its own.
    CHECK (valueFromVar (juce::var (1.0), 'i') == osc::Value::int32 (1));
    CHECK (valueFromVar (juce::var (1), 'f') == osc::Value::float32 (1.0f));
    CHECK (valueFromVar (juce::var (0), 'T') == osc::Value::boolean (false));
    CHECK (valueFromVar (juce::var (5), 's') == osc::Value::string ("5"));
    CHECK (valueFromVar (juce::var (2.5), '\0') == osc::Value::float64 (2.5));
    CHECK (valueFromVar (juce::var ("x"), 'i') == osc::Value::string ("x"));   // the fallback, never zero

    const auto values = valuesFromVar (juce::var (0.5), "f");
    REQUIRE (values.size() == 1);
    CHECK (values[0] == osc::Value::float32 (0.5f));
}

TEST_CASE ("tree: bindable is the PRD's predicate")
{
    const auto fixture = loadFixture();
    const auto tree = parsed (fixture);

    const auto writeOnly = first (fixture, [] (auto& n) { return isLeaf (n) && accessOf (n) == 2; }, "write-only leaf");
    const auto* w = tree.find (pathOf (writeOnly));
    REQUIRE (w != nullptr);
    CHECK (w->bindableForSend());
    CHECK_FALSE (w->bindableForRecord());
    CHECK (w->bindable());

    const auto readOnly = first (fixture, [] (auto& n) { return isLeaf (n) && accessOf (n) == 1; }, "read-only leaf");
    const auto* r = tree.find (pathOf (readOnly));
    REQUIRE (r != nullptr);
    CHECK_FALSE (r->bindableForSend());
    CHECK (r->bindableForRecord());

    const auto both = first (fixture, [] (auto& n) { return isLeaf (n) && accessOf (n) == 3; }, "read-write leaf");
    const auto* b = tree.find (pathOf (both));
    REQUIRE (b != nullptr);
    CHECK (b->bindableForSend());
    CHECK (b->bindableForRecord());

    //  Containers never, whatever their ACCESS says.
    for (const auto& node : nodesOf (fixture))
        if (isContainer (node))
        {
            const auto* c = tree.find (pathOf (node));
            REQUIRE (c != nullptr);
            CHECK_FALSE (c->bindable());
        }

    //  A leaf with no RANGE has none, so a slot bound to it stays inactive (PRD).
    const auto unranged = first (fixture, [] (auto& n) { return typeOf (n) == "f" && ! n.hasProperty ("RANGE"); },
                                 "f leaf without RANGE");
    const auto* u = tree.find (pathOf (unranged));
    REQUIRE (u != nullptr);
    CHECK (u->range.empty());
    CHECK_FALSE (u->argument (0).range.has_value());

    //  ACCESS absent reads as 3, as the proposal says to.
    const auto implied = fromJson (R"({"FULL_PATH":"/","CONTENTS":{"x":{"FULL_PATH":"/x","TYPE":"f"}}})");
    const auto* x = implied.find ("/x");
    REQUIRE (x != nullptr);
    CHECK_FALSE (x->access.has_value());
    CHECK (x->effectiveAccess() == 3);
    CHECK (x->bindableForSend());
}

//==============================================================================
TEST_CASE ("tree: the hash is SHA-256 of the sorted FULL_PATH TAB TYPE lines of the leaves")
{
    /*  Spec draft C5 (proposed). Built by hand here, from the clause, so the
        test checks the definition and not the implementation against itself. */
    const auto tree = fromJson (R"({"FULL_PATH":"/","CONTENTS":{
        "b":{"FULL_PATH":"/b","TYPE":"i","ACCESS":1,"VALUE":[3]},
        "a":{"FULL_PATH":"/a","TYPE":"f","ACCESS":3,"RANGE":[{"MIN":0,"MAX":1}]},
        "c":{"FULL_PATH":"/c","ACCESS":0,"CONTENTS":{}}}})");

    const std::string lines = "/a\tf\n/b\ti\n";
    const auto expected = juce::SHA256 (lines.data(), lines.size()).toHexString().toStdString();

    CHECK (tree.treeHash() == expected);
    CHECK (tree.treeHash().size() == 64);
    CHECK (tree.treeHash().find_first_not_of ("0123456789abcdef") == std::string::npos);
}

TEST_CASE ("tree: the hash follows paths and types, and nothing else")
{
    const auto fixture = loadFixture();
    auto tree = parsed (fixture);
    const auto before = tree.treeHash();

    CHECK (parsed (fixture).treeHash() == before);

    //  A changed value or range is configuration, not a changed device.
    const auto ranged = first (fixture, [] (auto& n) { return typeOf (n) == "f" && n.hasProperty ("RANGE"); },
                               "ranged f leaf");
    {
        auto* node = tree.find (pathOf (ranged));
        REQUIRE (node != nullptr);
        node->value = { osc::Value::float32 (99.0f) };
        node->range[0]->max = 1000.0;
        node->description = "something else";
        node->access = 1;
    }
    CHECK (tree.treeHash() == before);

    //  A retyped leaf is a changed device.
    {
        auto* node = tree.find (pathOf (ranged));
        node->type = "i";
    }
    const auto retyped = tree.treeHash();
    CHECK (retyped != before);

    //  And so is a missing one.
    REQUIRE (tree.remove (pathOf (ranged)));
    CHECK (tree.treeHash() != before);
    CHECK (tree.treeHash() != retyped);

    //  A container going is nothing, as long as its leaves were already gone.
    auto emptyContainer = parsed (fixture);
    const auto container = first (fixture, [] (auto& n) { return isContainer (n) && pathOf (n) != "/"; }, "container");
    (void) container;
    CHECK (emptyContainer.treeHash() == before);
}

//==============================================================================
TEST_CASE ("tree: merge replaces a subtree in place, and needs its parent")
{
    const auto fixture = loadFixture();
    auto tree = parsed (fixture);
    const auto count = tree.nodeCount();

    const auto leaf = first (fixture, [] (auto& n) { return isLeaf (n) && n.hasProperty ("DESCRIPTION"); },
                             "described leaf");
    auto changed = leaf.clone();
    changed.getDynamicObject()->setProperty ("DESCRIPTION", "re-read after PATH_CHANGED");

    std::string error;
    REQUIRE (tree.merge (changed, pathOf (leaf), error));

    const auto* node = tree.find (pathOf (leaf));
    REQUIRE (node != nullptr);
    CHECK (*node->description == "re-read after PATH_CHANGED");
    CHECK (tree.nodeCount() == count);

    //  A whole container, children included.
    const auto container = first (fixture, [] (auto& n) { return isContainer (n) && pathOf (n) != "/"; }, "container");
    REQUIRE (tree.merge (container, pathOf (container), error));
    CHECK (tree.nodeCount() == count);

    //  Without a parent there is nowhere to put it.
    Node orphan;
    orphan.fullPath = "/nowhere/at/all";
    orphan.type = "f";
    CHECK_FALSE (tree.merge (orphan));
    CHECK (tree.nodeCount() == count);

    //  Something that is not a node is refused with a reason.
    CHECK_FALSE (tree.merge (juce::var ("text"), "/x", error));
    CHECK_FALSE (error.empty());

    //  Merging at "/" replaces the whole tree.
    Node fresh;
    fresh.fullPath = "/";
    REQUIRE (tree.merge (fresh));
    CHECK (tree.nodeCount() == 1);
}

TEST_CASE ("tree: remove and rename do what the notifications say, and refuse the rest")
{
    const auto fixture = loadFixture();
    auto tree = parsed (fixture);
    const auto count = tree.nodeCount();

    const auto leaf = first (fixture, [] (auto& n) { return isLeaf (n); }, "leaf");
    const auto leafPath = pathOf (leaf);
    const auto parentPath = parentPathOf (leafPath);

    //  Rename a leaf beside itself.
    const auto renamed = parentPath == "/" ? "/renamed" : parentPath + "/renamed";
    REQUIRE (tree.rename (leafPath, renamed));
    CHECK (tree.find (leafPath) == nullptr);
    REQUIRE (tree.find (renamed) != nullptr);
    CHECK (tree.find (renamed)->fullPath == renamed);
    CHECK (tree.find (renamed)->type == std::optional<std::string> (typeOf (leaf)));
    CHECK (tree.nodeCount() == count);

    //  Remove it, before its parent moves below.
    CHECK_FALSE (tree.remove ("/"));
    CHECK_FALSE (tree.remove ("/no/such/node"));
    REQUIRE (tree.remove (renamed));
    CHECK (tree.find (renamed) == nullptr);
    CHECK (tree.nodeCount() == count - 1);

    //  Rename a container: every descendant's FULL_PATH follows.
    const auto container = first (fixture, [] (auto& n)
                                  {
                                      return isContainer (n) && pathOf (n) != "/"
                                          && n["CONTENTS"].getDynamicObject()->getProperties().size() > 0;
                                  }, "container with children");
    const auto containerPath = pathOf (container);
    const auto movedTo = containerPath + "_moved";
    REQUIRE (tree.rename (containerPath, movedTo));
    CHECK (tree.find (containerPath) == nullptr);

    const auto* moved = tree.find (movedTo);
    REQUIRE (moved != nullptr);

    std::vector<std::string> paths;
    tree.forEach ([&] (const Node& n) { paths.push_back (n.fullPath); });

    for (const auto& path : paths)
        CHECK (path.rfind (containerPath + "/", 0) != 0);

    for (const auto& child : moved->children)
        CHECK (child.second.fullPath == movedTo + "/" + child.first);

    //  A node that exists and is not inside the moved one, for the "target exists" refusal.
    const auto other = first (fixture, [&] (auto& n)
                              {
                                  const auto p = pathOf (n);
                                  return p != "/" && p != containerPath && p.rfind (containerPath + "/", 0) != 0
                                      && containerPath.rfind (p + "/", 0) != 0 && p != leafPath;
                              }, "node outside the moved container");

    //  Refusals.
    CHECK_FALSE (tree.rename ("/", "/elsewhere"));
    CHECK_FALSE (tree.rename (movedTo, movedTo));
    CHECK_FALSE (tree.rename (movedTo, movedTo + "/inside"));         // into itself
    CHECK_FALSE (tree.rename (movedTo, pathOf (other)));              // target exists
    CHECK_FALSE (tree.rename (movedTo, "/no/such/parent/x"));
    CHECK_FALSE (tree.rename ("/no/such/node", "/anywhere"));
    CHECK (tree.nodeCount() == count - 1);

    //  Remove the moved container and everything under it.
    std::size_t movedCount = 0;
    std::function<void (const Node&)> walk = [&] (const Node& node) { ++movedCount; for (auto& c : node.children) walk (c.second); };
    walk (*moved);

    REQUIRE (tree.remove (movedTo));
    CHECK (tree.find (movedTo) == nullptr);
    CHECK (tree.nodeCount() == count - 1 - movedCount);
}

//==============================================================================
TEST_CASE ("tree: a container the device sent without CONTENTS is not loaded until a subtree arrives")
{
    auto tree = fromJson (R"({"FULL_PATH":"/","CONTENTS":{
        "shallow":{"FULL_PATH":"/shallow","ACCESS":0},
        "empty":{"FULL_PATH":"/empty","ACCESS":0,"CONTENTS":{}},
        "leaf":{"FULL_PATH":"/leaf","TYPE":"i"}}})");

    REQUIRE (tree.find ("/shallow") != nullptr);
    CHECK_FALSE (tree.find ("/shallow")->loaded);
    CHECK (tree.find ("/shallow")->isContainer());

    CHECK (tree.find ("/empty")->loaded);
    CHECK (tree.find ("/leaf")->loaded);

    std::string error;
    REQUIRE (tree.merge (juce::JSON::parse (R"({"FULL_PATH":"/shallow","ACCESS":0,"CONTENTS":{
        "x":{"FULL_PATH":"/shallow/x","TYPE":"f"}}})"), "/shallow", error));

    CHECK (tree.find ("/shallow")->loaded);
    REQUIRE (tree.find ("/shallow/x") != nullptr);
    CHECK (tree.leafCount() == 2);
}

TEST_CASE ("tree: what the model does not read, it keeps; FULL_PATH absent is derived")
{
    INFO ("locale in effect: " << std::string (oqatest::appliedLocaleName()));

    auto tree = fromJson (R"({"FULL_PATH":"/","CONTENTS":{
        "a":{"TYPE":"f","ACCESS":3,"VALUE":[0.25],"RANGE":[{"MIN":-180.5,"MAX":180.5}],
             "TAGS":["one","two"],"EXTENDED_TYPE":["position.x"],"CONTENTS":{}}}})");

    const auto* a = tree.find ("/a");
    REQUIRE (a != nullptr);
    CHECK (a->fullPath == "/a");
    CHECK (a->extras.size() == 2);
    CHECK (a->extras.contains ("TAGS"));
    CHECK (a->extras.contains ("EXTENDED_TYPE"));

    REQUIRE (a->range.size() == 1);
    REQUIRE (a->range[0].has_value());
    CHECK (*a->range[0]->min == -180.5);
    CHECK (*a->range[0]->max == 180.5);
    REQUIRE (a->value.size() == 1);
    CHECK (a->value[0] == osc::Value::float32 (0.25f));

    //  And back out, extras included, reading the same.
    std::string error;
    const auto again = Tree::parse (tree.toVar(), error);
    REQUIRE (again.has_value());
    CHECK (again->nodeCount() == tree.nodeCount());
    CHECK (again->treeHash() == tree.treeHash());

    const auto* b = again->find ("/a");
    REQUIRE (b != nullptr);
    CHECK (b->extras.size() == 2);
    CHECK (b->range == a->range);
    CHECK (b->value == a->value);
    CHECK (b->access == a->access);

    //  JSON written under this locale still has a full stop in it.
    const auto text = juce::JSON::toString (tree.toVar(), true).toStdString();
    CHECK (text.find ("180.5") != std::string::npos);
    CHECK (text.find ("180,5") == std::string::npos);
}

TEST_CASE ("tree: what is not a tree is refused with a reason")
{
    std::string error;

    CHECK_FALSE (Tree::parse (juce::var (42), error).has_value());
    CHECK_FALSE (error.empty());

    error.clear();
    CHECK_FALSE (Tree::parse (std::string_view ("not json at all"), error).has_value());
    CHECK_FALSE (error.empty());

    error.clear();
    CHECK_FALSE (Tree::parse (std::string_view ("[1, 2, 3]"), error).has_value());
    CHECK_FALSE (error.empty());

    //  A child that is not an object takes the tree with it.
    error.clear();
    CHECK_FALSE (Tree::parse (std::string_view (R"({"FULL_PATH":"/","CONTENTS":{"x":5}})"), error).has_value());
    CHECK (error.find ("/x") != std::string::npos);

    //  An empty object is a loaded root with nothing under it.
    const auto empty = Tree::parse (std::string_view ("{}"), error);
    REQUIRE (empty.has_value());
    CHECK (empty->root().fullPath == "/");
    CHECK (empty->nodeCount() == 1);
    CHECK (empty->treeHash() == juce::SHA256 ("", 0).toHexString().toStdString());

    //  Path helpers.
    CHECK (parentPathOf ("/a/b") == "/a");
    CHECK (parentPathOf ("/a") == "/");
    CHECK (parentPathOf ("/") == "");
    CHECK (parentPathOf ("/a/b/") == "/a");
    CHECK (lastSegmentOf ("/a/b") == "b");
    CHECK (lastSegmentOf ("/a/b/") == "b");
    CHECK (lastSegmentOf ("/") == "");
}
