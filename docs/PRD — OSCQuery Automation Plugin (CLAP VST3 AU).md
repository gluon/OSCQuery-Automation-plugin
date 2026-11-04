# PRD — OSCQuery Automation Plugin (CLAP / VST3 / AU)

Nov 4, 2026 · @Pierre-Olivier Boulant

## Summary

A generic DAW plugin that records and plays back automation curves to and from any OSC-controlled device, with the OSC address space discovered over OSCQuery instead of typed into an XML file. Shipped as CLAP, VST3 and AU from one codebase, open source, developed as the reference client for an OSCQuery discovery profile to be proposed to AES.

Repository: [github.com/pob31/OSCQuery-Automation-plugin](https://github.com/pob31/OSCQuery-Automation-plugin)

**Problem.** Today's OSC automation plugins (IRCAM ToscA and its successor OSCar, the d&b Soundscape, Holophonix, L-ISA and WFS-DIY plugins) either require a hand-written mapping file or hard-code one manufacturer's parameter set. Each DAW session ends up tied to a device it cannot describe, and heterogeneous rigs (WFS + Ambisonics + haptic floor at PRISM, for instance) have no common way to expose parameters to a DAW.

**Approach.** Discovery is done once, at setup: the plugin browses the device tree over OSCQuery, the user binds each automation slot to a path, and the bindings are saved in the session. After that, the plugin behaves like a static ToscA-style plugin, so hosts never see a changing parameter list. Discovery (finding devices, reading trees) is the subject; namespaces such as ADM-OSC are deliberately out of scope.

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

## Users and use cases

| User | Situation | What they need from the plugin |
| --- | --- | --- |
| Sound designer in a DAW (Reaper, Nuendo, Live, Logic) | Writes spatial moves against a renderer during creation | Browse the renderer's tree, bind azimuth/x/y/gain per track, draw curves, hear them played back |
| Mixing engineer with a controller | Moves objects from a fader or tablet while the DAW records | `LISTEN` feedback from the device written into automation lanes in Write/Touch/Latch |
| Touring tech | Reopens the session at a new venue with a different renderer IP | Rebind by device name, see which slots are unresolved, fix them in one dialog |
| Research platform (PRISM) | WFS, Ambisonics and haptic floor in one room with no shared namespace | One plugin per track driving any of the three devices, chosen from the browser |
| Device manufacturer (Holophonix, d&b, L-Acoustics, Fletcher Machine) | Asked to add OSCQuery | A client that proves the profile works and a test matrix showing where their implementation falls short |
| Show control (Go.dot, QLab, ossia) | Needs the same bindings outside the DAW | Bindings exported as JSON so other clients can reuse them |

## Discovery: finding devices and reading trees

Discovery happens in the plugin editor, never in the audio thread, and only when the user opens the browser or asks to rebind.

**Finding devices**

- Browse `_oscjson._tcp` via DNS-SD (mDNS) on all active interfaces. Show the `NAME` from `HOST_INFO`, host and port.
- Manual entry fallback: host and port typed by the user. Required, because multicast often does not cross VLANs on show networks.
- Recent devices list, persisted in the plugin's global settings (not the session), keyed by `NAME` plus last known host:port.
- Each discovered device is probed with `GET /?HOST_INFO` before it is listed. Devices that fail the probe show as unreachable, not hidden.

**Reading the tree**

- `GET /` for the full tree by default; lazy subtree `GET /<path>` when the root reports more than a configurable node count (default 5000) or the device advertises a depth limit.
- Only nodes with `TYPE` and `ACCESS` including write are bindable for playback; nodes with `ACCESS` including read are bindable for record. Container nodes are browsable, not bindable.
- Per node, the plugin reads and stores: `FULL_PATH`, `TYPE`, `RANGE`, `UNIT` (when present), `ACCESS`, `DESCRIPTION`, `CLIPMODE`, `CRITICAL`.
- Supported `TYPE` for v1: `f`, `i`, `T`/`F` (as a toggle), and multi-argument float nodes (e.g. `fff` for xyz) bound as one slot per argument.
- Change notifications over the websocket (`PATH_REMOVED`, `PATH_RENAMED`, `PATH_CHANGED`) mark affected slots as stale in the UI. The plugin never rebinds on its own.

**Conformance the plugin assumes from the device (the Core profile)**

1. `HOST_INFO` with `NAME`, `OSC_PORT`, `OSC_TRANSPORT`, as the OSCQuery proposal defines them; `NAME` stable across reboot. The profile adds no field to `HOST_INFO` (decided 2026-11-04: the proposal needs support, not a rewrite).
2. A static tree whose paths survive reboot and session reload.
3. `TYPE` and `RANGE` on every bindable leaf.

Everything else (`LISTEN`, change notifications, websocket OSC) is the Live profile and is detected at runtime from `EXTENSIONS`.

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

## Host parameter model per format

The plugin never changes its parameter count at runtime. It changes names, grouping and hidden flags only.

| Capability | CLAP | VST3 | AU |
| --- | --- | --- | --- |
| Stable parameter identity | `clap_id` per slot | `ParamID` per slot | `AudioUnitParameterID` per slot |
| Rename after bind | `rescan(CLAP_PARAM_RESCAN_INFO)`, immediate, while active | `restartComponent(kParamTitlesChanged)`; Reaper honours it, others inconsistently | `kAudioUnitProperty_ParameterInfo` change notification; Logic partial |
| Hierarchical grouping | `module` path from the OSC path (`/wfs/input/12`) | Flat; group in name prefix | `clump` ID per device |
| Hide unbound slots | `CLAP_PARAM_IS_HIDDEN` | Not available | `IsHidden` flag |
| Drop stale automation | `clap_host_params->clear()` | Not available | Not available |
| Sample-accurate automation | Yes | Yes, if host sends it | Yes |
| Hosts (Nov 2026) | Bitwig, Reaper, FL Studio, Studio One | Everything except Logic and Pro Tools | Logic, GarageBand, Live and Reaper on macOS |

**Decisions**

- CLAP is the reference format: it is the only one where rename, grouping, hide and clear all work as designed.
- VST3 and AU are best-effort for naming: the bound path is always visible inside the plugin editor, so a host that ignores the rename still works.
- Build with JUCE plus `clap-juce-extensions` (or `clap-wrapper` if JUCE's native CLAP support has landed by implementation time). Decision to be taken at milestone 1.
- AAX is deferred; its parameter list is frozen at instantiation and Avid's SDK agreement conflicts with an open-source distribution.

## Transport, send and record directions

**Send (DAW to device)**

- UDP by default to `OSC_IP:OSC_PORT` from `HOST_INFO`; TCP (SLIP-framed) when `OSC_TRANSPORT` is TCP or the node is marked `CRITICAL`.
- **One bundle per device per audio block**, holding every changed slot from every instance in the process. Bundles are about jitter and atomicity, not raw latency: 64 tracks moving xyz become one datagram instead of 192, and the device applies x, y and z together. Bundles stay under the MTU (about 1,400 bytes, 40 to 45 three-float messages) and split beyond that.
- **Timetags.** Devices that honour bundle timetags (WFS-DIY does; most commercial renderers do not) receive timetag = intended playback time and schedule the change themselves. Whether a device honours them is a per-device setting the user ticks in the Monitor, default off, since OSCQuery has no field for it and the profile adds none (decided 2026-11-04). Devices with the setting off receive the immediate timetag.
- **No rate limit by default.** Every block that has changes is sent and the device smooths. The per-slot limiter exists as protection for lossy links or slow devices, default off.
- A shared network thread per device across all plugin instances in the process, so 64 tracks do not open 64 sockets. Instances register with a process-wide device registry; sends from the audio thread go through a lock-free queue to a realtime-priority network thread (a few hundred microseconds). An option sends with non-blocking `sendto` directly from the audio thread for the lowest latency, at the risk of a stall on a full socket buffer; default off.

**Record (device to DAW)**

- Preferred: `LISTEN` on the websocket for each bound path. Incoming values are scaled to 0–1 and pushed to the host as parameter changes through the format's host-callback path (`clap_host_params->request_flush`, VST3 `IEditController` output events, AU `AUParameterListener`).
- Fallback for devices without websocket: the plugin opens a UDP listener and reports its port to the device; how the device learns that port is the single biggest gap in the current OSCQuery proposal and goes to the Open questions.
- Record only writes to the host when the slot's direction includes receive and the host lane is in Write, Touch or Latch, matching the Soundscape plugin's `Rx` behaviour.
- Loop guard: a value the plugin just sent is ignored if echoed back within 50 ms, so send and receive on the same slot do not oscillate.

**Timing**

- **Where the latency is.** Host automation arrives once per block: 1.3 ms at 64 samples, 10.7 ms at 512 at 48 kHz. CLAP and VST3 deliver sample offsets within the block, so the plugin knows the whole block's moves at its start and can send them at once.
- **OSC runs ahead of the audio.** A packet sent at block start reaches the device before the audio for that block leaves the converters, by roughly the DAW's output latency. Net OSC latency is usually negative relative to what is heard, so alignment matters more than speed.
- **Per-device offset.** A signed offset in milliseconds per device, applied to timetags when the device honours them and to a send delay otherwise, to line the renderer up with the DAW's audio. OSC latency compensation, set once per rig.
- No clock or transport is sent. Devices that need transport get it from the DAW's own MTC or LTC, not from this plugin.
- Target on a local network: under 1 ms from the start of block processing to the packet on the wire, excluding the block itself.

## Session persistence and device re-identification

All bindings live in the plugin's state chunk inside the DAW session. No external mapping file.

**State chunk (JSON, versioned)**

- `version`
- `devices[]`: `name`, `last_host`, `last_port`, `tree_hash` (hash of the bindable leaves at bind time), `identity_path` and `identity_value` (optional), `honours_timetags`, `offset_ms`
- `slots[]`: `host_param_id`, `device_index`, `path`, `arg_index`, `type`, `range`, `scaling`, `direction`, `display_name`, `range_overridden`

**Reopening a session**

1. For each device, try the last host:port and compare `NAME` from `HOST_INFO`, and the identity leaf's value when one is set.
2. On mismatch or no answer, browse DNS-SD for the same `NAME`. Several candidates are told apart by the identity leaf, then by `tree_hash`; if still ambiguous, the device shows as unresolved and the user points at it once (in the Monitor or any instance's device pane), and every instance follows.
3. Once found, verify each bound path still exists with the same `TYPE`. Missing or retyped paths mark the slot as stale; it keeps its automation but stops sending.
4. If `tree_hash` differs, show a one-line notice in the editor so the user knows the device changed since the bind.

No automatic rebinding to a different device. A stale slot is fixed by the user in the browser, where the old path is shown next to the new tree.

**Export and import**

- Bindings export to a standalone JSON file so Go.dot, ossia or a second DAW can reuse them.
- Import can address slots by `host_param_id` so a ToscA or OSCar session can be migrated by a small converter script.

## Access control and failure states

The plugin assumes an unauthenticated control network. Reading a tree is harmless; writing is governed by the device's own allow-list (WFS-DIY's client table, Holophonix and d&b remote-control toggles).

- The plugin reads `ACCESS` as reported **to this client**. A device that answers `ACCESS: 1` (read-only) for a client not on its allow-list makes the slot bindable for record but not for send, and the editor says why.
- OSC received over the websocket is treated by the device exactly like UDP OSC, so the query port never becomes a write path that skips the allow-list. The plugin documents this expectation for implementers and the test matrix checks it.
- Every failure is visible in the editor, never silent:

| State | Cause | Shown as |
| --- | --- | --- |
| Unreachable | No `HOST_INFO` answer | Device row greyed, last-seen time |
| Unauthorised | `ACCESS` excludes write for this client | Slot amber, tooltip names the device's allow-list |
| Stale | Path missing or retyped after reopen | Slot red, old path shown, rebind button |
| Range unknown | Leaf without `RANGE` | Slot inactive until min/max entered |
| Echo loop | Send and receive on the same slot oscillating | Loop guard engages, counter shown |
| Dropped sends | Rate limiter active or socket error | Per-device send counter and error log |

## UX: plugin UI, browser, status

The editor has two panes and no modal dialogs: a **device browser** on the left and the **slot table** on the right. Binding is drag from the tree to a slot row, or click a slot then click a leaf.

**Device browser**

- Device list from DNS-SD plus manual entries and recents; each with name, host, identity value when set, reachability dot.
- Tree view of the selected device with search-as-you-type on path and `DESCRIPTION`. Leaves show type, range and unit inline; non-writable leaves are shown but marked.
- A `Copy tree as JSON` button for bug reports to manufacturers.

**Slot table**

- One row per slot: index, display name, device, path, range, scaling, direction, current value, state colour.
- Bulk operations: bind a range of slots to a multi-argument node, apply the same path pattern across instances with `$instance`, clear all stale.
- Range and scaling editable inline; `RANGE` from the device shown greyed when overridden.

**Status bar**

- Per device: packets sent/received per second, websocket state, last error.
- A `Test` button per slot that sends the current value once, for checking the patch without arming automation.

**Headless use**

- Everything in the editor is also reachable through the plugin's own OSCQuery server (the plugin exposes its slot table at `/plugin/<instance>/slot/<n>/...`), so Go.dot or a script can bind slots without opening the editor. Dogfooding the spec.

## Architecture and tech choices

&#91;embedded content: plugin architecture: 3 shims, 1 core per instance, 1 shared registry\]

The three format shims are thin; everything that matters lives in the core and in a process-wide registry, so 64 instances share one OSCQuery client and one socket per device.

- **Language and framework:** C++20 with JUCE, matching WFS-DIY so OSC and tree-browser code can be shared. CLAP through `clap-juce-extensions` unless native JUCE CLAP support is available at milestone 1.
- **OSCQuery client:** a small in-house library (HTTP, websocket, JSON tree model, DNS-SD) published separately, also GPLv3, so Go.dot and other open-source clients reuse it and the test suite links against it. Manufacturers with closed firmware validate against it through the conformance CLI rather than by bundling it; the author holds the copyright, so a JUCE-free permissive extraction stays possible if one asks.
- **Threads:** the audio thread reads slot values only; the registry owns a network thread per device; the editor owns the browser. Lock-free queues between them.
- **No external files:** bindings in the state chunk, recents in the standard per-user settings location.
- **Platforms:** macOS (universal), Windows, Linux (VST3 and CLAP only).
- **Test suite:** a conformance CLI that runs the Core and Live profile checks against any OSCQuery server and prints the matrix row for that device; the plugin's own OSCQuery server is its first test subject.

**Why no master plugin.** All instances share one shared library inside the host process, so the registry is free: nothing to insert, no load order, no track to delete by mistake. A master plugin would need IPC to reach the track instances and would fail in exactly the ways the bridge-style plugins do. What a master would have given, a place to look, is provided by a **Monitor** plugin: it shows the registry's devices, every slot across all instances, packet rates and errors, and holds global settings, but owns nothing. Any instance can open the same view.

Hosts that sandbox plugins per instance (Bitwig's per-plugin mode, Logic with AUv3, Reaper bridging) give each instance its own registry. Everything still works; the device simply sees several clients, and bundling happens per process rather than per DAW.

## Milestones and deliverables

Dates are placeholders to be set once staffing is known; order and gates are the commitment.

| # | Milestone | Deliverable | Gate |
| --- | --- | --- | --- |
| 0 | Core profile draft | 4-page spec of `HOST_INFO` as defined, stable `NAME`, path stability, `TYPE`/`RANGE`, `ACCESS` per client | Reviewed by Holophonix and one ossia maintainer |
| 1 | Framework decision | Spike: 8-slot CLAP + VST3 build, rename works in Reaper and Bitwig | CLAP path chosen (`clap-juce-extensions` vs native) |
| 2 | Discovery and bind | Browser, tree model, bind, send over UDP, state chunk | WFS-DIY driven from Reaper with no typed address |
| 3 | Record | `LISTEN` to host automation, loop guard, Write/Touch/Latch semantics | Controller moves recorded in Reaper and Bitwig |
| 4 | Re-identification | Rebind by `NAME` and identity leaf, pointing once in the Monitor, stale slots, export/import JSON | Session moved between two machines with different IPs |
| 5 | AU and Linux | AU build in Logic and Live; Linux CLAP/VST3 | Same session file opens in all three formats |
| 6 | Conformance CLI | Core and Live checks, matrix output | Run against WFS-DIY, Holophonix, ossia, Chataigne |
| 7 | Public beta and AES draft | v0.9 release, matrix published, draft submitted to AES standards | Manufacturers contacted with their matrix row |

## Interoperability test matrix

Each check is pass/fail, run by the conformance CLI. Rows are devices; the published matrix is the argument made to manufacturers.

| Check | Profile | What passes |
| --- | --- | --- |
| C1 `HOST_INFO` | Core | Answers `GET /?HOST_INFO` with `NAME`, `OSC_PORT`, `OSC_TRANSPORT` |
| C2 Stable `NAME` | Core | `HOST_INFO` `NAME` unchanged across reboot, and the same as the DNS-SD instance name |
| C3 DNS-SD | Core | Advertises `_oscjson._tcp` with the same name and port |
| C4 Typed leaves | Core | Every writable leaf has `TYPE`; floats and ints have `RANGE` |
| C5 Path stability | Core | Tree hash identical after reboot and after session reload |
| C6 `ACCESS` per client | Core | A client outside the allow-list sees `ACCESS: 1` on writable leaves |
| C7 Subtree GET | Core | `GET /<path>` returns that subtree only |
| L1 `LISTEN` | Live | Websocket `LISTEN` streams value changes at the device's update rate |
| L2 Change notifications | Live | `PATH_REMOVED` and `PATH_RENAMED` sent when a parameter disappears or moves |
| L3 Websocket OSC | Live | OSC over the websocket obeys the same allow-list as UDP |
| L4 `CRITICAL` honoured | Live | `CRITICAL` leaves accept TCP OSC |
| L5 Large tree | Live | Tree of 10,000 leaves served within 2 s on a local network |

Initial subjects: WFS-DIY, XOA/Tight-WFS, the PRISM haptic floor controller, Holophonix, ossia score, Chataigne, the plugin's own server. d&b, L-ISA and Fletcher Machine rows are added as they implement.

## Open questions

- [ ] **Reply port for devices without websocket.** The OSCQuery proposal gives no way for a client to tell the device where to send OSC feedback. Candidates: a `REPLY_PORT` query parameter on `GET`, a `/register` convention, or require the Live profile for record. This is the one gap the Core profile cannot leave open if record is a v1 feature.
- [x] **UUID field name.** Closed 2026-11-04: no UUID, and no new `HOST_INFO` field of any kind. OSCQuery is itself a proposal that needs support, not a rewrite. Identity is `NAME`, the tree hash, and an optional identity leaf the user designates in the device's own tree; what that cannot resolve, the user resolves by pointing once. The same rule removes the `TIMETAGS` field.
- [ ] **Slot count variants.** Separate plugin IDs per size (8/16/32/64/128) or one plugin with a large fixed pool and hidden slots? Hidden works in CLAP and AU, not VST3, which argues for variants.
- [x] **Licence.** Decided 2026-11-04: GPLv3 for the plugin and the client library, matching WFS-DIY and go.dot so their OSCQuery and mDNS code is reused as is. JUCE (AGPLv3 tier) and the VST3 SDK (GPLv3 or Steinberg agreement) make an open-source binary GPL in practice regardless of the source licence, so MIT bought nothing for the plugin. Cost: manufacturers cannot bundle the library in closed firmware; they run the conformance CLI instead.
- [ ] **JUCE CLAP status.** Verify whether JUCE has shipped native CLAP support before milestone 1; it changes the build but not the design.
- [x] **Nuendo.** Decided 2026-11-04: Nuendo uses VST3 with best-effort naming. The bound path is always visible in the plugin editor, so a lane that keeps its `(unbound)` or stale title still works. Nuendo's actual `kParamTitlesChanged` behaviour is measured and written down in milestone 5, not negotiated with Steinberg.
- [ ] **AES route.** Standards committee working group (new project under the networked-audio group) versus an AES technical document first. Needs a sponsor from a member company; d&b or L-Acoustics would carry weight.
- [ ] **Institutional home for the spec.** The plugin's upstream stays under pob31, and any lab or venue that adopts it forks. The spec draft needs a home that is not a single person: an AES working group, or a body such as the OCA Alliance or the ADM-OSC workgroup willing to host a discovery profile.
