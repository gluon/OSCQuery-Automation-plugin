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
    ONE DEVICE, AS THIS CLIENT SEES IT: its HOST_INFO, its tree, and a
    websocket that streams values and tree changes while it is up.

        Unprobed ──probe──▶ Reachable ──fetchTree──▶ TreeLoaded ──ws open──▶ Listening
            │                   │                                               │
            └──▶ Unreachable    └──(no websocket offered: stays here)     closed/error
                     ▲                                                          │
                     └────────── retried with backoff ◀──── Reconnecting ◀──────┘

    WHAT THE DEVICE SAYS GOES. Nothing optional is assumed: the websocket is
    opened only when HOST_INFO's EXTENSIONS lists LISTEN or a PATH_* notification
    (spec draft C1), and listen() answers `unsupported` on a device that does not
    list LISTEN. One leniency, recorded: a notification that ARRIVES is handled
    whether or not it was listed, because WFS-DIY sends PATH_CHANGED without
    listing it, and Phase 2 must cope with WFS-DIY exactly as it is. Nothing is
    ever SENT that the device did not list.

    THREADS. Device owns one worker thread. The websocket client's own thread
    hands frames to it through a queue, so every callback but one fires on the
    Device's thread: `onState` fires on whichever thread changed the state,
    which for probe(), fetchTree() and the other blocking calls is the caller's.
    Callers marshal to their own thread (the editor to the message thread,
    Phase 3's registry to its queue); none of this touches the audio thread.
    No juce::Timer and no MessageManager anywhere: a headless tool runs it.

    THE BLOCKING CALLS block for up to Options::httpTimeoutMs each and never
    throw. A device that has gone away takes the whole timeout to say so.

    RECONNECT, with backoff from reconnectInitialMs doubling to reconnectMaxMs:
    on a closed or broken websocket, HOST_INFO is probed again (the device may
    have restarted on other ports), the tree is fetched again unless it is
    large, the websocket reopened, every subscription re-sent and its value
    re-read over HTTP, because a device need not push a value on LISTEN
    (WFS-DIY does not). juce_simpleweb's client closes an idle connection after
    1000 s on its own; the same path covers that.

    LARGE TREES, as far as OSCQuery allows. The proposal has no depth-limit
    field and `GET /` always returns the whole tree, so "lazy" can only mean:
    the tree is fetched once; a tree above Options::largeTreeThreshold nodes
    is never fetched whole again (not on reconnect either), and every later
    change arrives by subtree GET of the path a notification names. A caller
    may also mark containers unloaded and fetch them one at a time with
    fetchSubtree(). The PRD's "device advertises a depth limit" has no field to
    read, and is noted as such in the devplan record.
*/

#include <oscquery/HostInfo.h>
#include <oscquery/osc/Codec.h>
#include <oscquery/tree/Tree.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace oqa::oscquery
{
    class Device
    {
    public:
        struct Options
        {
            /** The HTTP server: a dotted address or a host name, and its port. */
            std::string host;
            int port = 0;

            int httpTimeoutMs = 3000;

            /** Above this many nodes the tree is never fetched whole again. */
            std::size_t largeTreeThreshold = 5000;

            int reconnectInitialMs = 500;
            int reconnectMaxMs = 30000;

            /** False: never open a websocket, even when the device offers one. */
            bool openWebsocket = true;

            /** On reconnect, fetch the tree again unless it is large. */
            bool refetchTreeOnReconnect = true;
        };

        enum class State
        {
            Unprobed,       ///< nothing asked yet
            Unreachable,    ///< no HOST_INFO answer; retried while started
            Reachable,      ///< HOST_INFO answered; no tree yet
            TreeLoaded,     ///< the tree is in; no websocket (not offered, not wanted, or not yet open)
            Listening,      ///< the websocket is open
            Reconnecting,   ///< the websocket closed or broke; coming back with backoff
            Closed          ///< stop() was called
        };

        static const char* stateName (State state) noexcept;

        struct Stats
        {
            std::uint64_t valuesReceived = 0;      ///< OSC messages delivered to onValue
            std::uint64_t framesRefused = 0;       ///< binary frames the codec refused
            std::uint64_t notifications = 0;       ///< PATH_* commands handled
            std::uint64_t unknownCommands = 0;     ///< text frames with a COMMAND this does not know
            std::uint64_t reconnects = 0;          ///< websocket reopenings after a close or error
        };

        struct Callbacks
        {
            /** A value streamed by the device, one call per OSC message, bundles unpacked. */
            std::function<void (const std::string& path, const osc::Values& values)> onValue;

            std::function<void (State state)> onState;

            /** The subtree at `path` was replaced (PATH_ADDED, PATH_CHANGED, a re-fetched tree). */
            std::function<void (const std::string& path)> onTreeChanged;
            std::function<void (const std::string& path)> onPathRemoved;
            std::function<void (const std::string& oldPath, const std::string& newPath)> onPathRenamed;

            /** Something went wrong that did not change the state: a refused frame, a failed re-read. */
            std::function<void (const std::string& message)> onError;
        };

        enum class Listen
        {
            accepted,       ///< in the subscription set; on the wire now or when the websocket opens
            unsupported     ///< the device does not list LISTEN
        };

        //==============================================================================
        explicit Device (Options options);
        ~Device();

        Device (const Device&) = delete;
        Device& operator= (const Device&) = delete;

        /** Set before start(). */
        void setCallbacks (Callbacks callbacks);

        const Options& options() const noexcept;

        //==============================================================================
        //  Blocking, on the caller's thread.

        /** GET /?HOST_INFO. False, and Unreachable with a reason, when it does not answer. */
        bool probe();

        /** GET /. False with a reason when it fails; the device stays Reachable. */
        bool fetchTree();

        /** GET /<path>, merged into the tree. False when the path is gone or the parent unknown. */
        bool fetchSubtree (const std::string& path);

        /** GET /<path>?VALUE, typed by the tree's TYPE for that path. nullopt unless 200. */
        std::optional<osc::Values> readValue (const std::string& path);

        //==============================================================================
        //  The worker: probes if needed, fetches the tree, opens the websocket when
        //  offered, and keeps it open.
        void start();
        void stop();
        bool isStarted() const noexcept;

        /*  Subscriptions. The set is the truth; whether it is on the wire right
            now is state() == Listening. Re-sent on every (re)open. */
        Listen listen (const std::string& path);
        void ignore (const std::string& path);
        std::set<std::string> subscriptions() const;

        /*  One OSC packet as a binary websocket frame, for CRITICAL leaves on a
            device whose OSC_TRANSPORT is not TCP (spec draft L4). False, with a
            reason, when the packet cannot be encoded or the websocket is not open. */
        bool sendOsc (const osc::Packet& packet, std::string& error);

        //==============================================================================
        //  Snapshots, from any thread.
        State state() const;
        std::optional<HostInfo> hostInfo() const;
        bool treeLoaded() const;
        bool treeIsLarge() const;
        Tree tree() const;
        void withTree (const std::function<void (const Tree&)>& read) const;
        std::string lastError() const;
        Stats stats() const;

        /** Moves whenever state, host info, tree, stats or subscriptions change. */
        std::uint64_t revision() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
