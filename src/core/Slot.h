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
    THE PRD'S SLOT TABLE AS A STRUCT (Binding model: slots, paths, scaling).
    A slot is one host-automatable parameter; an instance owns N of them, N
    fixed at build time. The host parameter ID is the slot's index and never
    moves; everything else is what the user bound.

    | Slot field        | Source                       | Here                          |
    | Host parameter ID | fixed at build time          | hostParamId                   |
    | Device            | user choice in browser       | deviceIndex into the chunk    |
    | Path              | user choice in browser       | path, argIndex                |
    | Type              | from tree                    | type: f, i, T or F            |
    | Range             | from tree, user-overridable  | rangeMin, rangeMax, overridden|
    | Scaling           | user choice                  | scaling                       |
    | Direction         | user choice                  | direction                     |
    | Display name      | from path, user-editable     | displayName                   |

    BINDING READS THE NODE, NEVER A TYPED ADDRESS: bind() takes a tree::Node
    and an argument index, and fills type, range, unit and name from it. A node
    of TYPE "fff" binds as three consecutive slots (SlotTable::bindAll), and
    groupOf() finds them again so the send path makes one message of them.

    RANGE ABSENT -> INACTIVE: a slot bound to a leaf without RANGE is bound but
    not active until the user enters min and max (PRD). A toggle has no range
    and is active as soon as bound. An `i` with VALS and no MIN/MAX takes the
    first and last numeric value as its range.

    THE TYPES V1 BINDS are f, i, T and F (the PRD's list). T and F are one
    toggle either way: the device publishes one or the other as the current
    value's type, and the slot sends true above 0.5. Anything else (s, d, h, b)
    refuses to bind, with the reason.

    toDevice and deviceValue are noexcept and allocation-free: the audio thread
    calls them once per changed slot per block.
*/

#include <core/Scaling.h>

#include <oscquery/osc/Value.h>
#include <oscquery/tree/Node.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oqa::core
{
    enum class Direction
    {
        send,
        receive,
        both
    };

    /** The chunk's spelling: "send", "receive", "both". */
    const char* directionName (Direction direction) noexcept;
    std::optional<Direction> directionFromName (std::string_view name) noexcept;

    /** True for the OSC type tags a v1 slot binds: f, i, T, F. */
    bool isBindableTag (char tag) noexcept;

    /** The default display name: the path without its leading slash, and the
        argument index in brackets on a multi-argument node. */
    std::string defaultDisplayName (const oscquery::Node& node, std::size_t argIndex);

    //==============================================================================
    struct Slot
    {
        int hostParamId = 0;

        int deviceIndex = -1;
        std::string path;
        int argIndex = 0;

        /*  The pattern `path` was resolved from, with its $instance token
            (PathPattern.h; chunk `path_pattern`), empty for a slot bound to
            an explicit path. Kept so a change of the instance's number
            rebinds (Phase 6d-2). */
        std::string pathPattern;

        char type = 'f';

        std::optional<double> rangeMin;
        std::optional<double> rangeMax;
        bool rangeOverridden = false;

        Scaling scaling = Scaling::linear;
        Direction direction = Direction::send;

        std::string displayName;
        std::string unit;

        /** CRITICAL from the tree at bind time: this slot's messages take the reliable route (spec L4). */
        bool critical = false;

        //==============================================================================
        bool bound() const noexcept     { return ! path.empty(); }
        bool isToggle() const noexcept  { return type == 'T' || type == 'F'; }
        bool isInteger() const noexcept { return type == 'i'; }

        /** Both ends known and usable; always true for a toggle. */
        bool rangeKnown() const noexcept;

        /** Bound, and with a range it can scale by: what the PRD calls active. */
        bool active() const noexcept { return bound() && rangeKnown(); }

        bool sends() const noexcept    { return direction != Direction::receive; }
        bool receives() const noexcept { return direction != Direction::send; }

        /** The mapping in force: the slot's range and curve; 0–1 linear for a toggle. */
        Mapping mapping() const noexcept;

        //==============================================================================
        /*  Bind to one argument of a leaf. Fills type, range, unit and the
            default display name; keeps scaling; sets direction to send where
            the leaf is writable, else receive. False with the reason when the
            node is a container, the argument does not exist, its type is not
            one v1 binds, or its ACCESS allows neither. */
        bool bind (const oscquery::Node& node, std::size_t argument, int device, std::string& why);

        /** Back to unbound; the display name goes with it. */
        void unbind() noexcept;

        /** The user's min and max; marks the range overridden. */
        void setRange (double min, double max) noexcept;

        //==============================================================================
        /** 0–1 to device units: rounded for an `i`, 0 or 1 for a toggle. */
        double toDevice (float normalised) const noexcept;

        /** Device units to 0–1. */
        float toHost (double units) const noexcept;

        /** The typed OSC value to send for a host value: float32, int32 or boolean. */
        oscquery::osc::Value deviceValue (float normalised) const noexcept;

        /** The host value for a received OSC value; nullopt for a value of no numeric or boolean kind. */
        std::optional<float> hostValue (const oscquery::osc::Value& value) const noexcept;
    };

    //==============================================================================
    /** N slots, the host parameter ID being the index. */
    class SlotTable
    {
    public:
        explicit SlotTable (int count);

        int size() const noexcept { return static_cast<int> (slots.size()); }

        Slot& operator[] (int index)             { return slots[static_cast<std::size_t> (index)]; }
        const Slot& operator[] (int index) const { return slots[static_cast<std::size_t> (index)]; }

        const std::vector<Slot>& all() const noexcept { return slots; }

        /*  Bind every argument of a leaf to consecutive slots from `first`
            (the PRD: "Multi-argument nodes (fff) bind as consecutive slots").
            All or nothing: false with the reason when the table runs out of
            slots or any argument refuses. Slots already bound in the way are
            rebound. */
        bool bindAll (int first, const oscquery::Node& node, int device, std::string& why);

        /*  The slots bound to the same device and path as `slot`, in argument
            order, `slot` included: the one message the send path groups them
            into. Empty for an unbound slot. */
        std::vector<int> groupOf (int slot) const;

        int boundCount() const noexcept;

    private:
        std::vector<Slot> slots;
    };
}
