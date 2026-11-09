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
    THE GESTURE A RECEIVED VALUE OPENS (devplan Phase 4: "Write/Touch/Latch is
    the host's decision, not the plugin's. The plugin's job is the gesture:
    open it on the first incoming value, close it after an idle timeout
    (default 200 ms, setting)"). A host in Read mode ignores the push; in
    Touch, Latch or Write it records what arrives between the two gesture
    events, as it would a hand on a fader.

    Pure bookkeeping, per slot, on one thread (the shim's message thread):
    which slots have a gesture open and when each last received. The shim
    turns the answers into the format's calls (JUCE: beginChangeGesture,
    setValueNotifyingHost, endChangeGesture; the CLAP shim's out-events in
    Phase 5b). No clock of its own: the caller passes the time, so a test
    needs no waiting.
*/

#include <functional>
#include <vector>

namespace oqa::core
{
    class GestureTracker
    {
    public:
        explicit GestureTracker (int slotCount);

        int slotCount() const noexcept { return static_cast<int> (lastAt.size()); }

        /*  A value arrived for `slot` at `nowMs`. True when this opens the
            slot's gesture (none was open): the caller tells the host before
            pushing the value. False for a slot out of range. */
        bool received (int slot, double nowMs);

        /*  Close every gesture that has had no value for longer than
            `timeoutMs`, calling `close (slot)` for each; returns how many. */
        int expire (double nowMs, double timeoutMs, const std::function<void (int slot)>& close);

        /** Close every open gesture, calling `close (slot)` for each; returns how many. */
        int closeAll (const std::function<void (int slot)>& close);

        bool isOpen (int slot) const noexcept;
        int openCount() const noexcept;

    private:
        std::vector<double> lastAt;
        std::vector<bool> open;
    };
}
