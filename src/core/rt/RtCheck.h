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
    THE AUDIO THREAD LIPOGRAM, INSTRUMENTED (devplan Phase 3: "no allocation,
    locks, exceptions, syscalls or logging in the process callback; instrument
    and assert", after go.dot's src/wfg/engine/rt/).

    HOW IT COUNTS. A region is a stretch of code that must not allocate: the
    instance's process path opens one with ScopedRealtimeCheck. Inside a
    region, every call of the global operator new is a violation. The call is
    seen because the TEST EXECUTABLE replaces operator new and delete
    (tests/support/RtAllocCounter.cpp) and has each replacement call
    noteAllocation(). A shipped plugin replaces nothing: a replaced allocator
    inside a plugin could reach the host's own objects (macOS coalesces weak
    definitions across loaded images), so there the hooks are never called,
    isCounting() is false, and a region costs one thread-local increment.

    WHAT IT DOES NOT CATCH: locks and syscalls. Those are the Clang RTSan
    job's (CI, when the runner has Clang 20), with [[clang::nonblocking]] on
    the entry points via OQA_AUDIO_THREAD. The one syscall the audio path
    makes on purpose, the per-block wake of a device's network thread, is the
    named exception (decided by the author 2026-11-08) and the only line of
    ours in the suppression file.

    A VIOLATION IS A NUMBER THAT GOES UP. Nothing is logged, thrown or
    printed from a region, here or anywhere.
*/

#include <atomic>
#include <cstdint>

#if defined (OQA_RTSAN) && defined (__has_cpp_attribute)
 #if __has_cpp_attribute (clang::nonblocking)
  #define OQA_AUDIO_THREAD [[clang::nonblocking]]
 #endif
#endif

#ifndef OQA_AUDIO_THREAD
 #define OQA_AUDIO_THREAD
#endif

namespace oqa::core::rt
{
    /*  constinit: valid before any constructor runs, because the first
        allocation the replacement sees may be a static initialiser's. */
    inline constinit std::atomic<std::uint64_t> allocationsInRegion { 0 };
    inline constinit std::atomic<std::uint64_t> regionsEntered { 0 };
    inline constinit std::atomic<bool> countingEnabled { false };

    inline thread_local constinit int regionDepth = 0;

    /** Called by the test executable's operator new. Cheap when outside a region. */
    inline void noteAllocation() noexcept
    {
        if (regionDepth > 0)
            allocationsInRegion.fetch_add (1, std::memory_order_relaxed);
    }

    /** True once an allocator replacement has announced itself: zero then means zero. */
    inline bool isCounting() noexcept           { return countingEnabled.load (std::memory_order_relaxed); }

    inline std::uint64_t violations() noexcept  { return allocationsInRegion.load (std::memory_order_relaxed); }
    inline std::uint64_t regions() noexcept     { return regionsEntered.load (std::memory_order_relaxed); }

    inline void resetCounts() noexcept
    {
        allocationsInRegion.store (0, std::memory_order_relaxed);
        regionsEntered.store (0, std::memory_order_relaxed);
    }

    /** The RAII region. Nests; the count of regions is of outermost entries. */
    struct ScopedRealtimeCheck
    {
        ScopedRealtimeCheck() noexcept
        {
            if (regionDepth++ == 0)
                regionsEntered.fetch_add (1, std::memory_order_relaxed);
        }

        ~ScopedRealtimeCheck() noexcept
        {
            --regionDepth;
        }

        ScopedRealtimeCheck (const ScopedRealtimeCheck&) = delete;
        ScopedRealtimeCheck& operator= (const ScopedRealtimeCheck&) = delete;
    };
}
