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
    THE PER-SLOT RATE LIMITER (devplan Phase 3: "Per-slot rate limiter,
    default off"; PRD: "optional periodic resend for lossy links"). Driven by
    a clock the test holds, so nothing here waits.
*/

#include <doctest/doctest.h>

#include <core/send/RateLimiter.h>

#include <utility>
#include <vector>

using namespace oqa::core;

namespace
{
    constexpr std::int64_t ms = 1'000'000LL;

    struct Sent
    {
        std::vector<std::pair<std::size_t, double>> items;

        auto fn()
        {
            return [this] (std::size_t key, double value) { items.emplace_back (key, value); };
        }
    };
}

TEST_CASE ("rate limiter: off by default, every offer goes and nothing is ever held")
{
    RateLimiter limiter (4);
    CHECK (limiter.options().maxHz == 0.0);
    CHECK (limiter.options().resendMs == 0.0);

    for (int i = 0; i < 100; ++i)
        CHECK (limiter.offer (1, i * 0.01, i * ms / 10));

    CHECK (limiter.held() == 0);

    Sent sent;
    limiter.flushDue (1000 * ms, sent.fn());
    CHECK (sent.items.empty());
}

TEST_CASE ("rate limiter: at 100 Hz a second value within 10 ms is held, and the latest goes when due")
{
    RateLimiter limiter (4);
    limiter.setOptions ({ 100.0, 0.0 });

    CHECK (limiter.offer (2, 0.1, 0));
    CHECK_FALSE (limiter.offer (2, 0.2, 3 * ms));
    CHECK_FALSE (limiter.offer (2, 0.3, 6 * ms));
    CHECK (limiter.held() == 2);

    Sent sent;
    limiter.flushDue (9 * ms, sent.fn());
    CHECK (sent.items.empty());

    limiter.flushDue (10 * ms, sent.fn());
    REQUIRE (sent.items.size() == 1);
    CHECK (sent.items[0].first == 2);
    CHECK (sent.items[0].second == doctest::Approx (0.3));

    /*  Sent at 10 ms; the next window opens at 20 ms. */
    CHECK_FALSE (limiter.offer (2, 0.4, 15 * ms));
    CHECK (limiter.offer (2, 0.5, 20 * ms));

    /*  The held 0.4 was superseded by 0.5 going directly; nothing is left. */
    sent.items.clear();
    limiter.flushDue (100 * ms, sent.fn());
    CHECK (sent.items.empty());
}

TEST_CASE ("rate limiter: keys are independent, and an unknown key always goes")
{
    RateLimiter limiter (2);
    limiter.setOptions ({ 50.0, 0.0 });

    CHECK (limiter.offer (0, 1.0, 0));
    CHECK (limiter.offer (1, 1.0, 0));
    CHECK_FALSE (limiter.offer (0, 2.0, 1 * ms));
    CHECK_FALSE (limiter.offer (1, 2.0, 1 * ms));

    CHECK (limiter.offer (7, 1.0, 0));
    CHECK (limiter.offer (7, 2.0, 0));

    Sent sent;
    limiter.flushDue (20 * ms, sent.fn());
    CHECK (sent.items.size() == 2);
}

TEST_CASE ("rate limiter: a resend period repeats an unchanged value, and counts it")
{
    RateLimiter limiter (1);
    limiter.setOptions ({ 0.0, 500.0 });

    CHECK (limiter.offer (0, 0.75, 0));

    Sent sent;
    limiter.flushDue (499 * ms, sent.fn());
    CHECK (sent.items.empty());

    limiter.flushDue (500 * ms, sent.fn());
    REQUIRE (sent.items.size() == 1);
    CHECK (sent.items[0].second == doctest::Approx (0.75));
    CHECK (limiter.resent() == 1);

    /*  Not again until another period has passed since that resend. */
    limiter.flushDue (900 * ms, sent.fn());
    CHECK (sent.items.size() == 1);

    limiter.flushDue (1000 * ms, sent.fn());
    CHECK (sent.items.size() == 2);
    CHECK (limiter.resent() == 2);

    /*  A new value resets the clock. */
    CHECK (limiter.offer (0, 0.5, 1200 * ms));
    limiter.flushDue (1699 * ms, sent.fn());
    CHECK (sent.items.size() == 2);
    limiter.flushDue (1700 * ms, sent.fn());
    REQUIRE (sent.items.size() == 3);
    CHECK (sent.items[2].second == doctest::Approx (0.5));
}

TEST_CASE ("rate limiter: a held value is not also resent; reset forgets everything")
{
    RateLimiter limiter (1);
    limiter.setOptions ({ 10.0, 50.0 });

    CHECK (limiter.offer (0, 1.0, 0));
    CHECK_FALSE (limiter.offer (0, 2.0, 10 * ms));

    Sent sent;
    limiter.flushDue (60 * ms, sent.fn());

    /*  At 60 ms the 100 ms window is not over, so 2.0 is still held, and the
        resend of 1.0 does not fire while something newer waits. */
    CHECK (sent.items.empty());

    limiter.flushDue (100 * ms, sent.fn());
    REQUIRE (sent.items.size() == 1);
    CHECK (sent.items[0].second == doctest::Approx (2.0));
    CHECK (limiter.resent() == 0);

    limiter.reset();
    CHECK (limiter.offer (0, 3.0, 101 * ms));

    /*  Options that make no sense are read as off. */
    limiter.setOptions ({ -5.0, -1.0 });
    CHECK (limiter.options().maxHz == 0.0);
    CHECK (limiter.options().resendMs == 0.0);
}
