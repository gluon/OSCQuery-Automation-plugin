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
    THE RING FROM THE AUDIO THREADS TO A DEVICE'S NETWORK THREAD (PRD,
    Transport: "sends from the audio thread go through a lock-free queue to a
    realtime-priority network thread"). Many producers (every instance's
    process callback in the host, which may run them on several threads at
    once), one consumer (the device's thread).

    A BOUNDED QUEUE OF FIXED CELLS, NOTHING ALLOCATED AFTER CONSTRUCTION:
    Dmitry Vyukov's bounded MPMC queue, each cell carrying a sequence number
    that says whose turn it is. Full is a counted drop and a false, never a
    wait: the next block's value supersedes the one that did not fit, and the
    counter is what the status bar shows (PRD failure states, "dropped sends").
    go.dot's monitor/TrafficTap.h is the same shape.

    WHAT TRAVELS: a slot's new value, or an end-of-block marker from an
    instance, each tagged with the instance that sent it. Twelve bytes.
*/

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace oqa::core
{
    struct SlotChange
    {
        enum Kind : std::uint16_t
        {
            value = 0,          ///< `slot` of `instance` is now `normalised`
            endOfBlock = 1      ///< `instance` has posted everything for this block
        };

        std::uint32_t instance = 0;
        std::uint16_t slot = 0;
        std::uint16_t kind = value;
        float normalised = 0.0f;
    };

    template <std::size_t Capacity>
    class ChangeRing
    {
        static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "a power of two");

    public:
        ChangeRing() noexcept
        {
            for (std::size_t i = 0; i < Capacity; ++i)
                cells[i].sequence.store (i, std::memory_order_relaxed);
        }

        ChangeRing (const ChangeRing&) = delete;
        ChangeRing& operator= (const ChangeRing&) = delete;

        static constexpr std::size_t capacity() noexcept { return Capacity; }

        /** Any thread. False, and a counted drop, when full. */
        bool push (const SlotChange& change) noexcept
        {
            auto pos = tail.load (std::memory_order_relaxed);

            for (;;)
            {
                auto& cell = cells[pos & mask];
                const auto sequence = cell.sequence.load (std::memory_order_acquire);
                const auto difference = static_cast<std::intptr_t> (sequence) - static_cast<std::intptr_t> (pos);

                if (difference == 0)
                {
                    if (tail.compare_exchange_weak (pos, pos + 1, std::memory_order_relaxed))
                    {
                        cell.item = change;
                        cell.sequence.store (pos + 1, std::memory_order_release);
                        return true;
                    }
                }
                else if (difference < 0)
                {
                    droppedCount.fetch_add (1, std::memory_order_relaxed);
                    return false;
                }
                else
                {
                    pos = tail.load (std::memory_order_relaxed);
                }
            }
        }

        /** The consumer. False when empty. */
        bool pop (SlotChange& out) noexcept
        {
            auto pos = head.load (std::memory_order_relaxed);

            for (;;)
            {
                auto& cell = cells[pos & mask];
                const auto sequence = cell.sequence.load (std::memory_order_acquire);
                const auto difference = static_cast<std::intptr_t> (sequence) - static_cast<std::intptr_t> (pos + 1);

                if (difference == 0)
                {
                    if (head.compare_exchange_weak (pos, pos + 1, std::memory_order_relaxed))
                    {
                        out = cell.item;
                        cell.sequence.store (pos + mask + 1, std::memory_order_release);
                        return true;
                    }
                }
                else if (difference < 0)
                {
                    return false;
                }
                else
                {
                    pos = head.load (std::memory_order_relaxed);
                }
            }
        }

        std::uint64_t dropped() const noexcept { return droppedCount.load (std::memory_order_relaxed); }

        /** Roughly how many are waiting; exact only when nobody else moves. */
        std::size_t approximateSize() const noexcept
        {
            const auto t = tail.load (std::memory_order_relaxed);
            const auto h = head.load (std::memory_order_relaxed);
            return t >= h ? t - h : 0;
        }

    private:
        struct Cell
        {
            std::atomic<std::size_t> sequence;
            SlotChange item;
        };

        static constexpr std::size_t mask = Capacity - 1;

        std::array<Cell, Capacity> cells;

        /*  Each on its own cache line, so producers bumping the tail do not
            evict the consumer's head. MSVC warns that the padding this asks
            for is padding; it is. */
       #if defined (_MSC_VER)
        #pragma warning (push)
        #pragma warning (disable: 4324)
       #endif
        alignas (64) std::atomic<std::size_t> tail { 0 };
        alignas (64) std::atomic<std::size_t> head { 0 };
        alignas (64) std::atomic<std::uint64_t> droppedCount { 0 };
       #if defined (_MSC_VER)
        #pragma warning (pop)
       #endif
    };
}
