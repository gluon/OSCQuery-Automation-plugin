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
    HOST 0–1 TO DEVICE UNITS (devplan Phase 3a: "host 0–1 <-> device units with
    linear, log and exponential scaling"). The three curves, their inverses,
    their ends, and what happens where the log curve cannot apply.
*/

#include <doctest/doctest.h>

#include <core/Scaling.h>

#include <cmath>
#include <limits>

using namespace oqa::core;

namespace
{
    bool near (double a, double b, double tolerance = 1e-9)
    {
        return std::abs (a - b) <= tolerance;
    }
}

TEST_CASE ("scaling names round-trip through the chunk's spelling")
{
    for (auto s : { Scaling::linear, Scaling::logarithmic, Scaling::exponential })
        CHECK (scalingFromName (scalingName (s)) == s);

    CHECK (scalingFromName ("log") == Scaling::logarithmic);
    CHECK (scalingFromName ("logarithmic") == Scaling::logarithmic);
    CHECK (scalingFromName ("exp") == Scaling::exponential);
    CHECK_FALSE (scalingFromName ("quadratic").has_value());
    CHECK_FALSE (scalingFromName ("").has_value());
}

TEST_CASE ("linear maps the ends to the ends and the middle to the middle")
{
    const Mapping m { -60.0, 12.0, Scaling::linear };

    CHECK (near (m.toDevice (0.0), -60.0));
    CHECK (near (m.toDevice (1.0), 12.0));
    CHECK (near (m.toDevice (0.5), -24.0));

    CHECK (near (m.toHost (-60.0), 0.0));
    CHECK (near (m.toHost (12.0), 1.0));
    CHECK (near (m.toHost (-24.0), 0.5));
}

TEST_CASE ("a reversed range (min above max) still maps both ways")
{
    const Mapping m { 10.0, -10.0, Scaling::linear };

    CHECK (near (m.toDevice (0.0), 10.0));
    CHECK (near (m.toDevice (1.0), -10.0));
    CHECK (near (m.toHost (10.0), 0.0));
    CHECK (near (m.toHost (-10.0), 1.0));
    CHECK (near (m.toHost (0.0), 0.5));
}

TEST_CASE ("the logarithmic curve gives equal ratio for equal travel")
{
    const Mapping m { 20.0, 20000.0, Scaling::logarithmic };

    CHECK (m.effectiveScaling() == Scaling::logarithmic);
    CHECK (near (m.toDevice (0.0), 20.0));
    CHECK (near (m.toDevice (1.0), 20000.0, 1e-6));

    /*  Three decades: a third of the travel is one decade. */
    CHECK (near (m.toDevice (1.0 / 3.0), 200.0, 1e-6));
    CHECK (near (m.toDevice (2.0 / 3.0), 2000.0, 1e-6));

    CHECK (near (m.toHost (200.0), 1.0 / 3.0));
    CHECK (near (m.toHost (2000.0), 2.0 / 3.0));
}

TEST_CASE ("the logarithmic curve applies to a range that is wholly negative")
{
    const Mapping m { -1.0, -100.0, Scaling::logarithmic };

    CHECK (m.effectiveScaling() == Scaling::logarithmic);
    CHECK (near (m.toDevice (0.5), -10.0));
    CHECK (near (m.toHost (-10.0), 0.5));
}

TEST_CASE ("where the logarithmic curve cannot apply the mapping says so and is linear")
{
    SUBCASE ("a range through zero")
    {
        const Mapping m { -1.0, 1.0, Scaling::logarithmic };
        CHECK_FALSE (Mapping::logAvailable (-1.0, 1.0));
        CHECK (m.effectiveScaling() == Scaling::linear);
        CHECK (near (m.toDevice (0.5), 0.0));
    }

    SUBCASE ("a range starting at zero")
    {
        const Mapping m { 0.0, 100.0, Scaling::logarithmic };
        CHECK_FALSE (Mapping::logAvailable (0.0, 100.0));
        CHECK (m.effectiveScaling() == Scaling::linear);
        CHECK (near (m.toDevice (0.25), 25.0));
    }
}

TEST_CASE ("the exponential curve is fine near min, one decade over the travel")
{
    const Mapping m { 0.0, 1.0, Scaling::exponential };

    CHECK (near (m.toDevice (0.0), 0.0));
    CHECK (near (m.toDevice (1.0), 1.0));

    const double mid = m.toDevice (0.5);
    CHECK (mid < 0.5);
    CHECK (near (mid, (std::sqrt (10.0) - 1.0) / 9.0));

    CHECK (near (m.toHost (mid), 0.5));
}

TEST_CASE ("every curve inverts itself to within rounding across the travel")
{
    const Mapping linear { -60.0, 12.0, Scaling::linear };
    const Mapping log    { 20.0, 20000.0, Scaling::logarithmic };
    const Mapping exp    { -10.0, 10.0, Scaling::exponential };

    for (int i = 0; i <= 100; ++i)
    {
        const double t = i / 100.0;

        for (const auto& m : { linear, log, exp })
        {
            INFO ("curve " << scalingName (m.scaling) << " at " << t);
            CHECK (near (m.toHost (m.toDevice (t)), t, 1e-9));
        }
    }
}

TEST_CASE ("out-of-range inputs are clamped, never extrapolated")
{
    const Mapping m { 0.0, 100.0, Scaling::linear };

    CHECK (near (m.toDevice (-0.5), 0.0));
    CHECK (near (m.toDevice (1.5), 100.0));
    CHECK (near (m.toHost (-1.0), 0.0));
    CHECK (near (m.toHost (250.0), 1.0));

    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK (near (m.toDevice (nan), 0.0));
    CHECK (near (m.toHost (nan), 0.0));
}

TEST_CASE ("an invalid mapping answers its min and zero, and never NaN")
{
    const Mapping point { 5.0, 5.0, Scaling::linear };
    CHECK_FALSE (point.valid());
    CHECK (near (point.toDevice (0.7), 5.0));
    CHECK (near (point.toHost (5.0), 0.0));

    const Mapping inf { 0.0, std::numeric_limits<double>::infinity(), Scaling::linear };
    CHECK_FALSE (inf.valid());
    CHECK (std::isfinite (inf.toDevice (0.3)));
    CHECK (std::isfinite (inf.toHost (3.0)));
}
