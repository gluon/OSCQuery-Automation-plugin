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
    THE CLAP SHIM'S PLUGIN (devplan Phase 5b): one clap_plugin over one
    core::Instance, thin on purpose, as the JUCE shim's processor is. It
    does the same things with the core:

      init, activate      connect() to the registry (in init too, so an
                          editor opened on a never-activated plugin has a
                          registry; again in activate, a host may restart)
      deactivate          the instance marked inactive, still registered (Phase
                          6: the server's paths and the Monitor's row stay)
      destroy             disconnect()
      process, flush      the parameter events are applied (the last value
                          of each slot in the block; decided 2026-11-09, the
                          sample-accurate send waits for a device that
                          honours timetags), what the devices streamed goes
                          out as events (below), audio passes through
                          untouched, the slots' values go to the core, which
                          posts what moved and ends the block
      params              one per slot, id = slot index, 0-1, AUTOMATABLE;
                          HIDDEN while unbound (the PRD's "hidden where the
                          format allows"); name = display name; module =
                          device NAME plus the path's container, so the host
                          groups lanes by device and node; the text in device
                          units through the slot's own scaling. Never STEPPED:
                          flipping that flag would need RESCAN_ALL, which a
                          host only takes while the plugin is inactive, and a
                          toggle binds while the transport runs.
      rename              whenever the core's revision moves (a bind, a
                          rename, a state load), rescan (RESCAN_INFO): CLAP's
                          one rescan covers name, module and hidden, at once,
                          while active (clap/ext/params.h)
      clear()             decided 2026-11-09: only when a slot whose standing
                          was `missing` or `retyped` is rebound to a different
                          device or path (the curve was drawn for a target
                          that is gone); never on a rename rebind, never on
                          unbind, never on rebinding a live slot. The shim
                          keeps each slot's last standing from the status for
                          that.
      record (5b-3)       the registry wakes us once per message a device
                          streams for a receiving slot, on a thread of its
                          own; the wake asks the host to flush. In process or
                          in that flush, on the audio thread, the values are
                          drained (lock-free; devplan Phase 5b) into
                          out-events: PARAM_GESTURE_BEGIN on a gesture's first
                          value, PARAM_VALUE, PARAM_GESTURE_END once the
                          gesture has been idle for gesture_timeout_ms (the
                          one clock read per block, an exception to the
                          lipogram as the wake notify is). Whether a lane
                          records is the host's mode, as the PRD decides. The
                          50 ms timer asks for a flush while a gesture is open
                          and the host is not processing, so it closes.
      state               the core's JSON chunk over clap_istream and
                          clap_ostream, the parameters' values in it
      gui (5b-4)          the one editor (editor::EditorRoot) in the host's
                          window on win32, cocoa and x11, embedded only;
                          JUCE initialised in init() on the host's main
                          thread; on Linux JUCE's descriptors watched through
                          the host's posix-fd support (LinuxEventPump), or
                          polled from the timer (ClapGui.cpp)

    The registry is the shared core library's when one is beside the module,
    this binary's own otherwise; the status says which.

    clap-helpers checks every call's thread and state: in a Debug build a
    misbehaving host or plugin terminates (the harness and clap-validator
    see it), in Release it is ignored.
*/

#include "LinuxEventPump.h"
#include "StatusExtension.h"

#include <core/Instance.h>
#include <core/receive/Gesture.h>
#include <core/registry/RegistryLoader.h>
#include <editor/EditorRoot.h>

#include <clap/helpers/plugin.hh>

#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace oqa::clapshim
{
   #if defined (NDEBUG)
    inline constexpr auto misbehaviour = ::clap::helpers::MisbehaviourHandler::Ignore;
    inline constexpr auto checking     = ::clap::helpers::CheckingLevel::Minimal;
   #else
    inline constexpr auto misbehaviour = ::clap::helpers::MisbehaviourHandler::Terminate;
    inline constexpr auto checking     = ::clap::helpers::CheckingLevel::Maximal;
   #endif

    using PluginBase = ::clap::helpers::Plugin<misbehaviour, checking>;

    class ClapPlugin final : public PluginBase
    {
    public:
        ClapPlugin (const clap_plugin_descriptor_t* descriptor, const clap_host_t* host, int slotCount);
        ~ClapPlugin() override;

        int slotCount() const noexcept { return count; }

    protected:
        //  clap_plugin
        bool init() noexcept override;
        bool activate (double sampleRate, std::uint32_t minFrames, std::uint32_t maxFrames) noexcept override;
        void deactivate() noexcept override;
        clap_process_status process (const clap_process_t* process) noexcept override;
        const void* extension (const char* id) noexcept override;

        //  clap_plugin_audio_ports: one stereo port each way, in place
        bool implementsAudioPorts() const noexcept override { return true; }
        std::uint32_t audioPortsCount (bool isInput) const noexcept override;
        bool audioPortsInfo (std::uint32_t index, bool isInput, clap_audio_port_info_t* info) const noexcept override;

        //  clap_plugin_params: one per slot (ClapParams.cpp)
        bool implementsParams() const noexcept override { return true; }
        std::uint32_t paramsCount() const noexcept override;
        bool paramsInfo (std::uint32_t index, clap_param_info_t* info) const noexcept override;
        bool paramsValue (clap_id id, double* value) noexcept override;
        bool paramsValueToText (clap_id id, double value, char* display, std::uint32_t size) noexcept override;
        bool paramsTextToValue (clap_id id, const char* display, double* value) noexcept override;
        void paramsFlush (const clap_input_events_t* in, const clap_output_events_t* out) noexcept override;

        //  clap_plugin_state: the chunk
        bool implementsState() const noexcept override { return true; }
        bool stateSave (const clap_ostream_t* stream) noexcept override;
        bool stateLoad (const clap_istream_t* stream) noexcept override;

        //  clap_plugin_timer_support: the 50 ms tick
        bool implementsTimerSupport() const noexcept override { return true; }
        void onTimer (clap_id timerId) noexcept override;

        //  clap_plugin_gui: the editor in the host's window (ClapGui.cpp)
        bool implementsGui() const noexcept override { return true; }
        bool guiIsApiSupported (const char* api, bool isFloating) noexcept override;
        bool guiGetPreferredApi (const char** api, bool* isFloating) noexcept override;
        bool guiCreate (const char* api, bool isFloating) noexcept override;
        void guiDestroy() noexcept override;
        bool guiSetScale (double scale) noexcept override;
        bool guiGetSize (std::uint32_t* width, std::uint32_t* height) noexcept override;
        bool guiCanResize() const noexcept override;
        bool guiGetResizeHints (clap_gui_resize_hints_t* hints) noexcept override;
        bool guiAdjustSize (std::uint32_t* width, std::uint32_t* height) noexcept override;
        bool guiSetSize (std::uint32_t width, std::uint32_t height) noexcept override;
        bool guiSetParent (const clap_window_t* window) noexcept override;
        bool guiShow() noexcept override;
        bool guiHide() noexcept override;

      #if defined (__linux__)
        //  clap_plugin_posix_fd_support: JUCE's descriptors, watched by the host
        bool implementsPosixFdSupport() const noexcept override { return true; }
        void onPosixFd (int fd, clap_posix_fd_flags_t flags) noexcept override;
      #endif

    private:
        static constexpr std::uint32_t timerPeriodMs = 50;
        static constexpr int slowEveryTicks = 5;     ///< the revision poll, the status and the settings, every 250 ms

        /** This plugin from its clap_plugin, for the extensions' trampolines. */
        static ClapPlugin& self (const clap_plugin_t* plugin) noexcept;

        /** Apply the parameter events of a list: each slot's last value. Audio thread, or main when inactive. */
        void applyEvents (const clap_input_events_t* in) noexcept;

        /** What the devices streamed, as gesture and value events (ClapRecord.cpp). Same threads. */
        void drainReceived (const clap_output_events_t* out) noexcept;

        /** The values to the core: what moved is posted, the block ended. */
        void postValues() noexcept;

        /*  The bindings changed (a bind, a rename, a state load): the clear
            rule against the last standing recorded, then the host told
            (RESCAN_INFO, plus `moreFlags`). Main thread. */
        void bindingsChanged (clap_param_rescan_flags moreFlags = 0) noexcept;

        /** Each slot's binding and standing as of now, for the next bindingsChanged. Main thread. */
        void recordStanding() noexcept;

        /** The registry's settings this shim reads (gesture_timeout_ms). Main thread. */
        void refreshSettings() noexcept;

        /** JUCE up on the host's main thread (init), and down again (destroy). */
        void startJuce() noexcept;
        void stopJuce() noexcept;

        /** The peer's scale once parented: physical pixels per logical one; 1 before. */
        double guiScale() const noexcept;

      #if defined (__linux__)
        void pumpJuceIfUnwatched() noexcept;
      #endif

        const core::LoadedCore& loaded;
        const int count;
        core::Instance core;

        /*  The host's value of every slot, 0-1: written by the events on the
            audio thread and by a state load on the main one, read by both. */
        std::unique_ptr<std::atomic<float>[]> values;
        std::vector<float> scratch;

        /** What each slot was bound to, and how the registry judged it, at the last look. */
        struct Standing
        {
            int deviceIndex = -1;
            std::string path;
            std::size_t argIndex = 0;
            std::string state;      ///< the status's word: ok, missing, retyped, renamed...; empty when unbound or unchecked
        };

        std::vector<Standing> standing;
        std::uint64_t shownRevision = 0;
        int ticks = 0;

        clap_id timerId = CLAP_INVALID_ID;

        //  Record (5b-3). The tracker is the audio thread's while processing,
        //  the main thread's otherwise (deactivate, flush); never both.
        core::GestureTracker gestures;
        std::unique_ptr<std::atomic<bool>[]> endPending;     ///< gestures closed outside a flush, their END owed
        std::atomic<int> openGestures { 0 };
        std::atomic<double> gestureTimeoutMs { 200.0 };

        //  The status extension's strings, kept until the next call.
        std::string statusText, settingsText;
        const oqa_clap_status_t statusExtension;

        //  GUI (5b-4). Declared after everything the editor reads; JUCE's
        //  initialiser before the editor, so the editor goes first.
        std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juceInit;
      #if defined (__linux__)
        std::unique_ptr<LinuxEventPump> pump;
      #endif
        std::unique_ptr<editor::EditorRoot> editor;
    };
}
