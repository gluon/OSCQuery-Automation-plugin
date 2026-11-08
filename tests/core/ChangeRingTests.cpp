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
    THE RING (devplan Phase 3: "a lock-free queue from each instance's audio
    thread"): order kept, full is a counted drop, many producers at once lose
    nothing and duplicate nothing, and pushing allocates nothing.
*/

#include <doctest/doctest.h>

#include <core/registry/ChangeRing.h>
#include <core/rt/RtCheck.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace oqa::core;

TEST_CASE ("ring: what goes in comes out in order, and empty is empty")
{
    ChangeRing<8> ring;
    SlotChange out;

    CHECK_FALSE (ring.pop (out));

    for (std::uint16_t i = 0; i < 5; ++i)
        CHECK (ring.push ({ 7, i, SlotChange::value, static_cast<float> (i) * 0.1f }));

    CHECK (ring.approximateSize() == 5);

    for (std::uint16_t i = 0; i < 5; ++i)
    {
        REQUIRE (ring.pop (out));
        CHECK (out.instance == 7);
        CHECK (out.slot == i);
        CHECK (out.kind == SlotChange::value);
        CHECK (out.normalised == doctest::Approx (static_cast<float> (i) * 0.1f));
    }

    CHECK_FALSE (ring.pop (out));
    CHECK (ring.dropped() == 0);
}

TEST_CASE ("ring: full is a counted drop, never a wait, and room comes back as the consumer pops")
{
    ChangeRing<4> ring;

    for (std::uint16_t i = 0; i < 4; ++i)
        CHECK (ring.push ({ 1, i, SlotChange::value, 0.0f }));

    CHECK_FALSE (ring.push ({ 1, 99, SlotChange::value, 0.0f }));
    CHECK_FALSE (ring.push ({ 1, 98, SlotChange::endOfBlock, 0.0f }));
    CHECK (ring.dropped() == 2);

    SlotChange out;
    REQUIRE (ring.pop (out));
    CHECK (out.slot == 0);

    CHECK (ring.push ({ 1, 4, SlotChange::value, 0.0f }));

    for (std::uint16_t expected = 1; expected <= 4; ++expected)
    {
        REQUIRE (ring.pop (out));
        CHECK (out.slot == expected);
    }

    /*  Wrap-around many times over: the sequence numbers keep working. */
    for (int round = 0; round < 1000; ++round)
    {
        CHECK (ring.push ({ 2, static_cast<std::uint16_t> (round & 0xffff), SlotChange::value, 1.0f }));
        REQUIRE (ring.pop (out));
        CHECK (out.slot == static_cast<std::uint16_t> (round & 0xffff));
    }
}

TEST_CASE ("ring: four producers at once, one consumer, every item exactly once")
{
    constexpr int producers = 4;
    constexpr int perProducer = 20000;

    auto ring = std::make_unique<ChangeRing<4096>>();
    std::vector<std::vector<int>> seen (producers, std::vector<int> (perProducer, 0));
    std::atomic<int> finished { 0 };

    std::vector<std::thread> threads;

    for (int p = 0; p < producers; ++p)
        threads.emplace_back ([&, p]
        {
            for (int i = 0; i < perProducer; ++i)
            {
                /*  A producer that finds the ring full waits for the consumer:
                    the ring itself never does, which is what is under test. */
                while (! ring->push ({ static_cast<std::uint32_t> (p), 0, SlotChange::value, static_cast<float> (i) }))
                    std::this_thread::yield();
            }

            finished.fetch_add (1);
        });

    int popped = 0;
    SlotChange out;

    while (popped < producers * perProducer)
    {
        if (ring->pop (out))
        {
            REQUIRE (out.instance < static_cast<std::uint32_t> (producers));
            const auto index = static_cast<int> (out.normalised);
            REQUIRE (index >= 0);
            REQUIRE (index < perProducer);
            ++seen[out.instance][static_cast<std::size_t> (index)];
            ++popped;
        }
        else
        {
            std::this_thread::yield();
        }
    }

    for (auto& t : threads)
        t.join();

    CHECK (finished.load() == producers);
    CHECK_FALSE (ring->pop (out));

    for (int p = 0; p < producers; ++p)
        for (int i = 0; i < perProducer; ++i)
        {
            if (seen[p][static_cast<std::size_t> (i)] != 1)
            {
                FAIL ("producer " << p << " item " << i << " seen " << seen[p][static_cast<std::size_t> (i)] << " times");
            }
        }

    /*  The producers waited; the ring was told of no drop by them, since
        push only counts a drop when it answers false, and they retried. */
    MESSAGE ("the ring refused " << ring->dropped() << " pushes that were retried");
}

TEST_CASE ("ring: pushing and popping allocate nothing")
{
    if (! rt::isCounting())
    {
        MESSAGE ("not counting allocations in this executable");
        return;
    }

    auto ring = std::make_unique<ChangeRing<64>>();
    SlotChange out;

    rt::resetCounts();

    {
        rt::ScopedRealtimeCheck region;

        for (int round = 0; round < 500; ++round)
        {
            for (std::uint16_t i = 0; i < 32; ++i)
                ring->push ({ 1, i, SlotChange::value, 0.5f });

            ring->push ({ 1, 0, SlotChange::endOfBlock, 0.0f });

            while (ring->pop (out)) {}
        }
    }

    CHECK (rt::violations() == 0);
}
