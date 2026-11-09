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

/*
    THE PARAMS EXTENSION (Phase 5b-2): one parameter per slot, as ClapPlugin.h
    describes; the rename and the clear rule. Everything here but
    paramsFlush is [main-thread], where the chunk is read as it is.
*/

#include "ClapPlugin.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace oqa::clapshim
{
    namespace
    {
        constexpr const char* unboundName = "(unbound)";

        void copyInto (char* destination, std::size_t size, const juce::String& text)
        {
            const auto utf8 = text.toStdString();
            std::snprintf (destination, size, "%s", utf8.c_str());
        }

        /** The device's NAME and the path's container: "Renderer/wfs/input/12" for /wfs/input/12/azim. */
        juce::String moduleOf (const core::StateChunk& chunk, const core::Slot& slot)
        {
            juce::String module;

            if (slot.deviceIndex >= 0 && slot.deviceIndex < static_cast<int> (chunk.devices.size()))
                module = juce::String (chunk.devices[static_cast<std::size_t> (slot.deviceIndex)].name).replaceCharacter ('/', '-');

            const auto path = juce::String (slot.path);
            const auto lastSlash = path.lastIndexOfChar ('/');

            if (lastSlash > 0)
                module += path.substring (0, lastSlash);

            return module;
        }
    }

    //==============================================================================
    std::uint32_t ClapPlugin::paramsCount() const noexcept
    {
        return static_cast<std::uint32_t> (count);
    }

    bool ClapPlugin::paramsInfo (std::uint32_t index, clap_param_info_t* info) const noexcept
    {
        if (index >= static_cast<std::uint32_t> (count))
            return false;

        const auto& chunk = core.chunk();
        const auto& slot = chunk.slots.all()[index];

        info->id = index;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | (slot.bound() ? 0u : static_cast<std::uint32_t> (CLAP_PARAM_IS_HIDDEN));
        info->cookie = nullptr;
        copyInto (info->name, sizeof (info->name), slot.bound() ? juce::String (slot.displayName) : juce::String (unboundName));
        copyInto (info->module, sizeof (info->module), slot.bound() ? moduleOf (chunk, slot) : juce::String());
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    }

    bool ClapPlugin::paramsValue (clap_id id, double* value) noexcept
    {
        if (id >= static_cast<clap_id> (count))
            return false;

        *value = values[id].load (std::memory_order_relaxed);
        return true;
    }

    bool ClapPlugin::paramsValueToText (clap_id id, double value, char* display, std::uint32_t size) noexcept
    {
        if (id >= static_cast<clap_id> (count))
            return false;

        /*  As the JUCE shim's parameter shows it: the device's units through
            the slot's own scaling, the unit after; a toggle on or off; a slot
            without a range (unbound, or RANGE unknown) the 0-1 value itself.
            juce::String's number text is locale-independent. */
        const auto& slot = core.chunk().slots.all()[id];
        const auto normalised = static_cast<float> (std::clamp (value, 0.0, 1.0));
        juce::String text;

        if (! slot.active())
            text = juce::String (normalised, 3);
        else if (slot.isToggle())
            text = normalised >= 0.5f ? "on" : "off";
        else
        {
            const auto units = slot.toDevice (normalised);
            text = slot.isInteger() ? juce::String (static_cast<int> (units)) : juce::String (units, 3);

            if (! slot.unit.empty())
                text += " " + juce::String (slot.unit);
        }

        copyInto (display, size, text);
        return true;
    }

    bool ClapPlugin::paramsTextToValue (clap_id id, const char* display, double* value) noexcept
    {
        if (id >= static_cast<clap_id> (count) || display == nullptr)
            return false;

        const auto& slot = core.chunk().slots.all()[id];
        const auto text = juce::String::fromUTF8 (display).trim();

        if (! slot.active())
            *value = std::clamp (text.getDoubleValue(), 0.0, 1.0);
        else if (slot.isToggle())
            *value = (text.equalsIgnoreCase ("on") || text.getDoubleValue() >= 0.5) ? 1.0 : 0.0;
        else
            *value = std::clamp (static_cast<double> (slot.toHost (text.getDoubleValue())), 0.0, 1.0);

        return true;
    }

    void ClapPlugin::paramsFlush (const clap_input_events_t* in, const clap_output_events_t* out) noexcept
    {
        /*  Not processing (or the host chose flush): the events still land,
            what the devices streamed goes out, and what moved still goes to
            the device, so a knob turned while the transport is idle reaches
            it. Never concurrent with process (clap/ext/params.h). */
        applyEvents (in);
        drainReceived (out);
        postValues();
    }

    //==============================================================================
    void ClapPlugin::bindingsChanged (clap_param_rescan_flags moreFlags) noexcept
    {
        const auto& chunk = core.chunk();
        const auto& slots = chunk.slots.all();

        if (_host.canUseParams())
        {
            /*  The clear rule (decided 2026-11-09): a slot whose target had
                gone, now bound elsewhere, loses the curve drawn for it. */
            for (int i = 0; i < count && static_cast<std::size_t> (i) < slots.size(); ++i)
            {
                const auto& was = standing[static_cast<std::size_t> (i)];
                const auto& now = slots[static_cast<std::size_t> (i)];

                const bool wasGone = was.state == "missing" || was.state == "retyped";
                const bool elsewhere = now.bound()
                                       && (now.deviceIndex != was.deviceIndex || now.path != was.path || now.argIndex != was.argIndex);

                if (wasGone && elsewhere)
                    _host.paramsClear (static_cast<clap_id> (i), CLAP_PARAM_CLEAR_AUTOMATIONS);
            }

            _host.paramsRescan (CLAP_PARAM_RESCAN_INFO | moreFlags);
        }

        shownRevision = core.revision();
        recordStanding();
    }

    void ClapPlugin::recordStanding() noexcept
    {
        const auto& slots = core.chunk().slots.all();

        for (int i = 0; i < count && static_cast<std::size_t> (i) < slots.size(); ++i)
        {
            auto& s = standing[static_cast<std::size_t> (i)];
            const auto& slot = slots[static_cast<std::size_t> (i)];
            s.deviceIndex = slot.deviceIndex;
            s.path = slot.path;
            s.argIndex = slot.argIndex;
            s.state.clear();
        }

        if (! core.isConnected())
            return;

        /*  The registry's word on each bound slot, from the status (5a). */
        const auto status = juce::JSON::parse (juce::String (core.statusJson()));

        if (const auto* checks = status.getProperty ("slots", {}).getArray())
        {
            for (const auto& check : *checks)
            {
                const auto id = static_cast<int> (check.getProperty ("id", -1));

                if (id >= 0 && id < count)
                    standing[static_cast<std::size_t> (id)].state = check.getProperty ("state", "").toString().toStdString();
            }
        }
    }
}
