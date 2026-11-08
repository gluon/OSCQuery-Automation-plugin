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
    ONE NODE OF A DEVICE'S TREE, as the proposal describes it and the PRD reads
    it (Discovery, "Reading the tree"): FULL_PATH, TYPE, ACCESS, RANGE, UNIT,
    DESCRIPTION, CLIPMODE, CRITICAL, VALUE and CONTENTS. Anything else the
    device sent is kept in `extras`, untouched, so a tree re-serialised loses
    nothing it did not understand.

    THE LIBRARY KNOWS NO NAMESPACE SEMANTICS (PRD, non-goals): a node is a path,
    a type and a range, never a meaning. Nothing here reads a path.

    PER-ARGUMENT VIEWS. A node of TYPE "fff" is three arguments, and the PRD
    binds each to its own slot. `argument (i)` gathers the i-th type tag, RANGE
    entry, UNIT, CLIPMODE and VALUE into one view, which is what a slot binds.

    BINDABLE, per the PRD: a leaf (TYPE present) whose ACCESS includes write is
    bindable for send, one whose ACCESS includes read is bindable for record,
    and a container is never bindable. ACCESS absent reads as 3, which is what
    the proposal tells clients to assume (spec draft C4 says so, and why that is
    a reason for devices to state it).

    VALUES ARE osc::Value, typed by the node's TYPE: JSON has three scalar
    types and OSC has ten, so a `1` in a reply is an int32 on an `i` node and a
    float32 on an `f` node, and compares equal to what the wire carries.
*/

#include <oscquery/osc/Value.h>

#include <juce_core/juce_core.h>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace oqa::oscquery
{
    /** One RANGE entry: MIN and MAX, or VALS, or (rarely) both. */
    struct Range
    {
        std::optional<double> min;
        std::optional<double> max;
        osc::Values vals;

        bool hasMinMax() const noexcept { return min.has_value() && max.has_value(); }
        bool hasVals() const noexcept   { return ! vals.empty(); }
        bool empty() const noexcept     { return ! hasMinMax() && ! hasVals(); }

        bool operator== (const Range& other) const;
        bool operator!= (const Range& other) const { return ! (*this == other); }
    };

    /** The i-th argument of a node, everything about it in one place. */
    struct Argument
    {
        char tag = 'N';
        std::optional<Range> range;
        std::optional<osc::Value> value;
        std::string unit;
        std::string clipmode;
    };

    struct Node
    {
        std::string fullPath;

        /** TYPE as given, one character per argument. Absent on a container. */
        std::optional<std::string> type;

        /** ACCESS as given: 0 none, 1 read, 2 write, 3 both. */
        std::optional<int> access;

        /** One entry per argument, in type-tag order; nullopt where the device
            sent null. Empty when RANGE is absent. */
        std::vector<std::optional<Range>> range;

        /** One per argument where given; empty strings fill in. */
        std::vector<std::string> unit;
        std::vector<std::string> clipmode;

        std::optional<std::string> description;
        bool critical = false;

        /** VALUE, one element per argument, typed by TYPE. */
        bool hasValue = false;
        osc::Values value;

        /** CONTENTS, keyed by the last segment of each child's path. */
        std::map<std::string, Node> children;

        /** False for a container whose CONTENTS the device did not send (or
            this client has not fetched yet). A loaded container may be empty. */
        bool loaded = true;

        /** Attributes this model does not read, as the device sent them. */
        juce::NamedValueSet extras;

        //==============================================================================
        bool isLeaf() const noexcept       { return type.has_value(); }
        bool isContainer() const noexcept  { return ! type.has_value(); }

        int effectiveAccess() const noexcept { return access.value_or (3); }

        std::size_t argumentCount() const noexcept { return type.has_value() ? type->size() : 0; }
        Argument argument (std::size_t index) const;

        /** The last segment of fullPath; "" for the root. */
        std::string name() const;

        bool bindableForSend() const noexcept   { return isLeaf() && (effectiveAccess() & 2) != 0; }
        bool bindableForRecord() const noexcept { return isLeaf() && (effectiveAccess() & 1) != 0; }
        bool bindable() const noexcept          { return bindableForSend() || bindableForRecord(); }
    };

    //==============================================================================
    /*  A JSON value read as the OSC type a tag names: a number on an `i` node
        is an int32, on an `f` a float32, on a `d` a float64; a string is a
        string; true/false (or a number) on `T`/`F` is a boolean. A tag this
        cannot honour, or a value of the wrong shape, falls back to what the
        JSON itself suggests (number -> float64, string, boolean), never to zero. */
    osc::Value valueFromVar (const juce::var& value, char tag);

    /*  VALUE as an array, one element per tag of `type`. A scalar is read as
        one element. Elements past the type's length keep the JSON's own type. */
    osc::Values valuesFromVar (const juce::var& value, const std::string& type);

    /** The JSON for a value, the way a server writes it: numbers, strings, booleans, null. */
    juce::var varFromValue (const osc::Value& value);
}
