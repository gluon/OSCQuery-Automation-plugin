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
    does the same four things with the core:

      init, activate      connect() to the registry (in init too, so an
                          editor opened on a never-activated plugin has a
                          registry; again in activate, a host may restart)
      deactivate, destroy disconnect()
      process             audio passes through untouched; the slots' values
                          go to the core, which posts what moved and ends the
                          block (Phase 5b-2: the parameter events first)
      state               the core's JSON chunk over clap_istream and
                          clap_ostream, the parameters' values in it

    The registry is the shared core library's when one is beside the module,
    this binary's own otherwise; the status says which.

    clap-helpers checks every call's thread and state: in a Debug build a
    misbehaving host or plugin terminates (the harness and clap-validator
    see it), in Release it is ignored.
*/

#include <core/Instance.h>
#include <core/registry/RegistryLoader.h>

#include <clap/helpers/plugin.hh>

#include <cstdint>
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

        //  clap_plugin_audio_ports: one stereo port each way, in place
        bool implementsAudioPorts() const noexcept override { return true; }
        std::uint32_t audioPortsCount (bool isInput) const noexcept override;
        bool audioPortsInfo (std::uint32_t index, bool isInput, clap_audio_port_info_t* info) const noexcept override;

        //  clap_plugin_state: the chunk
        bool implementsState() const noexcept override { return true; }
        bool stateSave (const clap_ostream_t* stream) noexcept override;
        bool stateLoad (const clap_istream_t* stream) noexcept override;

        //  clap_plugin_timer_support: the 50 ms tick (Phase 5b-2 and 5b-3 fill it)
        bool implementsTimerSupport() const noexcept override { return true; }
        void onTimer (clap_id timerId) noexcept override;

    private:
        static constexpr std::uint32_t timerPeriodMs = 50;

        const core::LoadedCore& loaded;
        const int count;
        core::Instance core;

        /** The host's value of every slot, 0-1: written by the events, read by process and the state. */
        std::vector<float> values;

        clap_id timerId = CLAP_INVALID_ID;
    };
}
