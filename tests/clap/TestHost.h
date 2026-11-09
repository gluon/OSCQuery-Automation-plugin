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
    A CLAP HOST FOR THE TESTS (devplan Phase 5b): loads the .clap this build
    makes, the way a DAW does (the system's loader, clap_entry, the factory),
    and offers the plugin the host extensions it uses, every call recorded:
    params (rescan flags, cleared ids, flush requests), timer support, posix
    fd support, thread check, gui. The test is the main thread; a block is
    processed inside an AudioThreadScope, which is what the thread check
    answers.

    The module is loaded once per process and never unloaded, as the loader
    never closes the core library: a plugin's registry threads must not
    outlive their code. OQA_CORE_LIBRARY is set before the load so the
    module's loader takes the shared core this build made, and the plugin
    and the test share one registry.
*/

#include <clap/clap.h>

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <dlfcn.h>
#endif

namespace oqatest::claphost
{
    //==============================================================================
    /** The built module, loaded once: its entry and factory. */
    struct LoadedModule
    {
        const clap_plugin_entry_t* entry = nullptr;
        const clap_plugin_factory_t* factory = nullptr;
        std::string path;
        std::string error;

        static const LoadedModule& get()
        {
            static const LoadedModule module = load();
            return module;
        }

    private:
        static void setEnvironment (const char* name, const char* value)
        {
          #if defined (_WIN32)
            _putenv_s (name, value);
          #else
            setenv (name, value, 1);
          #endif
        }

        static LoadedModule load()
        {
            LoadedModule out;
            out.path = OQA_CLAP_PATH;

            setEnvironment ("OQA_CORE_LIBRARY", OQA_CORE_SHARED_PATH);

          #if defined (_WIN32)
            auto* handle = LoadLibraryW (juce::String (out.path).toWideCharPointer());
            if (handle == nullptr) { out.error = "LoadLibrary failed: " + std::to_string (GetLastError()); return out; }
            auto* symbol = reinterpret_cast<void*> (GetProcAddress (handle, "clap_entry"));
          #else
            auto* handle = dlopen (out.path.c_str(), RTLD_LOCAL | RTLD_NOW);
            if (handle == nullptr) { out.error = std::string ("dlopen failed: ") + dlerror(); return out; }
            auto* symbol = dlsym (handle, "clap_entry");
          #endif

            if (symbol == nullptr)
            {
                out.error = "no clap_entry in " + out.path;
                return out;
            }

            out.entry = static_cast<const clap_plugin_entry_t*> (symbol);

            if (! out.entry->init (out.path.c_str()))
            {
                out.error = "clap_entry.init refused";
                out.entry = nullptr;
                return out;
            }

            out.factory = static_cast<const clap_plugin_factory_t*> (out.entry->get_factory (CLAP_PLUGIN_FACTORY_ID));

            if (out.factory == nullptr)
                out.error = "no plugin factory";

            return out;
        }
    };

    //==============================================================================
    /** The host the plugin sees: every extension it may ask for, every call recorded. */
    class RecordingHost
    {
    public:
        RecordingHost()
        {
            host.clap_version = CLAP_VERSION_INIT;
            host.host_data = this;
            host.name = "oqa test host";
            host.vendor = "Pix et Bel";
            host.url = "https://github.com/pob31/OSCQuery-Automation-plugin";
            host.version = "0";
            host.get_extension = getExtension;
            host.request_restart = [] (const clap_host_t* h) { ++self (h).restartRequests; };
            host.request_process = [] (const clap_host_t* h) { ++self (h).processRequests; };
            host.request_callback = [] (const clap_host_t* h) { ++self (h).callbackRequests; };
        }

        const clap_host_t* clapHost() const noexcept { return &host; }

        //  What the test may switch off before creating a plugin.
        bool offerParams = true, offerTimer = true, offerPosixFd = true, offerThreadCheck = true, offerGui = true;

        //  Recorded.
        std::vector<clap_param_rescan_flags> rescans;
        std::vector<std::pair<clap_id, clap_param_clear_flags>> clears;
        std::atomic<int> flushRequests { 0 };
        std::atomic<int> restartRequests { 0 }, processRequests { 0 }, callbackRequests { 0 };

        struct Timer { clap_id id; std::uint32_t periodMs; };
        std::vector<Timer> timers;

        struct Fd { int fd; clap_posix_fd_flags_t flags; };
        std::vector<Fd> fds;

        std::vector<std::pair<std::uint32_t, std::uint32_t>> resizeRequests;
        int guiClosed = 0;

        std::mutex recordLock;

        /** Call `clap_plugin.process` inside one: the thread check answers "audio" there. */
        struct AudioThreadScope
        {
            explicit AudioThreadScope (RecordingHost& h) : host (h) { host.audioThread = std::this_thread::get_id(); }
            ~AudioThreadScope() { host.audioThread = std::thread::id(); }
            RecordingHost& host;
        };

        //  Convenience: the recorded rescans as one OR, and a reset.
        clap_param_rescan_flags rescanFlags() const
        {
            clap_param_rescan_flags all = 0;
            for (const auto f : rescans) all |= f;
            return all;
        }

        void forget()
        {
            const std::lock_guard<std::mutex> guard (recordLock);
            rescans.clear();
            clears.clear();
            flushRequests = 0;
            resizeRequests.clear();
        }

        void runTimers (const clap_plugin_t* plugin)
        {
            if (const auto* timer = static_cast<const clap_plugin_timer_support_t*> (plugin->get_extension (plugin, CLAP_EXT_TIMER_SUPPORT)))
                for (const auto& t : timers)
                    timer->on_timer (plugin, t.id);
        }

    private:
        static RecordingHost& self (const clap_host_t* h) { return *static_cast<RecordingHost*> (h->host_data); }

        static const void* getExtension (const clap_host_t* h, const char* id)
        {
            auto& me = self (h);

            if (std::strcmp (id, CLAP_EXT_PARAMS) == 0 && me.offerParams)                 return &me.paramsExt;
            if (std::strcmp (id, CLAP_EXT_TIMER_SUPPORT) == 0 && me.offerTimer)           return &me.timerExt;
            if (std::strcmp (id, CLAP_EXT_POSIX_FD_SUPPORT) == 0 && me.offerPosixFd)      return &me.fdExt;
            if (std::strcmp (id, CLAP_EXT_THREAD_CHECK) == 0 && me.offerThreadCheck)      return &me.threadExt;
            if (std::strcmp (id, CLAP_EXT_GUI) == 0 && me.offerGui)                       return &me.guiExt;

            return nullptr;
        }

        clap_host_t host {};

        std::thread::id mainThread = std::this_thread::get_id();
        std::atomic<std::thread::id> audioThread {};
        clap_id nextTimerId = 1;

        const clap_host_params_t paramsExt {
            [] (const clap_host_t* h, clap_param_rescan_flags flags) { auto& me = self (h); const std::lock_guard<std::mutex> g (me.recordLock); me.rescans.push_back (flags); },
            [] (const clap_host_t* h, clap_id id, clap_param_clear_flags flags) { auto& me = self (h); const std::lock_guard<std::mutex> g (me.recordLock); me.clears.emplace_back (id, flags); },
            [] (const clap_host_t* h) { ++self (h).flushRequests; },
        };

        const clap_host_timer_support_t timerExt {
            [] (const clap_host_t* h, uint32_t periodMs, clap_id* timerId) -> bool
            {
                auto& me = self (h);
                *timerId = me.nextTimerId++;
                me.timers.push_back ({ *timerId, periodMs });
                return true;
            },
            [] (const clap_host_t* h, clap_id timerId) -> bool
            {
                auto& me = self (h);
                for (auto it = me.timers.begin(); it != me.timers.end(); ++it)
                    if (it->id == timerId) { me.timers.erase (it); return true; }
                return false;
            },
        };

        const clap_host_posix_fd_support_t fdExt {
            [] (const clap_host_t* h, int fd, clap_posix_fd_flags_t flags) -> bool { self (h).fds.push_back ({ fd, flags }); return true; },
            [] (const clap_host_t* h, int fd, clap_posix_fd_flags_t flags) -> bool
            {
                for (auto& f : self (h).fds) if (f.fd == fd) { f.flags = flags; return true; }
                return false;
            },
            [] (const clap_host_t* h, int fd) -> bool
            {
                auto& registered = self (h).fds;
                for (auto it = registered.begin(); it != registered.end(); ++it) if (it->fd == fd) { registered.erase (it); return true; }
                return false;
            },
        };

        const clap_host_thread_check_t threadExt {
            [] (const clap_host_t* h) -> bool { auto& me = self (h); return me.audioThread.load() == std::thread::id() && std::this_thread::get_id() == me.mainThread; },
            [] (const clap_host_t* h) -> bool { return self (h).audioThread.load() == std::this_thread::get_id(); },
        };

        const clap_host_gui_t guiExt {
            [] (const clap_host_t*) {},
            [] (const clap_host_t* h, uint32_t w, uint32_t ht) -> bool { self (h).resizeRequests.emplace_back (w, ht); return true; },
            [] (const clap_host_t*) -> bool { return true; },
            [] (const clap_host_t*) -> bool { return true; },
            [] (const clap_host_t* h, bool) { ++self (h).guiClosed; },
        };
    };

    //==============================================================================
    /** A plugin created from the factory, destroyed with it. */
    class PluginHandle
    {
    public:
        PluginHandle (RecordingHost& hostIn, const char* id)
            : host (hostIn)
        {
            const auto& module = LoadedModule::get();

            if (module.factory != nullptr)
                plugin = module.factory->create_plugin (module.factory, host.clapHost(), id);
        }

        ~PluginHandle()
        {
            if (plugin != nullptr)
                plugin->destroy (plugin);
        }

        PluginHandle (const PluginHandle&) = delete;
        PluginHandle& operator= (const PluginHandle&) = delete;

        const clap_plugin_t* operator->() const noexcept { return plugin; }
        const clap_plugin_t* get() const noexcept { return plugin; }
        explicit operator bool() const noexcept { return plugin != nullptr; }

        template <typename Ext>
        const Ext* extension (const char* id) const
        {
            return plugin != nullptr ? static_cast<const Ext*> (plugin->get_extension (plugin, id)) : nullptr;
        }

        RecordingHost& host;

    private:
        const clap_plugin_t* plugin = nullptr;
    };

    //==============================================================================
    /** A clap_ostream over a string, writing at most `maxChunk` bytes per call when set. */
    struct StringOStream
    {
        explicit StringOStream (std::int64_t maxChunkIn = -1) : maxChunk (maxChunkIn)
        {
            stream.ctx = this;
            stream.write = [] (const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t
            {
                auto& me = *static_cast<StringOStream*> (s->ctx);
                auto n = static_cast<std::int64_t> (size);
                if (me.maxChunk > 0 && n > me.maxChunk) n = me.maxChunk;
                me.data.append (static_cast<const char*> (buffer), static_cast<std::size_t> (n));
                ++me.calls;
                return n;
            };
        }

        std::string data;
        std::int64_t maxChunk;
        int calls = 0;
        clap_ostream_t stream {};
    };

    /** A clap_istream over a string, giving at most `maxChunk` bytes per call when set. */
    struct StringIStream
    {
        explicit StringIStream (std::string text, std::int64_t maxChunkIn = -1) : data (std::move (text)), maxChunk (maxChunkIn)
        {
            stream.ctx = this;
            stream.read = [] (const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t
            {
                auto& me = *static_cast<StringIStream*> (s->ctx);
                auto remaining = static_cast<std::int64_t> (me.data.size() - me.position);
                auto n = std::min (remaining, static_cast<std::int64_t> (size));
                if (me.maxChunk > 0 && n > me.maxChunk) n = me.maxChunk;
                std::memcpy (buffer, me.data.data() + me.position, static_cast<std::size_t> (n));
                me.position += static_cast<std::size_t> (n);
                ++me.calls;
                return n;
            };
        }

        std::string data;
        std::size_t position = 0;
        std::int64_t maxChunk;
        int calls = 0;
        clap_istream_t stream {};
    };

    //==============================================================================
    /** Events into the plugin: parameter values with sample offsets. */
    struct InEvents
    {
        InEvents()
        {
            list.ctx = this;
            list.size = [] (const clap_input_events_t* l) -> uint32_t { return static_cast<uint32_t> (static_cast<InEvents*> (l->ctx)->events.size()); };
            list.get = [] (const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t*
            {
                auto& me = *static_cast<InEvents*> (l->ctx);
                return i < me.events.size() ? &me.events[i].header : nullptr;
            };
        }

        void paramValue (clap_id paramId, double value, std::uint32_t sampleOffset = 0)
        {
            clap_event_param_value_t e {};
            e.header.size = sizeof (e);
            e.header.time = sampleOffset;
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_PARAM_VALUE;
            e.header.flags = 0;
            e.param_id = paramId;
            e.cookie = nullptr;
            e.note_id = -1;
            e.port_index = -1;
            e.channel = -1;
            e.key = -1;
            e.value = value;
            events.push_back (e);
        }

        void clear() { events.clear(); }

        std::vector<clap_event_param_value_t> events;
        clap_input_events_t list {};
    };

    /** Events out of the plugin, each kept as its bytes. */
    struct OutEvents
    {
        OutEvents()
        {
            list.ctx = this;
            list.try_push = [] (const clap_output_events_t* l, const clap_event_header_t* e) -> bool
            {
                auto& me = *static_cast<OutEvents*> (l->ctx);
                me.events.emplace_back (reinterpret_cast<const std::uint8_t*> (e), reinterpret_cast<const std::uint8_t*> (e) + e->size);
                return true;
            };
        }

        const clap_event_header_t* header (std::size_t i) const { return reinterpret_cast<const clap_event_header_t*> (events[i].data()); }
        template <typename E> const E* as (std::size_t i) const { return reinterpret_cast<const E*> (events[i].data()); }
        void clear() { events.clear(); }

        std::vector<std::vector<std::uint8_t>> events;
        clap_output_events_t list {};
    };

    //==============================================================================
    /** One stereo block through the plugin, with its events. */
    struct ProcessRig
    {
        explicit ProcessRig (std::uint32_t framesIn = 64) : frames (framesIn)
        {
            for (auto& ch : inputData) ch.assign (frames, 0.0f);
            for (auto& ch : outputData) ch.assign (frames, 0.0f);

            inPointers[0] = inputData[0].data();  inPointers[1] = inputData[1].data();
            outPointers[0] = outputData[0].data(); outPointers[1] = outputData[1].data();

            input.data32 = inPointers;
            input.channel_count = 2;
            output.data32 = outPointers;
            output.channel_count = 2;

            process.steady_time = -1;
            process.frames_count = frames;
            process.transport = nullptr;
            process.audio_inputs = &input;
            process.audio_outputs = &output;
            process.audio_inputs_count = 1;
            process.audio_outputs_count = 1;
            process.in_events = &in.list;
            process.out_events = &out.list;
        }

        clap_process_status run (PluginHandle& plugin)
        {
            RecordingHost::AudioThreadScope audio (plugin.host);
            const auto status = plugin->process (plugin.get(), &process);
            process.steady_time = process.steady_time < 0 ? -1 : process.steady_time + frames;
            return status;
        }

        std::uint32_t frames;
        std::vector<float> inputData[2], outputData[2];
        float* inPointers[2] {};
        float* outPointers[2] {};
        clap_audio_buffer_t input {};
        clap_audio_buffer_t output {};
        InEvents in;
        OutEvents out;
        clap_process_t process {};
    };
}
