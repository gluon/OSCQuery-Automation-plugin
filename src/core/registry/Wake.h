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
    THE WAKE FROM THE AUDIO THREAD TO A DEVICE'S NETWORK THREAD: the one
    syscall the audio path makes on purpose (decided by the author
    2026-11-08), once per block per device. signal() never blocks; waitFor()
    returns when signalled or when the time is up.

    WHY NOT std::counting_semaphore EVERYWHERE: MSVC's and libstdc++'s wake
    the waiter at once (WaitOnAddress, a futex), but libc++'s try_acquire_for
    polls with a back-off that sleeps longer the longer it has waited, so on
    macOS a release was seen milliseconds late and blocks a few milliseconds
    apart merged into one bundle (the Mac mini, 2026-11-08). Apple's own
    dispatch semaphore wakes at once and its signal is realtime-safe; it is
    what audio code on that platform uses.
*/

#include <chrono>

#if defined (__APPLE__)
 #include <dispatch/dispatch.h>
#else
 #include <semaphore>
#endif

namespace oqa::core
{
    class Wake
    {
    public:
      #if defined (__APPLE__)
        Wake() : semaphore (dispatch_semaphore_create (0)) {}

        ~Wake()
        {
            /*  Dispatch asserts when a semaphore is disposed below its
                starting value; waits that timed out restored it, and extra
                signals only raise it, so this is safe. */
            dispatch_release (semaphore);
        }

        void signal() noexcept
        {
            dispatch_semaphore_signal (semaphore);
        }

        /** True when signalled; false when the time ran out. */
        bool waitFor (std::chrono::nanoseconds time) noexcept
        {
            const auto ns = time.count() > 0 ? time.count() : 0;
            return dispatch_semaphore_wait (semaphore, dispatch_time (DISPATCH_TIME_NOW, ns)) == 0;
        }

        /** Take every pending signal without waiting. */
        void drain() noexcept
        {
            while (dispatch_semaphore_wait (semaphore, DISPATCH_TIME_NOW) == 0) {}
        }

    private:
        dispatch_semaphore_t semaphore;
      #else
        Wake() = default;

        void signal() noexcept
        {
            semaphore.release();
        }

        bool waitFor (std::chrono::nanoseconds time) noexcept
        {
            return time.count() > 0 ? semaphore.try_acquire_for (time) : semaphore.try_acquire();
        }

        void drain() noexcept
        {
            while (semaphore.try_acquire()) {}
        }

    private:
        std::counting_semaphore<1 << 20> semaphore { 0 };
      #endif

    public:
        Wake (const Wake&) = delete;
        Wake& operator= (const Wake&) = delete;
    };
}
