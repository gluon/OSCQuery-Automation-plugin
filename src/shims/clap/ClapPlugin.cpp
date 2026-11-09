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

#include "ClapPlugin.h"

#include <core/rt/RtCheck.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

namespace oqa::clapshim
{
    namespace
    {
        std::string instanceLabel (const clap_plugin_descriptor_t* descriptor)
        {
            static std::atomic<int> counter { 0 };
            return std::string (descriptor->name) + " (CLAP) #" + std::to_string (++counter);
        }
    }

    ClapPlugin::ClapPlugin (const clap_plugin_descriptor_t* descriptor, const clap_host_t* host, int slotCount)
        : PluginBase (descriptor, host),
          loaded (core::RegistryLoader::load()),
          count (slotCount),
          core (loaded.api, slotCount, instanceLabel (descriptor)),
          values (static_cast<std::size_t> (slotCount), 0.0f)
    {
    }

    ClapPlugin::~ClapPlugin()
    {
        if (timerId != CLAP_INVALID_ID && _host.canUseTimerSupport())
            _host.timerSupportUnregister (timerId);

        core.disconnect();
    }

    //==============================================================================
    bool ClapPlugin::init() noexcept
    {
        core.connect();

        if (_host.canUseTimerSupport())
            if (! _host.timerSupportRegister (timerPeriodMs, &timerId))
                timerId = CLAP_INVALID_ID;

        return true;
    }

    bool ClapPlugin::activate (double, std::uint32_t, std::uint32_t) noexcept
    {
        core.connect();
        return true;
    }

    void ClapPlugin::deactivate() noexcept
    {
        core.disconnect();
    }

    clap_process_status ClapPlugin::process (const clap_process_t* process) noexcept
    {
        const core::rt::ScopedRealtimeCheck region;

        /*  Audio: nothing. Where the host gave output buffers of its own,
            the input is copied over; in place, there is nothing to do. */
        if (process->audio_inputs_count > 0 && process->audio_outputs_count > 0)
        {
            const auto& in = process->audio_inputs[0];
            auto& out = process->audio_outputs[0];
            const auto channels = std::min (in.channel_count, out.channel_count);

            for (std::uint32_t c = 0; c < channels; ++c)
            {
                if (in.data32 == nullptr || out.data32 == nullptr || in.data32[c] == nullptr || out.data32[c] == nullptr)
                    continue;

                if (in.data32[c] != out.data32[c])
                    std::memcpy (out.data32[c], in.data32[c], sizeof (float) * process->frames_count);
            }
        }

        core.processBlock (values.data(), count);
        return CLAP_PROCESS_CONTINUE;
    }

    //==============================================================================
    std::uint32_t ClapPlugin::audioPortsCount (bool) const noexcept
    {
        return 1;
    }

    bool ClapPlugin::audioPortsInfo (std::uint32_t index, bool isInput, clap_audio_port_info_t* info) const noexcept
    {
        if (index != 0)
            return false;

        info->id = 0;
        std::snprintf (info->name, sizeof (info->name), "%s", isInput ? "Input" : "Output");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = 0;
        return true;
    }

    //==============================================================================
    bool ClapPlugin::stateSave (const clap_ostream_t* stream) noexcept
    {
        /*  The chunk, the parameters' values in it (Phase 5b: a CLAP keeps
            its own). Written whole, in as many calls as the stream takes. */
        core.setValues (values.data(), count);
        const auto json = core.saveState();

        std::size_t written = 0;

        while (written < json.size())
        {
            const auto n = stream->write (stream, json.data() + written, json.size() - written);

            if (n <= 0)
                return false;

            written += static_cast<std::size_t> (n);
        }

        return true;
    }

    bool ClapPlugin::stateLoad (const clap_istream_t* stream) noexcept
    {
        std::string json;
        char buffer[4096];

        for (;;)
        {
            const auto n = stream->read (stream, buffer, sizeof (buffer));

            if (n < 0)
                return false;

            if (n == 0)
                break;

            json.append (buffer, static_cast<std::size_t> (n));
        }

        std::string error;

        if (! core.loadState (json, error))
            return false;

        const auto& saved = core.chunk().values;

        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = i < saved.size() ? saved[i] : 0.0f;

        return true;
    }

    //==============================================================================
    void ClapPlugin::onTimer (clap_id) noexcept
    {
        /*  Phase 5b-2: the revision poll and the settings; 5b-3: the nudge
            that closes an idle gesture while the host is not processing. */
    }
}
