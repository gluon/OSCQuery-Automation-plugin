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
    THE IDENTITY LEAF'S VALUE AS TEXT (Phase 5b; PRD: "an optional identity
    leaf (a read-only node the user designates, such as a serial number)").
    The chunk stores the value as a string; the editor writes it from the
    tree's VALUE and the registry compares it with what the device answers on
    reopen, read without a tree (so a number comes back as a double). One
    spelling for both: a string as it is, a boolean as true/false, a number
    without a trailing fraction when it has none, locale-independent.
*/

#include <oscquery/osc/Value.h>

#include <juce_core/juce_core.h>

#include <cmath>
#include <string>

namespace oqa::core
{
    inline std::string identityText (const oscquery::osc::Value& value)
    {
        if (value.isString())
            return value.getString();

        if (value.isBool())
            return value.getBool() ? "true" : "false";

        if (value.isNumber())
        {
            const auto v = value.asDouble();

            if (std::isfinite (v) && std::abs (v) < 1.0e15 && v == std::floor (v))
                return std::to_string (static_cast<long long> (v));

            return juce::String (v, 6).trimCharactersAtEnd ("0").trimCharactersAtEnd (".").toStdString();
        }

        return {};
    }

    /** The first argument's text; empty when there is none. */
    inline std::string identityText (const oscquery::osc::Values& values)
    {
        return values.empty() ? std::string() : identityText (values.front());
    }
}
