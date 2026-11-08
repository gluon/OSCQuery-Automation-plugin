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

#include <core/Scaling.h>

#include <algorithm>

namespace oqa::core
{
    const char* scalingName (Scaling scaling) noexcept
    {
        switch (scaling)
        {
            case Scaling::linear:       return "linear";
            case Scaling::logarithmic:  return "log";
            case Scaling::exponential:  return "exp";
        }

        return "linear";
    }

    std::optional<Scaling> scalingFromName (std::string_view name) noexcept
    {
        if (name == "linear")                          return Scaling::linear;
        if (name == "log" || name == "logarithmic")    return Scaling::logarithmic;
        if (name == "exp" || name == "exponential")    return Scaling::exponential;

        return std::nullopt;
    }

    namespace
    {
        constexpr double clamp01 (double t) noexcept
        {
            return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        }

        /*  The exponential warp and its inverse on the normalised axis: one
            decade over the travel. w (0) = 0, w (1) = 1. */
        double expWarp (double t) noexcept      { return (std::pow (10.0, t) - 1.0) / 9.0; }
        double expUnwarp (double w) noexcept    { return std::log10 (1.0 + 9.0 * w); }
    }

    double Mapping::toDevice (double normalised) const noexcept
    {
        if (! valid())
            return std::isfinite (min) ? min : 0.0;

        const double t = clamp01 (std::isfinite (normalised) ? normalised : 0.0);

        switch (effectiveScaling())
        {
            case Scaling::logarithmic:
                return min * std::pow (max / min, t);

            case Scaling::exponential:
                return min + (max - min) * expWarp (t);

            case Scaling::linear:
                break;
        }

        return min + (max - min) * t;
    }

    double Mapping::toHost (double units) const noexcept
    {
        if (! valid() || ! std::isfinite (units))
            return 0.0;

        const double lo = std::min (min, max);
        const double hi = std::max (min, max);
        const double u  = std::clamp (units, lo, hi);

        switch (effectiveScaling())
        {
            case Scaling::logarithmic:
                return clamp01 (std::log (u / min) / std::log (max / min));

            case Scaling::exponential:
                return clamp01 (expUnwarp ((u - min) / (max - min)));

            case Scaling::linear:
                break;
        }

        return clamp01 ((u - min) / (max - min));
    }
}
