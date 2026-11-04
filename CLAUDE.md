# CLAUDE.md

The two sections below are **PRD *Goals and non-goals* and *Binding model*, reproduced
verbatim** from the PRD in `docs/`. They are the review criterion for every pull request.

**If this file and the PRD disagree, the PRD wins and this file is stale. Fix it by copying,
never by editing.**

---

## Goals and non-goals

**Goals**

1. Bind DAW automation lanes to any OSCQuery-published parameter without typing an address.
2. Play back (DAW to device) and record (device to DAW) with the same binding.
3. Survive the move from studio to venue: a session rebinds to the same device on a different IP.
4. One codebase, three formats: CLAP, VST3, AU. Pro Tools (AAX) is a later target, not a v1 target.
5. Serve as the reference client for the OSCQuery discovery profile and its interoperability test suite.
6. Open source under the GPLv3 licence (decided 2026-11-04, was MIT), in the pob31 repository; institutions that adopt it are expected to fork.

**Non-goals**

- No audio processing in the plugin; audio passes through untouched.
- No namespace semantics. The plugin does not know what `/adm/obj/1/azim` means; it only knows it is a float in \[-180, 180\].
- No device configuration (speaker setup, patching). Only parameters the device exposes as automatable.
- No authentication scheme. Write access is the device's business (see Access control).
- No dynamic parameter lists inside a running session. Parameter count is fixed per plugin instance; only names and bindings change.


---

## Binding model: slots, paths, scaling

Each plugin instance owns a fixed pool of **N generic slots** (N = 32 by default; 8, 16, 32, 64 and 128 variants as separate plugin IDs so the host sees a fixed count). A slot is one host-automatable parameter.

| Slot field | Source | Notes |
| --- | --- | --- |
| Host parameter ID | Fixed at build time | Stable across binds, renames and sessions; automation curves attach to it |
| Device | User choice in browser | Stored as `NAME` + last known host:port, plus an optional identity leaf (a read-only node the user designates, such as a serial number) |
| Path | User choice in browser | Full OSC path, with argument index for multi-argument nodes |
| Type | From tree | `f`, `i`, `T/F`; drives the OSC type tag on send |
| Range | From tree, user-overridable | Maps host 0–1 to device units; `RANGE` absent → user must enter min/max before the slot is active |
| Scaling | User choice | Linear (default), log, or exponential; same choices as OSCar |
| Direction | User choice | Send, receive, or both |
| Display name | From path, user-editable | Shown in the host's automation lane |

**Rules**

- The host always sees a normalised 0–1 float. Scaling to device units happens inside the plugin, so no host rescan is ever needed for a range change.
- Unbound slots are hidden where the format allows (CLAP `is_hidden`, AU `kAudioUnitParameterFlag_IsHidden`) and named `(unbound)` where it does not (VST3).
- Multi-argument nodes (`fff`) bind as consecutive slots; sending is grouped so the device receives one message with all arguments, using the last known value for arguments not being automated.
- Rate limiting per slot: default 100 Hz, configurable; sends only on value change, with an optional periodic resend for lossy links.
- A `$instance` token in paths is supported for ToscA-style per-track numbering, but it is sugar over the explicit path, not the primary model.


---

## Where to look

- **`docs/PRD — OSCQuery Automation Plugin (CLAP VST3 AU).md`** — the product requirements; the
  source of the two sections above and of every decision, each dated in place.
- **`docs/Devplan — OSCQuery Automation Plugin (draft 0.1).md`** — the phase plan: what is built
  now, in what order, what each phase needs, and the facts checked on the way. Read *How to read
  this* first. Start at Phase 0.

## Standing rules, decided 2026-11-04

- **C++20 on JUCE 9.0.5**, pinned at the same commit as WFS-DIY. VST3 and AU first through
  JUCE's own wrappers; CLAP later through a shim of our own against the CLAP C API (plan
  Phase 5b). Neither `clap-juce-extensions` nor `clap-wrapper` is in the build.
- **GPLv3** for the plugin and the client library. File headers follow go.dot's form and read
  `Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle`.
- **The Core profile adds no field to OSCQuery.** No UUID, no `TIMETAGS`, no `REPLY_PORT`.
  Anything the plugin needs beyond the proposal is a plugin-side setting or lives in the device's
  own tree. OSCQuery is itself a proposal that needs support, not a rewrite.
- **No typed OSC address anywhere in `src/`.** Addresses appear only in test fixtures.
- **Standardisation is out of scope.** The profile draft is published in `docs/spec/`; who
  adopts it is not this project's deliverable.
- **Commits go straight to `main`** until Julien Bayle starts contributing; pull requests from
  then on. Never rewrite history on `main`.

## Code to reuse, and where it is

Sibling repositories on the author's machine, both GPLv3, both on JUCE 9.0.5:

- **WFS-DIY** (`../WFS_DIY_v1`): `Plugin/Source/Shared/OscQueryClient.{h,cpp}` (HTTP GET,
  websocket LISTEN/IGNORE, `PATH_CHANGED`), `OscTransport`, `RateLimiter`, and the Bridge
  shared-library pattern (`BridgeApi.h`, `BridgeLoader`). `Source/Network/OSCQueryServer.cpp` is
  the first conformance subject. `Plugin/Scripts`, `Plugin/Installer` and
  `.github/workflows/plugins-ci.yml` are the installer and CI patterns to copy.
- **go.dot** (`../go.dot`): `src/wfg/engine/oscquery/Mdns.{h,cpp}` and `Discovery.{h,cpp}`,
  multicast DNS written in-house: codec, browser on every interface, announcer.

Copied files keep their original header and gain a one-line origin note. Nothing is taken from
ossia, libossia or any other OSCQuery implementation.

## The bench, as of 2026-11-04

- **Hosts on this Windows machine:** Reaper and Samplitude (both VST3), Ableton Live. No
  Bitwig, no Nuendo.
- **OSCQuery applications on hand**, as devices to bind, as sources to record from, and as
  conformance subjects: WFS-DIY (a Release build at
  `../WFS_DIY_v1/Builds/VisualStudio2022/x64/Release/App/`), Chataigne, ossia score, and go.dot
  (`../go.dot`). Each serves a tree with LISTEN, so a value moved in any of them is a record
  test without a hardware controller.
- **Other machines on the author's farm:** a Mac mini for macOS (AU, Logic if installed,
  notarisation) and a NUC running Ubuntu for Linux. Access details from the author when a phase
  needs them.
- `cmake`, `pluginval` and `clap-validator` are not on `PATH` here; WFS-DIY's plugin build uses
  the Visual Studio generator through `Plugin/Scripts/build-all.ps1`.
