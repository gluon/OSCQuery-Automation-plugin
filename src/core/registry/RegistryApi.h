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
    table with `size` telling a caller whether it is there; the loader refuses
    a table smaller than this header, so a plugin built with the Phase 4
    entries beside a Phase 3 library runs on its own core and says so.
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
        gather_deadline_us, mtu, active_window_ms, and since Phase 4
        receive_port, loop_guard_ms, gesture_timeout_ms. Fields absent keep
        their value. 0 on success. */
    int (*settingsSetJson) (const char* json, char** error);
    char* (*settingsJson) (void);

    /** Frees any string the table answered. */
    void (*freeString) (char* text);

    /*  Additive, 2026-11-08 (size says whether it is there): stop every
        device's thread and forget every instance. For a process that is
        ending; a test calls it before main returns. */
    void (*shutdown) (void);

    /*  Additive, 2026-11-09 (Phase 4, record). Values the devices stream for
        the instance's receiving slots wait in a ring inside the registry.
        instanceSetReceiver: `wake (context)` is called on a registry thread, at
        most once per message delivered, and must only signal (the shim
        triggers an async update from it; nothing else). NULL clears it.
        instancePollReceived: drains the ring, oldest first, into the caller's
        arrays, up to `capacity`; returns how many. Scalars only: no allocation,
        no lock, any thread. */
    void (*instanceSetReceiver) (OqaInstance* instance, void (*wake) (void* context), void* context);
    int (*instancePollReceived) (OqaInstance* instance, int* slots, float* values, int capacity);

    /*  Additive, 2026-11-09 (Phase 5b, re-identification). The registry's
        Finder, one per process: finderEnsure starts it browsing DNS-SD and
        probing (an editor opened; otherwise it starts with the first device
        to look for); finderAddManual adds a typed host and port, probed like
        the rest. What it lists is the status JSON's `found`. */
    void (*finderEnsure) (void);
    void (*finderAddManual) (const char* host, int port);

    /*  Additive, 2026-11-09 (Phase 6a-2). An instance is registered for the
        plugin's life and says here whether the host runs its audio; the
        status (its own and the process-wide one, `instances[]`) shows it. */
    void (*instanceSetActive) (OqaInstance* instance, int active);

    /*  Additive, 2026-11-09 (Phase 6a-3). A device's timing set by hand, for
        the whole process (the Monitor, any editor): it stands whatever chunk is
        pushed next, and every instance's chunk takes it on its next save.
        Returns 0, or 1 when the registry has no such device. */
    int (*deviceSetTiming) (const char* name, const char* identityValue, int honoursTimetags, double offsetMs);
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
