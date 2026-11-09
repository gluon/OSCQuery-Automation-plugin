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

#include <core/receive/Gesture.h>

#include <algorithm>

namespace oqa::core
{
    GestureTracker::GestureTracker (int count)
        : lastAt (static_cast<std::size_t> (std::max (0, count)), 0.0),
          open (static_cast<std::size_t> (std::max (0, count)), false)
    {
    }

    bool GestureTracker::received (int slot, double nowMs)
    {
        if (slot < 0 || slot >= slotCount())
            return false;

        const auto i = static_cast<std::size_t> (slot);
        lastAt[i] = nowMs;

        if (open[i])
            return false;

        open[i] = true;
        return true;
    }

    int GestureTracker::expire (double nowMs, double timeoutMs, const std::function<void (int)>& close)
    {
        int closed = 0;

        for (std::size_t i = 0; i < open.size(); ++i)
        {
            if (! open[i] || nowMs - lastAt[i] <= timeoutMs)
                continue;

            open[i] = false;
            ++closed;

            if (close)
                close (static_cast<int> (i));
        }

        return closed;
    }

    int GestureTracker::closeAll (const std::function<void (int)>& close)
    {
        int closed = 0;

        for (std::size_t i = 0; i < open.size(); ++i)
        {
            if (! open[i])
                continue;

            open[i] = false;
            ++closed;

            if (close)
                close (static_cast<int> (i));
        }

        return closed;
    }

    bool GestureTracker::isOpen (int slot) const noexcept
    {
        return slot >= 0 && slot < slotCount() && open[static_cast<std::size_t> (slot)];
    }

    int GestureTracker::openCount() const noexcept
    {
        return static_cast<int> (std::count (open.begin(), open.end(), true));
    }
}
