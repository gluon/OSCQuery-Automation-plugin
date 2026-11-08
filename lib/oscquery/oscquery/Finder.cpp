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

#include <oscquery/Finder.h>

#include <oscquery/net/HttpGet.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>

namespace oqa::oscquery
{
    using Clock = std::chrono::steady_clock;

    struct Finder::Impl
    {
        Options options;
        Discovery discovery;
        std::uint64_t discoveryRevision = 0;

        //  The entries, by host:port; the worker's own except where noted.
        struct Entry
        {
            Candidate candidate;
            Clock::time_point lastProbe {};
            bool probeWanted = true;
        };

        std::map<std::string, Entry> entries;

        //  What callers add and remove, taken by the worker on its tick.
        std::mutex requests;
        std::vector<std::pair<std::string, int>> toAdd;
        std::vector<std::pair<std::string, int>> toRemove;
        std::atomic<bool> probeAll { false };

        //  What callers read.
        mutable std::mutex published;
        std::vector<Candidate> list;
        std::atomic<std::uint64_t> revision { 0 };

        std::thread worker;
        std::atomic<bool> running { false };
        std::atomic<bool> stopping { false };

        //==============================================================================
        void run()
        {
            while (! stopping.load())
            {
                bool changed = false;

                changed |= takeRequests();
                changed |= takeFound();
                changed |= probeDue();

                if (changed)
                    publish();

                std::this_thread::sleep_for (std::chrono::milliseconds (std::max (10, options.tickMs)));
            }
        }

        bool takeRequests()
        {
            std::vector<std::pair<std::string, int>> add, remove;

            {
                const std::lock_guard<std::mutex> guard (requests);
                add.swap (toAdd);
                remove.swap (toRemove);
            }

            bool changed = false;

            for (const auto& [host, port] : add)
            {
                Candidate c;
                c.host = host;
                c.port = port;
                c.manual = true;
                c.name = c.key();

                auto& entry = entries[c.key()];

                if (entry.candidate.port == 0)
                    entry.candidate = std::move (c);
                else
                    entry.candidate.manual = true;

                entry.probeWanted = true;
                changed = true;
            }

            for (const auto& [host, port] : remove)
            {
                const auto key = host + ":" + std::to_string (port);
                const auto it = entries.find (key);

                if (it == entries.end())
                    continue;

                it->second.candidate.manual = false;

                if (! it->second.candidate.advertised)
                    entries.erase (it);

                changed = true;
            }

            if (probeAll.exchange (false))
            {
                for (auto& entry : entries)
                    entry.second.probeWanted = true;
            }

            return changed;
        }

        bool takeFound()
        {
            if (! options.browse)
                return false;

            const auto now = discovery.revision();

            if (now == discoveryRevision)
                return false;

            discoveryRevision = now;

            std::map<std::string, Discovery::Found> found;

            for (auto& each : discovery.found())
                found[each.host + ":" + std::to_string (each.queryPort)] = std::move (each);

            bool changed = false;

            for (auto& [key, each] : found)
            {
                auto& entry = entries[key];
                auto& c = entry.candidate;

                if (c.port == 0)
                {
                    c.host = each.host;
                    c.port = each.queryPort;
                    c.name = each.name;
                    entry.probeWanted = true;
                    changed = true;
                }

                if (! c.advertised)
                {
                    c.advertised = true;
                    changed = true;
                }

                if (! c.reachable && c.name != each.name)
                {
                    c.name = each.name;
                    changed = true;
                }

                if (c.txt != each.txt)
                {
                    c.txt = each.txt;
                    changed = true;
                }
            }

            for (auto it = entries.begin(); it != entries.end();)
            {
                auto& c = it->second.candidate;

                if (c.advertised && found.find (it->first) == found.end())
                {
                    c.advertised = false;
                    changed = true;

                    //  Forgotten by DNS-SD, never typed, not answering: gone.
                    if (! c.manual && ! c.reachable)
                    {
                        it = entries.erase (it);
                        continue;
                    }
                }

                ++it;
            }

            return changed;
        }

        bool probeDue()
        {
            const auto now = Clock::now();
            const auto interval = std::chrono::milliseconds (std::max (0, options.reprobeMs));
            bool changed = false;

            for (auto it = entries.begin(); it != entries.end();)
            {
                if (stopping.load())
                    return changed;

                auto& entry = it->second;

                const bool due = entry.probeWanted
                              || (! entry.candidate.probed)
                              || (interval.count() > 0 && now - entry.lastProbe >= interval);

                if (! due)
                {
                    ++it;
                    continue;
                }

                entry.probeWanted = false;
                entry.lastProbe = Clock::now();
                probe (entry.candidate);
                changed = true;

                //  Unreachable, forgotten by DNS-SD and never typed: nobody wants it listed.
                if (! entry.candidate.reachable && ! entry.candidate.advertised && ! entry.candidate.manual)
                {
                    it = entries.erase (it);
                    continue;
                }

                ++it;
            }

            return changed;
        }

        void probe (Candidate& c)
        {
            const auto reply = net::httpGet (c.host, c.port, "/", "HOST_INFO", options.httpTimeoutMs);
            c.probed = true;

            if (! reply.ok || reply.status != 200)
            {
                c.reachable = false;
                c.error = reply.ok ? "HOST_INFO answered " + std::to_string (reply.status) : reply.error;
                return;
            }

            std::string error;
            auto info = parseHostInfo (reply.body, error);

            if (! info.has_value())
            {
                c.reachable = false;
                c.error = error;
                return;
            }

            c.reachable = true;
            c.error.clear();
            c.lastSeen = Clock::now();

            if (! info->name.empty())
                c.name = info->name;

            c.hostInfo = std::move (*info);
        }

        void publish()
        {
            std::vector<Candidate> fresh;
            fresh.reserve (entries.size());

            for (const auto& entry : entries)
                fresh.push_back (entry.second.candidate);

            std::sort (fresh.begin(), fresh.end(), [] (const Candidate& a, const Candidate& b)
                       {
                           if (a.name != b.name) return a.name < b.name;
                           if (a.host != b.host) return a.host < b.host;
                           return a.port < b.port;
                       });

            {
                const std::lock_guard<std::mutex> guard (published);
                list = std::move (fresh);
            }

            revision.fetch_add (1, std::memory_order_release);
        }
    };

    //==============================================================================
    Finder::Finder() : impl (std::make_unique<Impl>()) {}
    Finder::~Finder() { stop(); }

    bool Finder::start (Options options)
    {
        if (impl->running.load())
            return false;

        impl->options = std::move (options);
        impl->entries.clear();
        impl->discoveryRevision = 0;

        if (impl->options.browse && ! impl->discovery.start (impl->options.discovery))
            return false;

        impl->stopping.store (false);
        impl->running.store (true);
        impl->worker = std::thread ([this] { impl->run(); });
        return true;
    }

    void Finder::stop()
    {
        if (! impl->running.load())
            return;

        impl->stopping.store (true);

        if (impl->worker.joinable())
            impl->worker.join();

        impl->discovery.stop();
        impl->running.store (false);
    }

    bool Finder::isRunning() const noexcept { return impl->running.load(); }

    void Finder::addManual (const std::string& host, int port)
    {
        const std::lock_guard<std::mutex> guard (impl->requests);
        impl->toAdd.emplace_back (host, port);
    }

    bool Finder::removeManual (const std::string& host, int port)
    {
        const std::lock_guard<std::mutex> guard (impl->requests);
        impl->toRemove.emplace_back (host, port);
        return true;
    }

    void Finder::refresh()
    {
        if (impl->options.browse)
            impl->discovery.browse();

        impl->probeAll.store (true);
    }

    std::vector<Finder::Candidate> Finder::candidates() const
    {
        const std::lock_guard<std::mutex> guard (impl->published);
        return impl->list;
    }

    std::string Finder::browseMode() const    { return impl->options.browse ? impl->discovery.mode() : "off"; }
    std::string Finder::browseProblem() const { return impl->options.browse ? impl->discovery.problem() : std::string(); }

    std::uint64_t Finder::revision() const noexcept
    {
        return impl->revision.load (std::memory_order_acquire);
    }
}
