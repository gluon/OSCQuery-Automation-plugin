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
    OSC OVER TCP (PRD, Transport: "TCP (SLIP-framed) when OSC_TRANSPORT is TCP
    or the node is marked CRITICAL"; spec draft L4): one connection to a
    device's TCP OSC_PORT, each packet framed as Framing says (SLIP by default,
    decided by the author 2026-11-08) and written whole.

    BLOCKING, on the caller's thread: connect() takes up to connectTimeoutMs,
    and send() connects first when it must. Phase 3's registry calls it from
    its network thread, never from the audio thread. A write that fails drops
    the connection so that the next send() connects again; the caller sees
    false and a reason each time, and a counter for the status bar.

    Nothing is read back. A device answers OSC over the websocket (LISTEN) or
    to the UDP port the user configured; the PRD has no reply path over this
    socket, and OSC 1.1 does not define one either.
*/

#include <oscquery/osc/Codec.h>
#include <oscquery/osc/Framing.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace oqa::oscquery::net
{
    class TcpSender
    {
    public:
        struct Options
        {
            std::string host;
            int port = 0;
            osc::Framing framing = osc::Framing::slip;
            int connectTimeoutMs = 3000;
        };

        explicit TcpSender (Options options);
        ~TcpSender();

        TcpSender (const TcpSender&) = delete;
        TcpSender& operator= (const TcpSender&) = delete;

        const Options& options() const noexcept;

        /** Connects now. True when connected (already, or by this call). */
        bool connect (std::string& error);
        void disconnect();
        bool isConnected() const;

        /** Frames and writes one encoded packet, connecting first if needed. */
        bool send (const std::vector<std::uint8_t>& packet, std::string& error);

        /** Encodes, then as above. */
        bool send (const osc::Packet& packet, std::string& error);

        std::uint64_t packetsSent() const noexcept;
        std::uint64_t failures() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
