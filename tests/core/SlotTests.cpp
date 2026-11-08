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
    THE SLOT MODEL (devplan Phase 3a: "the PRD's table as a struct; RANGE
    missing -> slot inactive until min/max entered; T/F as a toggle; i with
    rounding; per-argument slots for fff").

    NO ADDRESS IS TYPED: every node is found in the generic fixture by what it
    is (its TYPE, its ACCESS, whether it has a RANGE), never by its path. What
    a test then reads back from the slot is compared with the node it bound.
*/

#include <doctest/doctest.h>

#include "TestSupport.h"

#include <core/Slot.h>

#include <oscquery/tree/Tree.h>

#include <functional>
#include <string>

using namespace oqa::core;
using namespace oqa::oscquery;

namespace
{
    Tree fixtureTree()
    {
        std::string error;
        auto tree = Tree::parse (juce::JSON::parse (oqatest::fixture ("trees/generic-device.json")), error);
        REQUIRE_MESSAGE (tree.has_value(), error);
        return *tree;
    }

    /** The first leaf, in path order, that satisfies the predicate. */
    const Node& leafWhere (const Tree& tree, const std::function<bool (const Node&)>& predicate)
    {
        for (const auto* leaf : tree.leaves())
            if (predicate (*leaf))
                return *leaf;

        FAIL ("the fixture has no such leaf");
        return tree.root();
    }

    bool hasMinMax (const Node& n, std::size_t arg = 0)
    {
        const auto view = n.argument (arg);
        return view.range.has_value() && view.range->hasMinMax();
    }

    bool hasValsOnly (const Node& n)
    {
        const auto view = n.argument (0);
        return view.range.has_value() && view.range->hasVals() && ! view.range->hasMinMax();
    }
}

//==============================================================================
TEST_CASE ("direction names round-trip through the chunk's spelling")
{
    for (auto d : { Direction::send, Direction::receive, Direction::both })
        CHECK (directionFromName (directionName (d)) == d);

    CHECK_FALSE (directionFromName ("sideways").has_value());
}

TEST_CASE ("v1 binds f, i, T and F and nothing else")
{
    for (char tag : { 'f', 'i', 'T', 'F' })
        CHECK (isBindableTag (tag));

    for (char tag : { 's', 'd', 'h', 'b', 'N', 'I', 't' })
        CHECK_FALSE (isBindableTag (tag));
}

TEST_CASE ("a fresh table has N unbound, inactive slots numbered by host parameter ID")
{
    const SlotTable table (32);

    CHECK (table.size() == 32);
    CHECK (table.boundCount() == 0);

    for (int i = 0; i < table.size(); ++i)
    {
        CHECK (table[i].hostParamId == i);
        CHECK_FALSE (table[i].bound());
        CHECK_FALSE (table[i].active());
        CHECK (table[i].displayName.empty());
    }
}

//==============================================================================
TEST_CASE ("binding a ranged float takes type, range, unit and a name from the node")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n)
    {
        return n.type == "f" && n.bindableForSend() && hasMinMax (n) && ! n.unit.empty() && ! n.unit[0].empty();
    });

    Slot slot;
    std::string why;
    REQUIRE_MESSAGE (slot.bind (node, 0, 2, why), why);

    CHECK (slot.bound());
    CHECK (slot.active());
    CHECK (slot.deviceIndex == 2);
    CHECK (slot.path == node.fullPath);
    CHECK (slot.argIndex == 0);
    CHECK (slot.type == 'f');
    CHECK (slot.unit == node.unit[0]);
    CHECK (slot.rangeMin == node.argument (0).range->min);
    CHECK (slot.rangeMax == node.argument (0).range->max);
    CHECK_FALSE (slot.rangeOverridden);
    CHECK (slot.direction == Direction::send);
    CHECK (slot.scaling == Scaling::linear);

    /*  The default name is the path without its slash; a single argument gets no index. */
    CHECK (slot.displayName == node.fullPath.substr (1));

    /*  0–1 lands on the device's range, and the typed value is a float32. */
    CHECK (slot.toDevice (0.0f) == doctest::Approx (*slot.rangeMin));
    CHECK (slot.toDevice (1.0f) == doctest::Approx (*slot.rangeMax));

    const auto v = slot.deviceValue (1.0f);
    CHECK (v.isFloat32());
    CHECK (v.asDouble() == doctest::Approx (*slot.rangeMax));

    CHECK (slot.toHost (*slot.rangeMin) == doctest::Approx (0.0f));
    CHECK (slot.toHost (*slot.rangeMax) == doctest::Approx (1.0f));
    CHECK (slot.hostValue (v) == doctest::Approx (1.0f));
}

TEST_CASE ("a float without RANGE is bound but inactive until min and max are entered")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n)
    {
        return n.type == "f" && n.bindableForSend() && ! n.argument (0).range.has_value();
    });

    Slot slot;
    std::string why;
    REQUIRE_MESSAGE (slot.bind (node, 0, 0, why), why);

    CHECK (slot.bound());
    CHECK_FALSE (slot.rangeKnown());
    CHECK_FALSE (slot.active());
    CHECK_FALSE (slot.rangeMin.has_value());

    slot.setRange (-12.0, 12.0);

    CHECK (slot.active());
    CHECK (slot.rangeOverridden);
    CHECK (slot.toDevice (0.5f) == doctest::Approx (0.0));
    CHECK (slot.toDevice (1.0f) == doctest::Approx (12.0));
}

TEST_CASE ("an integer rounds, is typed int32, and takes its range from MIN/MAX")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n)
    {
        return n.type == "i" && n.bindableForSend() && n.bindableForRecord() && hasMinMax (n);
    });

    Slot slot;
    std::string why;
    REQUIRE_MESSAGE (slot.bind (node, 0, 0, why), why);

    CHECK (slot.isInteger());
    CHECK (slot.active());

    const double min = *slot.rangeMin;
    const double max = *slot.rangeMax;

    CHECK (slot.toDevice (0.0f) == min);
    CHECK (slot.toDevice (1.0f) == max);

    /*  Somewhere in the middle the value is whole. */
    const double mid = slot.toDevice (0.3f);
    CHECK (mid == std::round (mid));

    const auto v = slot.deviceValue (0.3f);
    CHECK (v.isInt32());
    CHECK (v.asDouble() == mid);

    /*  And back: the integer's own position on the travel. */
    CHECK (slot.hostValue (v) == doctest::Approx (slot.toHost (mid)));
}

TEST_CASE ("an integer with VALS and no MIN/MAX spans its first and last value")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n)
    {
        return n.type == "i" && hasValsOnly (n);
    });

    Slot slot;
    std::string why;
    REQUIRE_MESSAGE (slot.bind (node, 0, 0, why), why);

    /*  argument() returns a view by value: copy it before reading into it. */
    const auto view = node.argument (0);
    const auto& vals = view.range->vals;
    REQUIRE (vals.size() >= 2);

    CHECK (slot.active());
    CHECK (slot.rangeMin == vals.front().asDouble());
    CHECK (slot.rangeMax == vals.back().asDouble());
    CHECK (slot.toDevice (1.0f) == vals.back().asDouble());
}

TEST_CASE ("a T or F leaf is a toggle: active without a range, true above a half")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n)
    {
        return (n.type == "T" || n.type == "F") && n.bindableForSend() && ! n.critical;
    });

    Slot slot;
    std::string why;
    REQUIRE_MESSAGE (slot.bind (node, 0, 0, why), why);

    CHECK (slot.isToggle());
    CHECK (slot.rangeKnown());
    CHECK (slot.active());
    CHECK_FALSE (slot.rangeMin.has_value());
    CHECK_FALSE (slot.critical);

    CHECK (slot.toDevice (0.49f) == 0.0);
    CHECK (slot.toDevice (0.5f) == 1.0);

    const auto on = slot.deviceValue (0.75f);
    const auto off = slot.deviceValue (0.25f);
    CHECK (on.isBool());
    CHECK (on.getBool());
    CHECK (off.isBool());
    CHECK_FALSE (off.getBool());

    CHECK (slot.hostValue (on) == 1.0f);
    CHECK (slot.hostValue (off) == 0.0f);
    CHECK_FALSE (slot.hostValue (oqa::oscquery::osc::Value::string ("on")).has_value());
}

TEST_CASE ("a multi-argument node binds one slot per argument, each with its own range and name")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n) { return n.argumentCount() == 3; });

    SlotTable table (8);
    std::string why;
    REQUIRE_MESSAGE (table.bindAll (5, node, 1, why), why);

    CHECK (table.boundCount() == 3);

    for (int a = 0; a < 3; ++a)
    {
        const auto& slot = table[5 + a];
        const auto view = node.argument (static_cast<std::size_t> (a));

        CHECK (slot.path == node.fullPath);
        CHECK (slot.argIndex == a);
        CHECK (slot.type == view.tag);
        CHECK (slot.rangeMin == view.range->min);
        CHECK (slot.rangeMax == view.range->max);
        CHECK (slot.displayName == node.fullPath.substr (1) + "[" + std::to_string (a) + "]");
        CHECK (slot.active());
    }

    /*  The third argument's range differs from the first two in the fixture,
        which is the point of per-argument ranges. */
    CHECK (table[7].rangeMax != table[5].rangeMax);

    /*  groupOf finds the three from any of them, in argument order. */
    const std::vector<int> expected { 5, 6, 7 };
    CHECK (table.groupOf (5) == expected);
    CHECK (table.groupOf (7) == expected);
    CHECK (table.groupOf (0).empty());
}

TEST_CASE ("bindAll is all or nothing when the table runs out of slots")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n) { return n.argumentCount() == 3; });

    SlotTable table (8);
    std::string why;
    CHECK_FALSE (table.bindAll (6, node, 0, why));
    CHECK_FALSE (why.empty());
    CHECK (table.boundCount() == 0);
}

TEST_CASE ("a write-only leaf binds for send and never for receive")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n)
    {
        return n.bindableForSend() && ! n.bindableForRecord();
    });

    Slot slot;
    std::string why;
    REQUIRE_MESSAGE (slot.bind (node, 0, 0, why), why);
    CHECK (slot.direction == Direction::send);
    CHECK (slot.sends());
    CHECK_FALSE (slot.receives());
}

TEST_CASE ("a read-only leaf binds with direction receive")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n)
    {
        return n.type == "f" && n.bindableForRecord() && ! n.bindableForSend();
    });

    Slot slot;
    std::string why;
    REQUIRE_MESSAGE (slot.bind (node, 0, 0, why), why);
    CHECK (slot.direction == Direction::receive);
    CHECK (slot.receives());
    CHECK_FALSE (slot.sends());
}

TEST_CASE ("what cannot bind says why: a container, a string, a missing argument")
{
    const auto tree = fixtureTree();
    Slot slot;
    std::string why;

    CHECK_FALSE (slot.bind (tree.root(), 0, 0, why));
    CHECK (why.find ("container") != std::string::npos);
    CHECK_FALSE (slot.bound());

    const auto& text = leafWhere (tree, [] (const Node& n) { return n.type == "s"; });
    CHECK_FALSE (slot.bind (text, 0, 0, why));
    CHECK (why.find ("'s'") != std::string::npos);
    CHECK_FALSE (slot.bound());

    const auto& one = leafWhere (tree, [] (const Node& n) { return n.type == "f"; });
    CHECK_FALSE (slot.bind (one, 1, 0, why));
    CHECK (why.find ("argument 1") != std::string::npos);
    CHECK_FALSE (slot.bound());
}

TEST_CASE ("unbind clears everything but the host parameter ID")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n) { return n.type == "f" && hasMinMax (n); });

    SlotTable table (4);
    std::string why;
    REQUIRE (table[3].bind (node, 0, 0, why));
    table[3].scaling = Scaling::logarithmic;

    table[3].unbind();

    CHECK (table[3].hostParamId == 3);
    CHECK_FALSE (table[3].bound());
    CHECK_FALSE (table[3].active());
    CHECK (table[3].path.empty());
    CHECK (table[3].displayName.empty());
    CHECK_FALSE (table[3].rangeMin.has_value());
    CHECK (table[3].deviceIndex == -1);

    /*  The user's curve is a preference and survives; it is not the device's. */
    CHECK (table[3].scaling == Scaling::logarithmic);
}

TEST_CASE ("the chosen curve applies to a bound slot's range")
{
    /*  No range in the fixture excludes zero, so the log curve is what a user
        asks for after entering a range of their own: the no-RANGE float, given
        three decades by hand. */
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n)
    {
        return n.type == "f" && n.bindableForSend() && ! n.argument (0).range.has_value();
    });

    Slot slot;
    std::string why;
    REQUIRE_MESSAGE (slot.bind (node, 0, 0, why), why);
    slot.setRange (20.0, 20000.0);

    const double linearMid = slot.toDevice (0.5f);
    CHECK (linearMid == doctest::Approx (10010.0));

    slot.scaling = Scaling::logarithmic;
    CHECK (slot.mapping().effectiveScaling() == Scaling::logarithmic);
    CHECK (slot.toDevice (0.5f) == doctest::Approx (632.455532));
    CHECK (slot.toDevice (0.0f) == doctest::Approx (20.0));
    CHECK (slot.toDevice (1.0f) == doctest::Approx (20000.0));

    /*  On a range through zero the same choice is linear, and the mapping says so. */
    slot.setRange (-1.0, 1.0);
    CHECK (slot.scaling == Scaling::logarithmic);
    CHECK (slot.mapping().effectiveScaling() == Scaling::linear);
    CHECK (slot.toDevice (0.5f) == doctest::Approx (0.0));
}

TEST_CASE ("a CRITICAL leaf marks its slot critical, and the chunk carries it")
{
    const auto tree = fixtureTree();
    const auto& node = leafWhere (tree, [] (const Node& n) { return n.critical && n.bindableForSend(); });

    Slot slot;
    std::string why;
    REQUIRE_MESSAGE (slot.bind (node, 0, 0, why), why);
    CHECK (slot.critical);

    slot.unbind();
    CHECK_FALSE (slot.critical);
}
