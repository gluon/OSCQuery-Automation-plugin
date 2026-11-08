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
    OSC PACKETS ON A STREAM. UDP carries one packet per datagram and needs no
    framing; TCP is a byte stream and needs a way to say where a packet ends.
    Two ways exist:

      * SLIP (RFC 1055), with an END byte at both ends of every packet, as
        OSC 1.1 specifies and the spec draft proposes (L4, "(proposed)");
      * a 32-bit big-endian byte count before each packet, which OSC 1.0's
        stream transport described and which WFS-DIY's own TCP receiver reads
        (spatcore/control/osc/OSCTCPReceiver.h, read 2026-11-08).

    Both are here, pure, and the sender takes either (decided by the author
    2026-11-08: framing selectable, SLIP the default). Nothing here opens a
    socket; the Deframer is fed bytes and hands back packets.

    A DEFRAMER READS SOMEBODY ELSE'S BYTES, like the codec: a packet over the
    size limit is dropped and counted, never grown without bound; a SLIP
    escape that is not one is kept as the byte it was, as the RFC recommends,
    and counted; a byte count that cannot be right loses the stream its
    synchronisation, and the reader starts again at the next byte that could
    be a length - which is the honest thing, since there is no END to find.
*/

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oqa::oscquery::osc
{
    enum class Framing
    {
        slip,           ///< RFC 1055, double-END, as OSC 1.1
        lengthPrefix    ///< int32 big-endian count then the packet, as OSC 1.0's stream transport
    };

    const char* framingName (Framing framing) noexcept;

    /** "slip" or "length" (any case) to the enum; nullopt for anything else. */
    std::optional<Framing> framingFromName (const std::string& name);

    //==============================================================================
    namespace slip
    {
        inline constexpr std::uint8_t end    = 0xC0;
        inline constexpr std::uint8_t esc    = 0xDB;
        inline constexpr std::uint8_t escEnd = 0xDC;
        inline constexpr std::uint8_t escEsc = 0xDD;
    }

    /** END, the packet with END and ESC escaped, END. */
    std::vector<std::uint8_t> slipEncode (const std::uint8_t* data, std::size_t size);

    /** The byte count as int32 big-endian, then the packet. */
    std::vector<std::uint8_t> lengthPrefixEncode (const std::uint8_t* data, std::size_t size);

    /** One packet framed for a stream, either way. */
    std::vector<std::uint8_t> frame (Framing framing, const std::vector<std::uint8_t>& packet);

    //==============================================================================
    class Deframer
    {
    public:
        /** Packets longer than `maxPacketSize` are dropped. */
        explicit Deframer (Framing framing, std::size_t maxPacketSize = 65536);

        Framing framing() const noexcept { return kind; }

        /** Bytes off the wire, in any slicing. */
        void feed (const std::uint8_t* data, std::size_t size);
        void feed (const std::vector<std::uint8_t>& bytes) { feed (bytes.data(), bytes.size()); }

        /** The next complete packet, if one is ready. Call until nullopt. */
        std::optional<std::vector<std::uint8_t>> next();

        /** Packets dropped for being too long, and byte counts that could not be right. */
        std::uint64_t dropped() const noexcept { return droppedCount; }

        /** SLIP escapes that were not one; the byte was kept. */
        std::uint64_t violations() const noexcept { return violationCount; }

        /** Forgets the partial packet, as after a reconnect. */
        void reset();

    private:
        void feedSlip (const std::uint8_t* data, std::size_t size);
        void feedLength (const std::uint8_t* data, std::size_t size);

        Framing kind;
        std::size_t maxSize;
        std::vector<std::uint8_t> partial;
        std::vector<std::vector<std::uint8_t>> ready;
        bool escaping = false;
        bool discarding = false;
        std::uint64_t droppedCount = 0;
        std::uint64_t violationCount = 0;
    };
}
