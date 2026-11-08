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
    THE REGISTRY'S C ABI: the one thing that crosses the shared core library's
    boundary (devplan Phase 3: "The registry lives in a shared core library
    loaded by every plugin binary, after WFS-DIY's Bridge: versioned ABI,
    exact-match version check, installed next to the plugins").

    ONE EXPORT, A TABLE OF FUNCTIONS. The library exports oqa_core_api(),
    which answers a const OqaCoreApi*; the loader checks abiVersion equals
    OQA_CORE_ABI_VERSION exactly and `size` is at least what it knows, and
    calls through the table from then on. WFS-DIY's Bridge resolved a dozen
    symbols one by one with the same exact-match rule; one table is one
    resolution and one place to read the contract. A plugin binary also holds
    the same table statically (oqa::core::staticCoreApi) and uses it when no
    library is found or its version differs: the plugin always works, sharing
    is the bonus.

    WHAT CROSSES: plain scalars on the hot path (post, end_block, from the
    audio thread: no allocation, no lock, one wake per block per device), and
    JSON text for everything structured (bindings in, a device's tree and the
    status out). Strings the library answers are freed with freeString.

    VERSION 1 (2026-11-08). A changed signature or meaning bumps the version
    and every installed library is then refused by the exact match, falling
    back to the static copy. A purely additive function goes at the END of the
    table with `size` telling a caller whether it is there.
*/

#include <stddef.h>
#include <stdint.h>

#define OQA_CORE_ABI_VERSION 1

#if defined (_WIN32)
 #define OQA_CORE_EXPORT __declspec (dllexport)
#else
 #define OQA_CORE_EXPORT __attribute__ ((visibility ("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** One plugin instance as the registry knows it. */
typedef struct OqaInstance OqaInstance;

typedef struct OqaCoreApi
{
    int abiVersion;             /**< OQA_CORE_ABI_VERSION of the library that built the table */
    size_t size;                /**< sizeof (OqaCoreApi) there, so an older caller stops where its knowledge ends */

    /*  Lifecycle. Called from prepareToPlay (and again whenever the host
        restarts the process) and from releaseResources and the destructor. */
    OqaInstance* (*instanceRegister) (const char* label, int slotCount);
    void (*instanceUnregister) (OqaInstance* instance);

    /*  The instance's bindings as its state chunk's JSON (devices and slots).
        0 on success; else non-zero and *error (freeString it) says why. Not
        for the audio thread. */
    int (*instanceSetBindings) (OqaInstance* instance, const char* chunkJson, char** error);

    /*  The audio thread. post: `slot` of this instance is now `normalised`
        (0-1). endBlock: nothing more this block; every device touched is
        woken once. Neither allocates, locks or waits. */
    void (*instancePost) (OqaInstance* instance, int slot, float normalised);
    void (*instanceEndBlock) (OqaInstance* instance);

    /*  The editor's Test button: send the slot's value once, now. 0 on
        success; else *error says why (not bound, device unreachable...). */
    int (*instanceTestSend) (OqaInstance* instance, int slot, float normalised, char** error);

    /*  The tree of the instance's device `deviceIndex` (the chunk's index) at
        `path` ("/" for all of it), as the device serves it, or NULL when the
        tree is not loaded. The device's own JSON, so the editor shows what
        the device said. */
    char* (*instanceDeviceTreeJson) (OqaInstance* instance, int deviceIndex, const char* path);

    /** Status of the devices this instance refers to, as JSON. */
    char* (*instanceStatusJson) (OqaInstance* instance);

    /** Status of every device in the registry (the Monitor's view), as JSON. */
    char* (*statusJson) (void);

    /*  Point a device at an address, process-wide: every instance referring
        to the device named `name` (and `identityValue`, "" for none) follows.
        0 on success, non-zero when no such device is registered. */
    int (*deviceSetAddress) (const char* name, const char* identityValue, const char* host, int port);

    /*  Global settings as JSON: direct_send, default_rate_hz, resend_ms,
        gather_deadline_us, mtu, active_window_ms. Fields absent keep their
        value. 0 on success. */
    int (*settingsSetJson) (const char* json, char** error);
    char* (*settingsJson) (void);

    /** Frees any string the table answered. */
    void (*freeString) (char* text);
} OqaCoreApi;

/*  The shared library's export. Declared with the export attribute only
    where the library is being built; a plugin binary never exports it. */
#if defined (OQA_CORE_BUILDING_SHARED)
OQA_CORE_EXPORT const OqaCoreApi* oqa_core_api (void);
#endif

#ifdef __cplusplus
}

namespace oqa::core
{
    /** The same table, from the core linked into this binary: the fallback. */
    const OqaCoreApi* staticCoreApi() noexcept;
}
#endif
