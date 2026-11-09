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
    A PLUGIN EXTENSION OF OUR OWN (Phase 5b-2): the instance's status and the
    registry's settings as JSON, through the CLAP API, for the test host and
    for anyone diagnosing a plugin without its editor. Read-only; the strings
    are the plugin's and stay valid until the next call. [main-thread]

    Vendor extensions are CLAP's way (an id of one's own); a host that does
    not know it never asks.
*/

#include <clap/clap.h>

#ifdef __cplusplus
extern "C" {
#endif

static const char OQA_CLAP_EXT_STATUS[] = "io.github.pob31.oqa.status/1";

typedef struct oqa_clap_status
{
    /** The instance's status as the registry reports it (devices, slots, warnings). */
    const char* (CLAP_ABI *status_json) (const clap_plugin_t* plugin);

    /** The registry's process-wide settings. */
    const char* (CLAP_ABI *settings_json) (const clap_plugin_t* plugin);
} oqa_clap_status_t;

#ifdef __cplusplus
}
#endif
