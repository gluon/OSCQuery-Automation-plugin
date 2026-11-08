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
    HOST 0–1 TO DEVICE UNITS AND BACK (PRD, Binding model: "The host always
    sees a normalised 0–1 float. Scaling to device units happens inside the
    plugin, so no host rescan is ever needed for a range change.")

    THREE CURVES, the PRD's "same choices as OSCar": linear (the default),
    logarithmic and exponential. Each is a warp of the normalised axis followed
    by the linear map onto [min, max], so every curve is its own inverse's
    partner and toHost (toDevice (t)) == t to rounding.

      linear       u = min + (max - min) * t
      logarithmic  u = min * (max / min) ^ t          equal travel, equal ratio
      exponential  u = min + (max - min) * (10^t - 1) / 9   fine near min

    THE LOG CURVE NEEDS A RANGE THAT EXCLUDES ZERO and keeps one sign (a
    frequency, a distance, never a pan). Where it cannot apply, the mapping says
    so and behaves as linear; the editor shows why. The exponential curve is one
    decade of growth over the travel, so it applies to any range.

    Implementer's definitions (2026-11-08), the author's to overrule against
    OSCar's manual; the chunk stores the curve by name, so a changed curve is a
    changed number on the wire and nothing else.

    EVERYTHING HERE IS noexcept AND ALLOCATION-FREE: the audio thread calls
    toDevice once per changed slot per block.
*/

#include <cmath>
#include <optional>
#include <string_view>

namespace oqa::core
{
    enum class Scaling
    {
        linear,
        logarithmic,
        exponential
    };

    /** The chunk's spelling: "linear", "log", "exp". */
    const char* scalingName (Scaling scaling) noexcept;
    std::optional<Scaling> scalingFromName (std::string_view name) noexcept;

    /** A range and a curve: everything toDevice and toHost need. */
    struct Mapping
    {
        double min = 0.0;
        double max = 1.0;
        Scaling scaling = Scaling::linear;

        /** Finite, and not a point. */
        bool valid() const noexcept
        {
            return std::isfinite (min) && std::isfinite (max) && min != max;
        }

        /** The log curve wants both ends on the same side of zero, neither on it. */
        static bool logAvailable (double min, double max) noexcept
        {
            return std::isfinite (min) && std::isfinite (max)
                && min != 0.0 && max != 0.0 && (min > 0.0) == (max > 0.0);
        }

        /** The curve actually applied: linear where log is unavailable. */
        Scaling effectiveScaling() const noexcept
        {
            if (scaling == Scaling::logarithmic && ! logAvailable (min, max))
                return Scaling::linear;

            return scaling;
        }

        /** 0–1 (clamped) to device units, within [min, max]. An invalid mapping answers min. */
        double toDevice (double normalised) const noexcept;

        /** Device units (clamped to the range) to 0–1. An invalid mapping answers 0. */
        double toHost (double units) const noexcept;
    };
}
