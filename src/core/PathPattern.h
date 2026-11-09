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
    THE $instance TOKEN (PRD, Binding model: "A `$instance` token in paths is
    supported for ToscA-style per-track numbering, but it is sugar over the
    explicit path, not the primary model"; devplan Phase 6d-2). A pattern is
    a path with the token; the instance's number (the chunk's `instance`,
    user-set, default 1) replaces every occurrence, and the slot keeps the
    pattern beside the resolved path (`path_pattern`), so a change of number
    rebinds, and the Monitor applies one pattern across instances.

    Pure: no device, no registry.
*/

#include <string>
#include <string_view>

namespace oqa::core
{
    inline constexpr std::string_view instanceToken = "$instance";

    inline bool hasInstanceToken (std::string_view pattern) noexcept
    {
        return pattern.find (instanceToken) != std::string_view::npos;
    }

    /** The pattern with every `$instance` replaced by the number; the pattern itself when it has none. */
    inline std::string substituteInstance (std::string_view pattern, int number)
    {
        std::string out;
        out.reserve (pattern.size() + 8);
        const auto digits = std::to_string (number);
        std::size_t at = 0;

        while (at < pattern.size())
        {
            const auto next = pattern.find (instanceToken, at);

            if (next == std::string_view::npos)
            {
                out.append (pattern.substr (at));
                break;
            }

            out.append (pattern.substr (at, next - at));
            out.append (digits);
            at = next + instanceToken.size();
        }

        return out;
    }
}
