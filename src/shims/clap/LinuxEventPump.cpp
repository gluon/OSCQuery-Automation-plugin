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

#include "LinuxEventPump.h"

#if defined (__linux__)

#include <poll.h>

#include <vector>

namespace oqa::clapshim
{
    LinuxEventPump::LinuxEventPump (std::function<bool (int)> registerFd, std::function<void (int)> unregisterFd)
        : registerWithHost (std::move (registerFd)), unregisterWithHost (std::move (unregisterFd))
    {
        juce::LinuxEventLoopInternal::registerLinuxEventLoopListener (*this);
        refresh();
    }

    LinuxEventPump::~LinuxEventPump()
    {
        juce::LinuxEventLoopInternal::deregisterLinuxEventLoopListener (*this);

        if (unregisterWithHost)
            for (const auto fd : registered)
                unregisterWithHost (fd);
    }

    void LinuxEventPump::onFd (int fd)
    {
        juce::LinuxEventLoopInternal::invokeEventLoopCallbackForFd (fd);
    }

    void LinuxEventPump::pumpAll()
    {
        const auto fds = juce::LinuxEventLoopInternal::getRegisteredFds();

        if (fds.empty())
            return;

        std::vector<pollfd> polled;
        polled.reserve (fds.size());

        for (const auto fd : fds)
            polled.push_back ({ fd, POLLIN, 0 });

        if (poll (polled.data(), static_cast<nfds_t> (polled.size()), 0) <= 0)
            return;

        for (const auto& p : polled)
            if ((p.revents & POLLIN) != 0)
                juce::LinuxEventLoopInternal::invokeEventLoopCallbackForFd (p.fd);
    }

    void LinuxEventPump::fdCallbacksChanged()
    {
        refresh();
    }

    void LinuxEventPump::refresh()
    {
        if (! registerWithHost)
            return;

        const auto current = juce::LinuxEventLoopInternal::getRegisteredFds();
        const std::set<int> wanted (current.begin(), current.end());

        for (auto it = registered.begin(); it != registered.end();)
        {
            if (wanted.count (*it) == 0)
            {
                if (unregisterWithHost)
                    unregisterWithHost (*it);

                it = registered.erase (it);
            }
            else
                ++it;
        }

        for (const auto fd : wanted)
            if (registered.count (fd) == 0 && registerWithHost (fd))
                registered.insert (fd);
    }
}

#endif
