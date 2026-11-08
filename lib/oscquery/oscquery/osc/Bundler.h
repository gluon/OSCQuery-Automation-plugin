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
    MESSAGES INTO BUNDLES THAT FIT A DATAGRAM (PRD, Transport: "One bundle per
    device per audio block ... Bundles stay under the MTU (about 1,400 bytes,
    40 to 45 three-float messages) and split beyond that").

    Pure: messages in, datagrams out, in order. Every datagram is a bundle,
    even one holding a single message, so the time tag is always there for a
    device that honours it (the PRD's per-device setting) and a device that does
    not reads the immediate tag and applies the messages together.

    A message that cannot fit a bundle on its own is refused by index rather
    than sent on its own as a datagram the network would fragment; the caller
    decides what to do with a message that large, which in this plugin is
    nothing, because a slot's message is three floats at most.
*/

#include <oscquery/osc/Codec.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oqa::oscquery::osc
{
    /** What a bundle costs on the wire before any message: "#bundle\0" and the time tag. */
    inline constexpr std::size_t bundleHeaderBytes = 16;

    /** What each element costs beyond its own bytes: its size word. */
    inline constexpr std::size_t bundleElementOverhead = 4;

    /** The default ceiling for one datagram, bundle header included. */
    inline constexpr std::size_t defaultMtu = 1400;

    struct Bundled
    {
        /** Encoded bundles, each at most `mtu` bytes, messages in the order given. */
        std::vector<std::vector<std::uint8_t>> datagrams;

        /** Indices into the input of messages that could not fit a bundle alone, or would not encode. */
        std::vector<std::size_t> refused;

        /** Why each refused message was, in the same order. */
        std::vector<std::string> reasons;
    };

    /*  Packs `messages` into bundles of at most `mtu` bytes, time tag `when` on
        each. A message that is itself a bundle is flattened into its messages
        (its own time tag is lost; this is the sender's clock, not the
        caller's). */
    Bundled bundle (const std::vector<Packet>& messages, TimeTag when, std::size_t mtu = defaultMtu);

    /** The bytes of one bundle built from already encoded messages, no size check. */
    std::vector<std::uint8_t> bundleOf (const std::vector<std::vector<std::uint8_t>>& encodedMessages, TimeTag when);
}
