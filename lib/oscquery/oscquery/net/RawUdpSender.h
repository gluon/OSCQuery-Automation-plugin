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
    A UDP SENDER THAT NEVER BLOCKS AND NEVER ALLOCATES ONCE OPEN: the one a
    realtime thread may call (PRD, Transport: "An option sends with
    non-blocking sendto directly from the audio thread for the lowest latency,
    at the risk of a stall on a full socket buffer; default off"), and the one
    the registry's network thread uses for every bundle.

    open() does the work that may wait: it resolves the host (getaddrinfo),
    makes the socket, sets it non-blocking and asks the system for send-buffer
    room (SocketBuffers). send() is one sendto() to the address resolved then.
    A full buffer answers false and counts a drop; the datagram is not retried,
    because the next block's value supersedes it. UdpEndpoint::send builds a
    juce::String and may resolve the host on every call, which is why it is
    not this.

    One sender per destination, used from one thread at a time: there is no
    lock in it. Counters are atomics, readable from any thread.
*/

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace oqa::oscquery::net
{
    class RawUdpSender
    {
    public:
        RawUdpSender() noexcept;
        ~RawUdpSender();

        RawUdpSender (const RawUdpSender&) = delete;
        RawUdpSender& operator= (const RawUdpSender&) = delete;

        /*  Resolve `host` (a dotted address or a name) and `port`, open a
            non-blocking socket to it. False with the reason; the sender is
            then closed. Not for a realtime thread. */
        bool open (const std::string& host, int port, std::string& error);

        void close() noexcept;
        bool isOpen() const noexcept;

        /*  One datagram. False when not open, when the buffer is full (a
            drop), or when the system refuses (a failure). Never waits. */
        bool send (const std::uint8_t* data, std::size_t size) noexcept;

        std::uint64_t sent() const noexcept     { return sentCount.load (std::memory_order_relaxed); }
        std::uint64_t dropped() const noexcept  { return droppedCount.load (std::memory_order_relaxed); }
        std::uint64_t failures() const noexcept { return failureCount.load (std::memory_order_relaxed); }

        /** The send buffer the system granted, in bytes; 0 when closed. */
        int sendBuffer() const noexcept         { return sendBytes; }

        /** The address and port as resolved, for the status bar. */
        const std::string& resolvedAddress() const noexcept { return resolved; }

        static constexpr int sendBufferWanted = 1024 * 1024;

    private:
        struct Impl;
        Impl* impl = nullptr;        // the system's socket and sockaddr; a pointer so no socket header is in this one

        std::string resolved;
        int sendBytes = 0;

        std::atomic<std::uint64_t> sentCount { 0 };
        std::atomic<std::uint64_t> droppedCount { 0 };
        std::atomic<std::uint64_t> failureCount { 0 };
    };
}
