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
    THE PLUGIN'S OWN NAMESPACE (PRD, UX, Headless use: "the plugin exposes its
    slot table at /plugin/<instance>/slot/<n>/..."). THE ONE FILE IN src/ WITH
    OSC ADDRESS TEXT, by decision (2026-11-09, Phase 6): the PRD fixes the
    namespace, so these are the PRD's addresses, not a device's; every other
    segment is the implementer's and is spelled here and nowhere else.

      /plugin/<id>                  one registered instance (its registry id)
        label           s           the shim's label for it
        instance        i           the number $instance stands for in its paths
        active          i 0|1       whether the host runs its audio
        slot/<n>        container   one per slot, n the host parameter index
          path          s           the bound OSC path; "" unbound. Writable: a
                                    string binds (a second string names the
                                    device), "" unbinds
          device        s           the device's NAME. Writable: on a bound
                                    slot rebinds there, on an unbound one names
                                    the device for the next path written
          state         s           unbound, pending, ok, missing, retyped,
                                    renamed, unauthorised, inactive
          arg_index     i 0..3      (bound only, from here on)
          type          s           f, i, T, F
          range/min     f           the slot's range, device units (writable)
          range/max     f
          scaling       s           linear, log, exp (writable)
          direction     s           send, receive, both (writable)
          display_name  s           (writable)
          value         f 0..1      the host's current value (read-only, LISTEN)
      /device/<i>                   the registry's devices, in key order, read-only
        name host port resolution reachable state honours_timetags offset_ms
      /server                       this server
        name process_id port osc_port announcing
*/

#include <string>

namespace oqa::core::server::ns
{
    inline constexpr const char* pluginRoot = "/plugin";
    inline constexpr const char* deviceRoot = "/device";
    inline constexpr const char* serverRoot = "/server";

    inline constexpr const char* label        = "label";
    inline constexpr const char* instance     = "instance";
    inline constexpr const char* active       = "active";
    inline constexpr const char* slot         = "slot";
    inline constexpr const char* path         = "path";
    inline constexpr const char* device       = "device";
    inline constexpr const char* state        = "state";
    inline constexpr const char* argIndex     = "arg_index";
    inline constexpr const char* type         = "type";
    inline constexpr const char* range        = "range";
    inline constexpr const char* min          = "min";
    inline constexpr const char* max          = "max";
    inline constexpr const char* scaling      = "scaling";
    inline constexpr const char* direction    = "direction";
    inline constexpr const char* displayName  = "display_name";
    inline constexpr const char* value        = "value";

    inline constexpr const char* name            = "name";
    inline constexpr const char* host            = "host";
    inline constexpr const char* port            = "port";
    inline constexpr const char* resolution      = "resolution";
    inline constexpr const char* reachable       = "reachable";
    inline constexpr const char* honoursTimetags = "honours_timetags";
    inline constexpr const char* offsetMs        = "offset_ms";
    inline constexpr const char* processId       = "process_id";
    inline constexpr const char* oscPort         = "osc_port";
    inline constexpr const char* announcing      = "announcing";

    inline std::string join (const std::string& parent, const std::string& child)
    {
        return parent == "/" ? "/" + child : parent + "/" + child;
    }

    inline std::string pluginPath (unsigned long id)        { return join (pluginRoot, std::to_string (id)); }
    inline std::string slotPath (unsigned long id, int n)   { return join (join (pluginPath (id), slot), std::to_string (n)); }
    inline std::string devicePath (int i)                   { return join (deviceRoot, std::to_string (i)); }
}
