/*
    This file is part of Go.dot — https://github.com/pob31/go.dot

    Copyright (C) 2026 Pierre-Olivier Boulant

    Go.dot is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version. Go.dot is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    (LICENSE, at the repository root) for more details.

    SPDX-License-Identifier: GPL-3.0-or-later

    Origin: go.dot tests/OscQueryTests.cpp at 1c8aaa0a (struct Client), as a
    header, with sendText, the frames copied out, and waitUntil beside it.
*/

#pragma once

/*
    A websocket client for tests: connects to 127.0.0.1:<port>, records every
    text and binary frame it receives under a mutex, and sends text frames.
*/

#include <juce_core/juce_core.h>
#include <juce_simpleweb/juce_simpleweb.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace oqatest
{
    /** Polls `predicate` every 5 ms until it holds or `milliseconds` pass. */
    inline bool waitUntil (const std::function<bool()>& predicate, int milliseconds = 10000)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (milliseconds);

        while (! predicate())
        {
            if (juce::Time::getMillisecondCounter() >= deadline)
                return predicate();

            juce::Thread::sleep (5);
        }

        return true;
    }

    struct WsTestClient final : public SimpleWebSocketClient::Listener
    {
        SimpleWebSocketClient socket;

        std::mutex mutex;
        std::vector<juce::String> text;
        std::vector<std::vector<std::uint8_t>> binary;
        std::atomic<bool> open { false };

        WsTestClient() { socket.addWebSocketListener (this); }
        ~WsTestClient() override { socket.removeWebSocketListener (this); socket.stop(); }

        void connectionOpened() override { open = true; }

        void messageReceived (const juce::String& message) override
        {
            const std::lock_guard<std::mutex> lock { mutex };
            text.push_back (message);
        }

        void dataReceived (const juce::MemoryBlock& data) override
        {
            const std::lock_guard<std::mutex> lock { mutex };
            const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
            binary.emplace_back (bytes, bytes + data.getSize());
        }

        /** "host:port" with no ws:// prefix: juce_simpleweb's client wants it so. */
        bool connect (int port, int milliseconds = 10000)
        {
            socket.start ("127.0.0.1:" + juce::String (port));
            return waitUntil ([this] { return open.load(); }, milliseconds);
        }

        void sendText (const juce::String& message) { socket.send (message); }

        std::vector<juce::String> textFrames()
        {
            const std::lock_guard<std::mutex> lock { mutex };
            return text;
        }

        std::vector<std::vector<std::uint8_t>> binaryFrames()
        {
            const std::lock_guard<std::mutex> lock { mutex };
            return binary;
        }

        std::size_t textCount()
        {
            const std::lock_guard<std::mutex> lock { mutex };
            return text.size();
        }

        std::size_t binaryCount()
        {
            const std::lock_guard<std::mutex> lock { mutex };
            return binary.size();
        }
    };
}
