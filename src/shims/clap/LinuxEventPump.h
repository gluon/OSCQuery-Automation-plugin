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
    JUCE'S EVENT LOOP IN A CLAP HOST ON LINUX (Phase 5b-4). On Windows and
    macOS the host's own message loop dispatches JUCE's messages (a hidden
    window, a run-loop source); on Linux JUCE waits on file descriptors (the
    X11 connection, its internal message queue) that something has to watch.
    JUCE's own VST3 wrapper hands them to the host's run loop; this does the
    same through clap_host_posix_fd_support: every descriptor JUCE registers
    is registered with the host, and when the host says one is ready, JUCE
    is told to read it. Where the host offers no posix-fd support, the 50 ms
    timer polls them instead (pumpAll).

    Linux only; the header compiles to nothing elsewhere.
*/

#if defined (__linux__)

#include <juce_events/juce_events.h>
#include <juce_events/native/juce_EventLoopInternal_linux.h>

#include <functional>
#include <set>

namespace oqa::clapshim
{
    class LinuxEventPump final : private juce::LinuxEventLoopInternal::Listener
    {
    public:
        /*  `registerFd` and `unregisterFd` reach the host's posix-fd
            extension; either may be empty when the host has none, and then
            only pumpAll does anything. */
        LinuxEventPump (std::function<bool (int fd)> registerFd, std::function<void (int fd)> unregisterFd);
        ~LinuxEventPump() override;

        /** The host says this descriptor is ready: JUCE reads it. Main thread. */
        void onFd (int fd);

        /** Without posix-fd support: read every descriptor that has something, now. Main thread. */
        void pumpAll();

    private:
        void fdCallbacksChanged() override;
        void refresh();

        std::function<bool (int)> registerWithHost;
        std::function<void (int)> unregisterWithHost;
        std::set<int> registered;
    };
}

#endif
