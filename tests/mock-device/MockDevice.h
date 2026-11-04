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
    THE MOCK OSCQUERY DEVICE (devplan, Phase 0): a tiny server that every later
    phase's tests drive instead of a real device, and the conformance CLI's first
    subject.

    It serves a tree loaded from a JSON fixture, verbatim in the shape a device
    answers `GET /` with, over HTTP and WebSocket on one port (juce_simpleweb):

        GET /?HOST_INFO          NAME, OSC_PORT (a real bound UDP port),
                                 OSC_TRANSPORT, WS_PORT, EXTENSIONS
        GET /                    the whole tree
        GET /<path>              that node and its descendants, else 404
        GET /<path>?<ATTRIBUTE>  200 with {"ATTRIBUTE": ...}; 204 when the node
                                 lacks it (or VALUE on a write-only node);
                                 400 for an attribute the proposal does not have

    Over the websocket it takes LISTEN and IGNORE, and a test changes the device
    under the client's feet with setValue, rename, remove, retype and add, which
    stream the value or send PATH_RENAMED / PATH_REMOVED / PATH_CHANGED /
    PATH_ADDED, each only if Options::extensions lists it.

    It knows no namespace: every address it serves comes from the fixture.
    Thread-safe: requests are answered on the server's own thread, controls are
    called from the test's, and the tree is behind one mutex.
*/

#include <juce_core/juce_core.h>

#include <memory>

namespace oqatest
{
    class MockDevice
    {
    public:
        struct Options
        {
            /** HOST_INFO's NAME. */
            juce::String name { "Mock OSCQuery device" };

            /** What HOST_INFO's EXTENSIONS lists. A websocket feature or
                notification not listed here is not served or sent. */
            juce::StringArray extensions { "ACCESS", "VALUE", "RANGE", "DESCRIPTION", "TYPE",
                                           "UNIT", "CLIPMODE", "CRITICAL", "LISTEN",
                                           "PATH_CHANGED", "PATH_ADDED", "PATH_REMOVED",
                                           "PATH_RENAMED" };

            /** False: a device without websocket (optional in the proposal).
                Upgrades are closed at once and HOST_INFO has no WS_PORT. */
            bool websocket = true;

            /** The HTTP port to bind; 0 lets the system choose, which is what
                lets tests run in parallel. */
            int httpPort = 0;
        };

        MockDevice();
        explicit MockDevice (Options options);
        ~MockDevice();

        /** Loads a tree in `GET /` shape. False, and the tree unchanged, if it is
            not a JSON object. */
        bool loadTree (const juce::var& root);
        bool loadTreeFromFile (const juce::File& file);

        /** Binds HTTP/WebSocket and the UDP port behind OSC_PORT, and returns once
            the server answers. False if either cannot be bound. */
        bool start();
        void stop();
        bool isRunning() const;

        int httpPort() const;
        int oscPort() const;

        const Options& options() const;

        /** What `GET /?HOST_INFO` answers. */
        juce::var hostInfo() const;

        /** A deep copy of the node at `path` ("/" for the root), or void. */
        juce::var node (const juce::String& path) const;

        //==============================================================================
        //  Controls for tests. Each returns false, and changes nothing, when the
        //  path it needs does not exist (or, for rename and add, when the target
        //  already does).

        /** Sets VALUE and sends one binary OSC message to every client that
            LISTENs to `path`. One element per argument of the node's TYPE. */
        bool setValue (const juce::String& path, const juce::Array<juce::var>& values);

        /** Moves a node, rewriting FULL_PATH down its subtree; PATH_RENAMED. */
        bool rename (const juce::String& oldPath, const juce::String& newPath);

        /** Removes a node and its subtree; PATH_REMOVED. */
        bool remove (const juce::String& path);

        /** Changes a leaf's TYPE; PATH_CHANGED. */
        bool retype (const juce::String& path, const juce::String& newType);

        /** Adds `node` at `path` under an existing container, setting FULL_PATH
            down its subtree; PATH_ADDED. */
        bool add (const juce::String& path, const juce::var& node);

        /** How many websocket connections LISTEN to `path` right now. */
        int listenerCount (const juce::String& path) const;

        /** How many websocket connections are open. */
        int connectionCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE (MockDevice)
    };
}
