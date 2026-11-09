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

#include <core/Slot.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace oqa::core
{
    const char* directionName (Direction direction) noexcept
    {
        switch (direction)
        {
            case Direction::send:     return "send";
            case Direction::receive:  return "receive";
            case Direction::both:     return "both";
        }

        return "send";
    }

    std::optional<Direction> directionFromName (std::string_view name) noexcept
    {
        if (name == "send")     return Direction::send;
        if (name == "receive")  return Direction::receive;
        if (name == "both")     return Direction::both;

        return std::nullopt;
    }

    bool isBindableTag (char tag) noexcept
    {
        return tag == 'f' || tag == 'i' || tag == 'T' || tag == 'F';
    }

    std::string defaultDisplayName (const oscquery::Node& node, std::size_t argIndex)
    {
        std::string name = node.fullPath;

        if (! name.empty() && name.front() == '/')
            name.erase (0, 1);

        if (node.argumentCount() > 1)
            name += "[" + std::to_string (argIndex) + "]";

        return name;
    }

    //==============================================================================
    namespace
    {
        /*  MIN and MAX where the device gave them; else the first and last
            numeric VALS, which is what an enumerated `i` has instead. */
        bool rangeOf (const oscquery::Argument& argument, double& min, double& max) noexcept
        {
            if (! argument.range.has_value())
                return false;

            const auto& range = *argument.range;

            if (range.hasMinMax())
            {
                min = *range.min;
                max = *range.max;
                return std::isfinite (min) && std::isfinite (max);
            }

            if (range.hasVals())
            {
                bool any = false;
                double lo = std::numeric_limits<double>::infinity();
                double hi = -lo;

                for (const auto& v : range.vals)
                {
                    if (! v.isNumber() || v.isNonFinite())
                        return false;

                    lo = std::min (lo, v.asDouble());
                    hi = std::max (hi, v.asDouble());
                    any = true;
                }

                if (any)
                {
                    min = lo;
                    max = hi;
                    return true;
                }
            }

            return false;
        }
    }

    bool Slot::rangeKnown() const noexcept
    {
        if (isToggle())
            return true;

        return rangeMin.has_value() && rangeMax.has_value()
            && Mapping { *rangeMin, *rangeMax, scaling }.valid();
    }

    Mapping Slot::mapping() const noexcept
    {
        if (isToggle() || ! rangeKnown())
            return Mapping { 0.0, 1.0, Scaling::linear };

        return Mapping { *rangeMin, *rangeMax, scaling };
    }

    bool Slot::bind (const oscquery::Node& node, std::size_t argument, int device, std::string& why)
    {
        if (node.isContainer())
        {
            why = node.fullPath + " is a container; only a leaf binds";
            return false;
        }

        if (argument >= node.argumentCount())
        {
            why = node.fullPath + " has " + std::to_string (node.argumentCount())
                + " argument(s); there is no argument " + std::to_string (argument);
            return false;
        }

        const auto view = node.argument (argument);

        if (! isBindableTag (view.tag))
        {
            why = node.fullPath + " argument " + std::to_string (argument) + " is of type '"
                + std::string (1, view.tag) + "'; v1 binds f, i, T and F";
            return false;
        }

        const bool canSend    = node.bindableForSend();
        const bool canReceive = node.bindableForRecord();

        if (! canSend && ! canReceive)
        {
            why = node.fullPath + " has ACCESS " + std::to_string (node.effectiveAccess())
                + ": neither readable nor writable for this client";
            return false;
        }

        deviceIndex = device;
        path        = node.fullPath;
        argIndex    = static_cast<int> (argument);
        type        = view.tag;
        unit        = view.unit;
        critical    = node.critical;
        displayName = defaultDisplayName (node, argument);

        rangeOverridden = false;
        rangeMin.reset();
        rangeMax.reset();

        double min = 0.0, max = 0.0;

        if (! isToggle() && rangeOf (view, min, max))
        {
            rangeMin = min;
            rangeMax = max;
        }

        direction = canSend ? Direction::send : Direction::receive;
        return true;
    }

    void Slot::unbind() noexcept
    {
        deviceIndex = -1;
        path.clear();
        pathPattern.clear();
        argIndex = 0;
        type = 'f';
        rangeMin.reset();
        rangeMax.reset();
        rangeOverridden = false;
        displayName.clear();
        unit.clear();
        critical = false;
    }

    void Slot::setRange (double min, double max) noexcept
    {
        rangeMin = min;
        rangeMax = max;
        rangeOverridden = true;
    }

    //==============================================================================
    double Slot::toDevice (float normalised) const noexcept
    {
        if (isToggle())
            return normalised >= 0.5f ? 1.0 : 0.0;

        const double units = mapping().toDevice (static_cast<double> (normalised));
        return isInteger() ? std::round (units) : units;
    }

    float Slot::toHost (double units) const noexcept
    {
        if (isToggle())
            return units >= 0.5 ? 1.0f : 0.0f;

        return static_cast<float> (mapping().toHost (units));
    }

    oscquery::osc::Value Slot::deviceValue (float normalised) const noexcept
    {
        using oscquery::osc::Value;

        if (isToggle())
            return Value::boolean (normalised >= 0.5f);

        const double units = toDevice (normalised);

        if (isInteger())
        {
            constexpr double lo = static_cast<double> (std::numeric_limits<std::int32_t>::min());
            constexpr double hi = static_cast<double> (std::numeric_limits<std::int32_t>::max());
            return Value::int32 (static_cast<std::int32_t> (std::clamp (units, lo, hi)));
        }

        return Value::float32 (static_cast<float> (units));
    }

    std::optional<float> Slot::hostValue (const oscquery::osc::Value& value) const noexcept
    {
        if (value.isBool())
            return value.getBool() ? 1.0f : 0.0f;

        if (value.isNumber() && ! value.isNonFinite())
            return toHost (value.asDouble());

        return std::nullopt;
    }

    //==============================================================================
    SlotTable::SlotTable (int count)
    {
        slots.resize (static_cast<std::size_t> (std::max (0, count)));

        for (std::size_t i = 0; i < slots.size(); ++i)
            slots[i].hostParamId = static_cast<int> (i);
    }

    bool SlotTable::bindAll (int first, const oscquery::Node& node, int device, std::string& why)
    {
        const auto arguments = node.argumentCount();

        if (node.isContainer() || arguments == 0)
        {
            why = node.fullPath + " is a container; only a leaf binds";
            return false;
        }

        if (first < 0 || first + static_cast<int> (arguments) > size())
        {
            why = node.fullPath + " needs " + std::to_string (arguments) + " slot(s) from slot "
                + std::to_string (first) + "; the table has " + std::to_string (size());
            return false;
        }

        /*  All or nothing: bind copies first, then commit. */
        std::vector<Slot> staged;
        staged.reserve (arguments);

        for (std::size_t a = 0; a < arguments; ++a)
        {
            Slot copy = slots[static_cast<std::size_t> (first) + a];

            if (! copy.bind (node, a, device, why))
                return false;

            staged.push_back (std::move (copy));
        }

        for (std::size_t a = 0; a < arguments; ++a)
            slots[static_cast<std::size_t> (first) + a] = std::move (staged[a]);

        return true;
    }

    std::vector<int> SlotTable::groupOf (int slot) const
    {
        std::vector<int> group;

        if (slot < 0 || slot >= size())
            return group;

        const auto& anchor = slots[static_cast<std::size_t> (slot)];

        if (! anchor.bound())
            return group;

        for (int i = 0; i < size(); ++i)
        {
            const auto& s = slots[static_cast<std::size_t> (i)];

            if (s.bound() && s.deviceIndex == anchor.deviceIndex && s.path == anchor.path)
                group.push_back (i);
        }

        std::sort (group.begin(), group.end(), [this] (int a, int b)
        {
            return slots[static_cast<std::size_t> (a)].argIndex < slots[static_cast<std::size_t> (b)].argIndex;
        });

        return group;
    }

    int SlotTable::boundCount() const noexcept
    {
        return static_cast<int> (std::count_if (slots.begin(), slots.end(),
                                                [] (const Slot& s) { return s.bound(); }));
    }
}
