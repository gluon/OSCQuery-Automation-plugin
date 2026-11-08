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
    THE DEVICES A USER CAN PICK FROM (PRD, Discovery: "Finding devices"): what
    DNS-SD finds under `_oscjson._tcp`, plus what the user typed as host and
    port, every one of them probed with `GET /?HOST_INFO` before it is listed.
    A device that fails the probe is shown as unreachable, not hidden.

    ONE LIST, TWO SOURCES. A found device and a typed one are the same kind of
    row: name, host, port, reachable or not, and the HOST_INFO that answered.
    The name is HOST_INFO's NAME once the device has answered (that is what the
    recents store keys on, and what C2 says the DNS-SD instance name equals);
    until then it is the instance name DNS-SD gave, or `host:port` for a typed
    entry nobody has heard from.

    A typed entry stays until it is removed. A found entry stays while DNS-SD
    still lists it, and for as long as it keeps answering after DNS-SD forgets
    it; it goes when both have given up.

    THREADS. One worker: it reads the browser's list when its revision moves,
    and probes each candidate in turn, on its own thread, blocking per probe
    for up to httpTimeoutMs. Readers copy the list out under one lock, and
    `revision()` says when it moved - the shape the editor polls, as go.dot's
    Discovery is polled. No callbacks, no MessageManager.
*/

#include <oscquery/HostInfo.h>
#include <oscquery/mdns/Discovery.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace oqa::oscquery
{
    class Finder
    {
    public:
        struct Options
        {
            /** False: no DNS-SD, typed entries only. */
            bool browse = true;

            /** The browser's own options: port 5353 and the group, or a port and peers for a test. */
            Discovery::Options discovery;

            int httpTimeoutMs = 3000;

            /** How long a probe's answer stands before the candidate is asked again. */
            int reprobeMs = 5000;

            /** The worker's tick. */
            int tickMs = 100;
        };

        struct Candidate
        {
            std::string name;
            std::string host;
            int port = 0;

            bool manual = false;        ///< typed by the user
            bool advertised = false;    ///< in the browser's list right now
            bool probed = false;        ///< asked at least once
            bool reachable = false;     ///< HOST_INFO answered on the last probe

            std::optional<HostInfo> hostInfo;
            std::string error;          ///< why the last probe failed, when it did
            std::vector<std::string> txt;

            /** When HOST_INFO last answered; the epoch if never. */
            std::chrono::steady_clock::time_point lastSeen {};

            std::string key() const { return host + ":" + std::to_string (port); }
        };

        Finder();
        ~Finder();

        Finder (const Finder&) = delete;
        Finder& operator= (const Finder&) = delete;

        /** False only when browsing was asked for and no socket could be had at all. */
        bool start (Options options);
        void stop();
        bool isRunning() const noexcept;

        /** A typed host and port, probed like the rest. */
        void addManual (const std::string& host, int port);
        bool removeManual (const std::string& host, int port);

        /** Ask DNS-SD now, and probe everything on the next tick. */
        void refresh();

        /** The list, sorted by name then host and port. */
        std::vector<Candidate> candidates() const;

        /** The browser's: "off", "browsing" or "unicast"; and what is wrong, if anything. */
        std::string browseMode() const;
        std::string browseProblem() const;

        /** Moves whenever the list changes. */
        std::uint64_t revision() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
