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

#include <oscquery/osc/FixedEncoder.h>

#include <oscquery/osc/Address.h>

#include <cmath>
#include <cstring>

namespace oqa::oscquery::osc
{
    namespace
    {
        constexpr std::size_t padded (std::size_t n) noexcept
        {
            return (n + 3) & ~static_cast<std::size_t> (3);
        }

        /*  A cursor over the caller's buffer. Every put checks the room first;
            a put that would not fit sets `failed` and writes nothing, so the
            caller's bytes up to the last good put are what they were. */
        struct Cursor
        {
            std::uint8_t* out;
            std::size_t capacity;
            std::size_t pos = 0;
            bool failed = false;

            bool room (std::size_t n) noexcept
            {
                if (failed || pos + n > capacity)
                    return failed = true, false;

                return true;
            }

            void putU32 (std::uint32_t v) noexcept
            {
                if (! room (4)) return;
                out[pos++] = static_cast<std::uint8_t> ((v >> 24) & 0xff);
                out[pos++] = static_cast<std::uint8_t> ((v >> 16) & 0xff);
                out[pos++] = static_cast<std::uint8_t> ((v >> 8) & 0xff);
                out[pos++] = static_cast<std::uint8_t> (v & 0xff);
            }

            void putU64 (std::uint64_t v) noexcept
            {
                putU32 (static_cast<std::uint32_t> (v >> 32));
                putU32 (static_cast<std::uint32_t> (v & 0xffffffffu));
            }

            /** text, NUL, pad to four. False on a NUL inside the text. */
            bool putString (std::string_view text) noexcept
            {
                if (text.find ('\0') != std::string_view::npos)
                    return failed = true, false;

                const auto total = padded (text.size() + 1);

                if (! room (total))
                    return false;

                std::memcpy (out + pos, text.data(), text.size());
                std::memset (out + pos + text.size(), 0, total - text.size());
                pos += total;
                return true;
            }

            bool putBytes (const std::uint8_t* bytes, std::size_t n) noexcept
            {
                const auto total = padded (n);

                if (! room (total))
                    return false;

                std::memcpy (out + pos, bytes, n);
                std::memset (out + pos + n, 0, total - n);
                pos += total;
                return true;
            }
        };

        std::uint32_t bitsOf (float f) noexcept
        {
            std::uint32_t bits;
            std::memcpy (&bits, &f, sizeof bits);
            return bits;
        }

        std::uint64_t bitsOf (double d) noexcept
        {
            std::uint64_t bits;
            std::memcpy (&bits, &d, sizeof bits);
            return bits;
        }

        bool putArgument (Cursor& c, const Value& value) noexcept
        {
            if (value.isInt32())   { c.putU32 (static_cast<std::uint32_t> (value.getInt32())); return ! c.failed; }
            if (value.isInt64())   { c.putU64 (static_cast<std::uint64_t> (value.getInt64())); return ! c.failed; }
            if (value.isBool() || value.isNil() || value.isImpulse()) return true;

            if (value.isFloat32())
            {
                if (! std::isfinite (value.getFloat32())) return c.failed = true, false;
                c.putU32 (bitsOf (value.getFloat32()));
                return ! c.failed;
            }

            if (value.isFloat64())
            {
                if (! std::isfinite (value.getFloat64())) return c.failed = true, false;
                c.putU64 (bitsOf (value.getFloat64()));
                return ! c.failed;
            }

            if (value.isString())  return c.putString (value.getString());
            if (value.isTimeTag()) { c.putU64 (value.getTimeTag().raw); return ! c.failed; }

            if (value.isBlob())
            {
                const auto& bytes = value.getBlob().bytes;

                if (bytes.size() > 0x7fffffffu) return c.failed = true, false;

                c.putU32 (static_cast<std::uint32_t> (bytes.size()));
                return ! c.failed && c.putBytes (bytes.data(), bytes.size());
            }

            return c.failed = true, false;
        }

        /*  The message into the cursor; false and `failed` when it cannot be. */
        bool putMessage (Cursor& c, std::string_view address, const Value* args, std::size_t argCount) noexcept
        {
            if (! isValidAddress (address))
                return c.failed = true, false;

            if (! c.putString (address))
                return false;

            /*  The type tag string, written in place: ',' then one tag per
                argument, NUL, pad. Done by hand so no string is built. */
            const auto tagBytes = padded (1 + argCount + 1);

            if (! c.room (tagBytes))
                return false;

            c.out[c.pos] = ',';

            for (std::size_t i = 0; i < argCount; ++i)
                c.out[c.pos + 1 + i] = static_cast<std::uint8_t> (args[i].typeTag());

            std::memset (c.out + c.pos + 1 + argCount, 0, tagBytes - 1 - argCount);
            c.pos += tagBytes;

            for (std::size_t i = 0; i < argCount; ++i)
                if (! putArgument (c, args[i]))
                    return false;

            return true;
        }
    }

    //==============================================================================
    std::size_t encodeMessageInto (std::uint8_t* out, std::size_t capacity,
                                   std::string_view address,
                                   const Value* args, std::size_t argCount) noexcept
    {
        if (out == nullptr || (argCount > 0 && args == nullptr))
            return 0;

        Cursor c { out, capacity };
        return putMessage (c, address, args, argCount) ? c.pos : 0;
    }

    std::size_t encodedMessageSize (std::string_view address, const Value* args, std::size_t argCount) noexcept
    {
        if (! isValidAddress (address) || (argCount > 0 && args == nullptr))
            return 0;

        std::size_t total = padded (address.size() + 1) + padded (1 + argCount + 1);

        for (std::size_t i = 0; i < argCount; ++i)
        {
            const auto& v = args[i];

            if (v.isInt32() || v.isFloat32())              total += 4;
            else if (v.isInt64() || v.isFloat64()
                     || v.isTimeTag())                     total += 8;
            else if (v.isBool() || v.isNil() || v.isImpulse()) {}
            else if (v.isString())                         total += padded (v.getString().size() + 1);
            else if (v.isBlob())                           total += 4 + padded (v.getBlob().bytes.size());
            else                                           return 0;
        }

        return total;
    }

    //==============================================================================
    BundleWriter::BundleWriter (std::uint8_t* out, std::size_t capacityBytes) noexcept
        : buffer (out), room (out == nullptr ? 0 : capacityBytes)
    {
    }

    bool BundleWriter::begin (TimeTag when) noexcept
    {
        used = 0;
        messages = 0;

        if (room < headerBytes)
            return false;

        static constexpr std::uint8_t marker[8] = { '#', 'b', 'u', 'n', 'd', 'l', 'e', 0 };
        std::memcpy (buffer, marker, 8);

        Cursor c { buffer, room, 8 };
        c.putU64 (when.raw);
        used = c.pos;
        return true;
    }

    bool BundleWriter::add (std::string_view address, const Value* args, std::size_t argCount) noexcept
    {
        if (used < headerBytes)
            return false;                           // begin() first

        Cursor c { buffer, room, used + elementOverhead };

        if (c.pos > room || ! putMessage (c, address, args, argCount))
            return false;

        const auto messageBytes = c.pos - used - elementOverhead;

        Cursor count { buffer, room, used };
        count.putU32 (static_cast<std::uint32_t> (messageBytes));

        used = c.pos;
        ++messages;
        return true;
    }
}
