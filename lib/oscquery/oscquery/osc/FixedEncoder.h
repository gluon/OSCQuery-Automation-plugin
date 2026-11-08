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
    OSC ENCODED INTO A BUFFER THE CALLER OWNS, with nothing allocated and
    nothing thrown: the encoder a realtime thread may call (devplan Phase 3:
    "the optional direct sendto from the audio thread", and the network
    thread's own bundles). Codec.h's encode() returns a vector and is for
    everyone else; the two agree byte for byte, and the tests say so.

    A MESSAGE is address, type tags, arguments, each padded to four. A BUNDLE
    is "#bundle", a time tag, then each message behind its byte count
    (BundleWriter). What does not fit is not written: add() answers false and
    the buffer is as it was, so a caller splits at the MTU by trying.

    REFUSED, as Codec.h refuses: a malformed address, a string with a NUL in
    it, a non-finite float, a value OSC has no tag for.
*/

#include <oscquery/osc/Value.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oqa::oscquery::osc
{
    /*  One message into [out, out + capacity). The bytes written, or 0 when it
        does not fit or cannot be encoded; nothing past the answer is touched. */
    std::size_t encodeMessageInto (std::uint8_t* out, std::size_t capacity,
                                   std::string_view address,
                                   const Value* args, std::size_t argCount) noexcept;

    /** The bytes a message would take, or 0 when it cannot be encoded. */
    std::size_t encodedMessageSize (std::string_view address, const Value* args, std::size_t argCount) noexcept;

    //==============================================================================
    /** A bundle built in place, message by message. */
    class BundleWriter
    {
    public:
        static constexpr std::size_t headerBytes = 16;      ///< "#bundle\0" and the time tag
        static constexpr std::size_t elementOverhead = 4;   ///< each message's byte count

        BundleWriter (std::uint8_t* out, std::size_t capacity) noexcept;

        /** Start over: the header, with this time tag. False when even that does not fit. */
        bool begin (TimeTag when) noexcept;

        /*  Append one message behind its byte count. False, and the buffer
            unchanged, when it does not fit or cannot be encoded. */
        bool add (std::string_view address, const Value* args, std::size_t argCount) noexcept;

        const std::uint8_t* data() const noexcept { return buffer; }
        std::size_t size() const noexcept         { return used; }
        std::size_t capacity() const noexcept     { return room; }
        std::size_t count() const noexcept        { return messages; }
        bool empty() const noexcept               { return messages == 0; }

    private:
        std::uint8_t* buffer = nullptr;
        std::size_t room = 0;
        std::size_t used = 0;
        std::size_t messages = 0;
    };
}
