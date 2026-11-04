# OSCQuery Automation Plugin — Development Plan (suggestion for Claude Code)

**Draft 0.1**, 2026-11-04, derived from the PRD of the same date. A proposed phase order, not a
schedule. Each phase ends with something runnable in a DAW and a test that stays in CI. Phases
will be broken into subphases as they go.

**Decided 2026-11-04, by the author:** C++20 on JUCE 9 for the plugin and the client library;
**GPLv3** for both, replacing the PRD's MIT. Rust was considered and set aside: it removes JUCE's
AGPL from the plugin but not the VST3 SDK's GPL, and nih-plug's static parameter model does not
fit a plugin whose whole point is renaming parameters after bind. The PRD's licence lines are
amended. **Also decided the same day: the Core profile adds no field to `HOST_INFO`.** OSCQuery
is itself a proposal that needs support, not a rewrite. No UUID, no `TIMETAGS`. Device identity
is `NAME`, the tree hash, and an optional identity leaf the user designates in the device's own
tree; timetag support is a per-device setting. The PRD is amended accordingly.

---

## How to read this

- **Read the PRD first.** Its *Goals and non-goals* and *Binding model* sections are the review
  criterion for every PR. A `CLAUDE.md` holding those two sections verbatim is Phase 0 work.
- **Core before shims, library before core.** Nothing reaches a plugin editor before the core
  exposes it and a headless test drives it. The editor is a client of the core; the three format
  shims are thin.
- **Anything the PRD lists under *Open questions* is not decided by the implementer.** Surface
  it, propose, wait. The ones that block a phase are repeated under that phase's *Needs from the
  author*.
- **Spikes are throwaway.** They live in `spikes/`, never migrate into `src/`, and each ends with
  a written pass/fail in `docs/spikes/`.
- **Small PRs per subphase.** One concern per PR; the test fixture is part of the PR.
- **Nothing is typed by hand that the device can tell us.** If an implementation step needs an
  OSC address written into code or into a test, the step is wrong or the test is a fixture of a
  device tree, never a shortcut.

Sizes are relative: **S** days, **M** weeks, **L** several weeks. No dates.

---

## Facts checked on 2026-11-04 that the PRD left open

These change the build, not the design. Each is re-checked at the phase that depends on it.

| Fact | Where checked | Consequence |
| --- | --- | --- |
| JUCE 9.0.5 is the version WFS-DIY and go.dot build on; its `juce_audio_plugin_client` has AAX, AU, LV2, Standalone, Unity and VST3 targets and **no CLAP** | `ThirdParty/JUCE` in both repos | The PRD's "native JUCE CLAP if it has landed" branch is closed for now. Phase 1 chooses between the two wrappers below. |
| `clap-juce-extensions` documents JUCE 6, 7 and 8; JUCE 9 is not mentioned. It exposes `clap_juce_audio_processor_capabilities` and `clap_juce_parameter_capabilities` but its README does not say whether hidden flags, info rescans or `clap_host_params->clear()` are reachable | github.com/free-audio/clap-juce-extensions; a JUCE forum post says the 8.0.11 `juce_audio_processors_headless` split broke it | Not in the build (decided 2026-11-04). Phase 1 spends one day seeing whether it compiles on 9.0.5, as information only; its source is read in Phase 5b for how a JUCE editor is embedded in a CLAP window. |
| `clap-wrapper` turns a native CLAP into VST3, AUv2, AUv3 and a Standalone; AAX is dual-licensed (GPL-3 or Avid's agreement) | github.com/free-audio/clap-wrapper | Not in the build (decided 2026-11-04). VST3 and AU come from JUCE's own wrappers; CLAP from an in-house shim in Phase 5b. |
| Upstream JUCE, checked 2026-11-04: no file, commit, branch or changelog entry mentions CLAP on `develop`; the Q3 2025 roadmap promised it for JUCE 9, which has shipped through 9.0.5 without it | github.com/juce-framework/JUCE; juce.com roadmap posts | Nothing waits on JUCE. If native CLAP lands later it is a third way to build the same shim interface, not a design change. |
| WFS-DIY's `HOST_INFO` emits `NAME`, `OSC_PORT`, `OSC_TRANSPORT` (UDP), `WS_PORT` and `EXTENSIONS` for ACCESS, VALUE, RANGE, DESCRIPTION, TYPE, CLIPMODE, LISTEN. No `OSC_IP`, no mDNS announcement, `PATH_CHANGED` only (no `PATH_REMOVED`/`PATH_RENAMED`), `ACCESS` is global, not per client | `Source/Network/OSCQueryServer.cpp` in WFS-DIY | WFS-DIY as the first conformance subject fails C3, C6 and L2 today. Companion work in WFS-DIY is listed per phase below. |
| go.dot already has multicast DNS written in-house (`src/wfg/engine/oscquery/Mdns.{h,cpp}`, `Discovery.{h,cpp}`): a codec, a browser on every interface sharing port 5353 with the system responder, and an announcer | go.dot repo, built 2026-10-17 | The client library's DNS-SD comes from there, not from a new implementation or a platform SDK. |
| WFS-DIY's plugin suite has `OscQueryClient` (HTTP GET, websocket LISTEN/IGNORE, `PATH_CHANGED`), `OscTransport` (UDP send incl. three-float messages), `RateLimiter`, and a *Bridge* pattern: a shared dynamic library loaded by every plugin binary with a versioned ABI | `Plugin/Source/Shared` in WFS-DIY | The library's HTTP/websocket layer starts from `OscQueryClient`; the process-wide registry starts from the Bridge pattern. |
| WFS-DIY and go.dot are **GPL-3**; the PRD put the plugin and the library under **MIT**; JUCE 8+ is AGPLv3 or commercial, the VST3 SDK GPLv3 or Steinberg's agreement | both `LICENSE` files; juce.com; steinberg.net | An open-source binary is GPL in practice whatever the source licence says. Decided 2026-11-04: **GPLv3 for plugin and library**, so files from WFS-DIY and go.dot are copied as they are, JUCE types included. The library is not bundleable in closed firmware; manufacturers use the conformance CLI. |
| WFS-DIY's plugin CI builds on windows-2025 (VS 2026), macOS and Linux and uploads installers; signing and notarisation on release only | `.github/workflows/plugins-ci.yml` in WFS-DIY | Phase 0 copies the pattern. |

---

## Where code comes from

| Piece | Origin | What changes |
| --- | --- | --- |
| HTTP GET, websocket, LISTEN/IGNORE, notifications | WFS-DIY `Plugin/Source/Shared/OscQueryClient` | Loses the WFS-specific float-only callback; gains a typed tree model, `HOST_INFO` parsing with `EXTENSIONS`, subtree GET, `PATH_REMOVED`/`PATH_RENAMED`, reconnect with backoff |
| DNS-SD browse and announce | go.dot `src/wfg/engine/oscquery/Mdns`, `Discovery` | Extracted with no dependency on go.dot's engine; the announcer serves the plugin's own server in Phase 6 |
| OSC encode/decode, UDP send | WFS-DIY `OscTransport` plus `juce_osc` | Adds bundles with timetags, MTU splitting, SLIP over TCP, a non-blocking `sendto` path |
| Rate limiter | WFS-DIY `RateLimiter` | Becomes per slot and default off, per the PRD |
| Shared library across plugin binaries | WFS-DIY Bridge (`BridgeApi.h`, `BridgeLoader`) | Hosts the device registry so every slot-count variant and every format in the process shares one client per device (see Phase 3) |
| Websocket/HTTP transport | `juce_simpleweb` (vendored in both repos) | Unchanged; TLS compiled out as in WFS-DIY |
| Installer scripts, CI | WFS-DIY `Plugin/Scripts`, `Plugin/Installer`, `plugins-ci.yml` | Renamed; adds a CLAP install path and pluginval |

Nothing is taken from ossia, libossia or any other OSCQuery implementation. The library is the
reference client; it has to be independently written to be worth anything as one. With both
source repos and this one under GPLv3, the copies above need no relicensing; the headers keep
their author and gain an origin line.

---

## Phase 0 — Core profile draft, repo scaffold · M

**Goal:** a 4-page spec other people can review, and a repo that builds an empty plugin on three
platforms.

- **Core profile draft** in `docs/spec/core-profile-draft-0.1.md`: `HOST_INFO` fields as the
  OSCQuery proposal defines them and nothing more, `NAME` stable across reboot and equal to the
  DNS-SD instance name, path stability, `TYPE`/`RANGE` on every bindable leaf, `ACCESS` as
  reported to this client, subtree GET. Each clause maps to one check in the PRD's
  interoperability matrix (C1–C7); the Live profile (L1–L5) is a second, shorter section. The
  draft says in one paragraph why it adds no field: a profile of the proposal is easier to adopt
  than an amendment to it.
- Repo scaffold: CMake, submodules pinned (JUCE 9.0.5 at the same commit as WFS-DIY,
  `juce_simpleweb`, `clap` and `clap-helpers` for Phase 5b), GPLv3 file
  headers in go.dot's form reading `Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle`
  (decided 2026-11-04; Julien Bayle is a contributor to the repository), a
  `THIRD_PARTY_NOTICES.md` from the first dependency,
  `docs/` holding the PRD, this plan and the spec, `CLAUDE.md` = PRD *Goals and non-goals* +
  *Binding model* verbatim.
- Layout fixed now so Phase 2 and 3 do not argue about it:

  ```
  lib/oscquery/     the client library, its own CMake target and tests, no JUCE GUI dependency
  src/core/         slots, registry, state chunk, scaling, send and record paths
  src/shims/        juce/ (one AudioProcessor: VST3 and AU), clap/ — thin
  src/editor/       browser, slot table, status bar, Monitor
  tools/conform/    the conformance CLI (Phase 8)
  tests/            unit (library, core), host-less plugin tests, a mock OSCQuery device
  spikes/           throwaway
  ```

- CI on GitHub Actions for Linux / macOS / Windows copied from WFS-DIY's `plugins-ci.yml`:
  build, run tests, upload unsigned artefacts. pluginval added as soon as there is a VST3 to feed it.
- A **mock OSCQuery device** in `tests/mock-device/`: a tiny server (reusing `juce_simpleweb`)
  that serves a tree from a JSON fixture, answers `HOST_INFO`, accepts LISTEN and can be told to
  rename, remove or retype a path mid-test. Every later phase's tests drive it; the conformance CLI
  is first pointed at it.

**Done when:** CI is green on all three platforms building a VST3 that does nothing; the spec draft
has gone out for review; the mock device serves a fixture tree and a test reads it back.

**Needs from the author:**

- Nothing blocking. Copyright holders are decided (Pierre-Olivier Boulant, Julien Bayle). Files
  copied from WFS-DIY and go.dot keep their original header and gain a one-line origin note.
- The two reviewers for the spec (Holophonix, one ossia maintainer).

---

## Phase 1 — Bench the formats shipped first · S

**Goal:** the rename behaviour of VST3 and AU measured in the hosts that matter, before core
code depends on it.

**Decided 2026-11-04:** VST3 and AU first, through JUCE's own wrappers; CLAP later, through a
shim of our own written against the CLAP C API (Phase 5b). Neither `clap-juce-extensions` nor
`clap-wrapper` is in the build. JUCE 9.0.5 has no native CLAP and the announcement has no date,
so nothing waits on it; if it lands, it is a third way to build the same shim interface.

- A throwaway `juce_add_plugin` in `spikes/`, 8 slots with the PRD's parameter model: normalised
  float, stable ID, name changed at runtime through `updateHostDisplay` with
  `parameterInfoChanged`, `(unbound)` names, AU `IsHidden` and `clump`.
- The matrix, filled per host and written into `docs/spikes/phase1-formats.md`:

| Check | Where |
| --- | --- |
| Rename a slot while active; the automation lane name follows | Reaper, Samplitude, Live, Bitwig, Nuendo (VST3); Logic (AU) |
| Hide an unbound slot; it leaves the host's parameter list | Logic (AU) |
| A stale lane after rebind: what the host shows, what the user has to do | all six |
| pluginval at strictness 5 on the VST3, `auval` on the AU | CI, and the Mac mini |

Reaper, Samplitude and Live are on the author's Windows machine; the Mac mini on the farm
covers AU. Bitwig and Nuendo rows are filled when a licence is at hand, and the report says
which rows are still empty.

- **One day, information only:** clone `clap-juce-extensions` into `spikes/` and try to build it
  on JUCE 9.0.5. The result goes in the report. It is not a dependency either way.
- **Registry location, settled by consequence:** with JUCE each plugin ID is its own binary, so
  five slot-count variants are five binaries per format, and VST3 cannot hide slots, so one
  binary with hidden slots is not an option there. The process-wide registry therefore lives in
  a shared library (WFS-DIY's Bridge pattern), built in Phase 3. The variants are five plugin
  IDs, as the PRD leant.

**Done when:** the report exists, the five-ID decision is written into the PRD, and the spike is
deleted from `spikes/`.

**Needs from the author:** access to the Mac mini; Bitwig and Nuendo licences, or an agreed
empty row for each until one turns up.

---

## Phase 2 — The client library · M

**Goal:** `lib/oscquery` reads any OSCQuery server, headless, under test, with no plugin code
above it.

- **Tree model**: node with `FULL_PATH`, `TYPE`, `RANGE`, `UNIT`, `ACCESS`, `DESCRIPTION`,
  `CLIPMODE`, `CRITICAL`, `VALUE`, children; multi-argument types split into per-argument views;
  a *bindable* predicate per the PRD (`TYPE` present, `ACCESS` includes write for send, read for
  record, containers never). A `treeHash()` over bindable leaves for Phase 5.
- **HTTP**: `GET /?HOST_INFO`, `GET /`, `GET /<path>` for subtrees, lazy loading above the node
  threshold (default 5000). Timeouts and a reachability probe that yields *unreachable* rather
  than an exception.
- **Websocket**: LISTEN / IGNORE, value stream, `PATH_CHANGED`, `PATH_ADDED`, `PATH_REMOVED`,
  `PATH_RENAMED`, OSC over websocket for `CRITICAL`; reconnect with backoff, re-LISTEN on
  reconnect. Features gated by `EXTENSIONS`, never assumed.
- **DNS-SD**: browse `_oscjson._tcp` on all interfaces (from go.dot), an announcer for Phase 6,
  a manual host:port entry path that goes through the same probe.
- **OSC**: messages, bundles with timetags, MTU-aware splitting, UDP and SLIP-over-TCP senders,
  a UDP receiver for the Phase 4 fallback.
- **Recents store**: devices keyed by `NAME` plus last host:port in the per-user settings
  location, with the standard JUCE `PropertiesFile` paths; the identity leaf's path and value
  kept alongside when set.
- Tests against the mock device: full tree, lazy subtree, every notification, a device that
  answers `HOST_INFO` and nothing else, a device without websocket, a 10,000-leaf tree in under
  2 s (L5, measured on loopback). Serialisation tests run under `fr_FR` as well as `C` because
  `RANGE` and `VALUE` are decimals.

**Done when:** a command-line example in `lib/oscquery/examples/` prints WFS-DIY's tree, subscribes
to a path and prints its changes, with no address typed; the test suite passes on three platforms.

**Needs from the author:** nothing blocking. The library's public name, if it is to be its own
repo later (Phase 8 splits it).

**Companion in WFS-DIY:** none yet; the library must cope with WFS-DIY exactly as it is.

---

## Phase 3 — Slots, registry, send · L

**Goal:** WFS-DIY driven from Reaper with no typed address. PRD milestone 2.

- **Slot model** (`src/core`): the PRD's table as a struct; host 0–1 ↔ device units with linear,
  log and exponential scaling; `RANGE` missing → slot inactive until min/max entered; `T`/`F` as a
  toggle; `i` with rounding; per-argument slots for `fff` grouped into one message on send using
  the last known value for arguments not being automated.
- **State chunk**: the PRD's JSON, `version` from the first commit, a loader that reads every
  past version (fixtures kept in `tests/state/`), unknown fields preserved on round-trip.
- **Device registry**: process-wide, one entry per resolved device (`NAME` plus identity leaf
  value, resolved to one host:port), one network thread per device at
  realtime priority, a lock-free queue from each instance's audio thread. Instances register in
  `prepareToPlay` (or the CLAP `activate`) and again whenever the host restarts the process
  (Logic's `AUHostingService` does this; WFS-DIY learnt it the hard way). The registry lives in
  a **shared core library** loaded by every plugin binary, after WFS-DIY's Bridge: versioned ABI,
  exact-match version check, installed next to the plugins. Every variant, and later the CLAP
  shim, links the same library, so one process has one registry whatever mix of binaries it
  loads.
- **Send path**: one bundle per device per audio block holding every changed slot from every
  instance, split at the MTU, immediate timetag unless the device's *honours timetags* setting is
  on, in which case timetag = intended playback time plus the per-device offset. Per-slot rate limiter,
  default off. TCP when `OSC_TRANSPORT` says so or the node is `CRITICAL`. The optional direct
  `sendto` from the audio thread, default off, behind a setting with a warning in its tooltip.
- **Shims**: VST3 and AU through one JUCE `AudioProcessor` subclass, best-effort naming
  (`kParamTitlesChanged`, `(unbound)` names; `IsHidden` and `clump` on AU), AU measured in Logic
  in Phase 7. The shim interface is written for CLAP's capabilities (rename, hide, clear, module)
  and the JUCE shim answers *unsupported* where a format must; the CLAP shim arrives in Phase 5b
  and no core code moves for it.
- **Editor, first cut**: device list (found, recent, manual), tree with search, slot table with
  click-slot-then-click-leaf binding, inline range and scaling, the *Test* button. Enough to bind;
  not yet the full UX.
- **Audio thread lipogram** test as in go.dot: no allocation, locks, exceptions, syscalls or
  logging in the process callback; instrument and assert.
- Measurement: time from start of block to packet on the wire on loopback, logged by a test
  harness, target under 1 ms as the PRD states.

**Done when:** in Reaper on Windows and macOS, a user opens the plugin, picks WFS-DIY from the
found list, binds x/y/z of one input to three slots, draws curves, and the source moves; 32 tracks
doing the same produce one bundle per block on the wire (verified by capture); the session reloads
with its bindings.

**Needs from the author:** the default slot count to ship first (this plan says 32 alone until
Phase 7 adds the other variants); whether `$instance` lands here or in Phase 6 (plan says 6).

**Companion in WFS-DIY:** `OSC_IP` in `HOST_INFO` (cheap, and the library otherwise falls back to
the HTTP host). Nothing else.

---

## Phase 4 — Record · M

**Goal:** controller moves recorded in Reaper and Bitwig. PRD milestone 3.

- LISTEN on each bound path whose direction includes receive; incoming values scaled to 0–1 and
  pushed to the host through the format's path: CLAP out-events with `PARAM_GESTURE_BEGIN` /
  `PARAM_VALUE` / `PARAM_GESTURE_END` and `request_flush` when not processing; VST3
  `beginEdit`/`performEdit`/`endEdit`; AU gesture events in Phase 7.
- **Write/Touch/Latch is the host's decision, not the plugin's.** The plugin's job is the
  gesture: open it on the first incoming value, close it after an idle timeout (default 200 ms,
  setting). A host in Read mode ignores the push; that is the Soundscape `Rx` behaviour without a
  plugin-side mode switch. The editor shows the incoming value either way.
- **Loop guard**: a value the plugin sent is ignored if echoed back within 50 ms (setting), per
  slot, with a counter in the status bar.
- Slots with direction *both*: send and receive on one slot without oscillating; the test drives
  the mock device to echo with a delay above and below the guard window.
- **Devices without LISTEN** (decided 2026-11-04; WebSockets are optional in the proposal, so
  HTTP-only devices are conformant): the library's UDP receiver is wired, one port per process,
  and the plugin shows that port and its own address in the editor and the Monitor. The user
  enters them in the device's own configuration, as ToscA and OSCar users do today. The editor
  says on such a device that record is waiting for the device to be told the port, and the
  status bar shows the first packet arriving. Nothing is added to the spec for it.
- Test: the mock device in HTTP-only mode is told the plugin's port by the test harness and sends
  values; the lane records exactly as it does over LISTEN.

**Done when:** a value moved in WFS-DIY, Chataigne, ossia score or go.dot writes a lane in
Reaper (and Bitwig once CLAP exists, Phase 5b) in Touch mode, the lane plays back to the
device, and the loop guard counter stays at zero during playback; a session with 32 receiving
slots shows no missed values at the device's update rate. The three applications are the record
sources, so no hardware controller is needed on the bench.

**Needs from the author:** the gesture idle timeout default. The reply port is decided (LISTEN
when declared, else a UDP port shown in the editor and configured on the device side).

**Companion in WFS-DIY:** none required; LISTEN exists.

---

## Phase 5 — Re-identification, stale slots, export · M

**Goal:** a session moved between two machines with different IPs. PRD milestone 4.

- Reopen sequence as the PRD states: last host:port and compare `NAME` (and the identity leaf's
  value when set); on mismatch or silence, DNS-SD by `NAME`, candidates told apart by identity
  leaf then `tree_hash`, else unresolved; each bound path verified for existence and `TYPE`;
  stale slots keep automation and stop sending; `tree_hash` mismatch shows a one-line notice. No
  automatic rebinding, ever.
- **Identity leaf**: in the device pane the user can pick any read-only leaf of the device's tree
  as its identity (a serial number, a hostname node, whatever the device already publishes). The
  plugin stores path and value and compares the value on reopen. Nothing is asked of the device
  beyond what it already serves.
- **The target is pointed at in one place** (decided 2026-11-04, in answer to "is this where a
  master plugin comes in"): the registry maps each device identity to its current host and port,
  process-wide, so slots never hold an address. The Monitor's device list, and the device pane of
  every instance, show each device the session refers to with name, identity value if set, last
  address and reachability; the user picks a found device or types an address, and every instance
  follows at once. Each instance writes the new address into its own chunk on the next save, and
  the recents store keeps it per user, so a sandboxed instance with its own registry finds it
  too. A device alone under its name is re-found over DNS-SD with no click; pointing by hand is
  the path for two devices sharing a name with no identity leaf to tell them apart, or for a
  network where multicast does not cross. This is what a master plugin would have done, without a
  track to delete, a load order, or IPC.
- Notifications over the websocket (`PATH_REMOVED`, `PATH_RENAMED`, `PATH_CHANGED`) mark slots
  stale live; the editor shows the old path next to the current tree with a rebind button.
- **Export/import** of bindings as standalone JSON addressed by `host_param_id`; a small converter
  script in `tools/` from a ToscA or OSCar mapping as the worked example of import.
- Failure states table from the PRD (unreachable, unauthorised, stale, range unknown, echo loop,
  dropped sends) all reachable and all driven by the mock device in tests.
- `ACCESS` read as reported to this client: a mock-device test where the same leaf answers
  `ACCESS: 1` to one client and `3` to another.

**Done when:** a session saved against the mock device on one address reopens against the same
device on another address with every slot live; two mock devices sharing a `NAME` are told apart
by their identity leaf; a session whose device is not found by DNS-SD reopens with it unresolved,
is pointed at the new address once in the Monitor, and every instance goes live; a renamed path shows stale and is rebound in one click; an exported JSON reimports into a fresh instance identically; all of it in CI with two
mock devices.

**Needs from the author:** nothing blocking.

**Companion in WFS-DIY:** `PATH_REMOVED` / `PATH_RENAMED` when a channel is deleted or moved
(L2); a read-only leaf carrying something unique per installation, if WFS-DIY has one to
publish, so it can serve as an identity leaf (optional, not a profile requirement). Per-client `ACCESS` (C6) if WFS-DIY's
client table is to be reflected; otherwise it stays a documented gap in its matrix row.

---

## Phase 5b — Our own CLAP shim · M

**Goal:** CLAP, the reference format, from a shim written against the CLAP C API with
`clap-helpers`, depending on neither JUCE's roadmap nor `clap-juce-extensions`. Decided
2026-11-04. Placed here because Bitwig is a record gate and CLAP's gesture path is the cleanest
to test, and because the core has to exist before a shim has anything to call.

- Entry point and a **factory listing all five slot-count variants in one binary**; lifecycle
  (init, activate, start and stop processing, reset); state through the same JSON chunk over
  `clap_ostream` and `clap_istream`; one stereo audio port in and out, copied.
- **Params extension**: count and info per slot, `CLAP_PARAM_IS_HIDDEN` for unbound slots,
  `rescan(CLAP_PARAM_RESCAN_INFO)` on rename, `clap_host_params->clear()` on a stale lane,
  `module` from the OSC path, in-events with sample offsets, out-events with
  `PARAM_GESTURE_BEGIN` / `PARAM_VALUE` / `PARAM_GESTURE_END` for record, `request_flush` when
  not processing. All of it direct calls into the same core as the JUCE shim.
- **The JUCE editor in the host's window**: `create`, `set_parent`, `set_size`, `show`, `hide`
  on Windows, macOS and X11; JUCE initialised inside a non-JUCE plugin; on Linux the host's
  timer and posix-fd extensions pump JUCE's message loop. `clap-juce-extensions`' source is read
  for how it does this and nothing is taken from it beyond that.
- The CLAP binary links the shared core library from Phase 3, so a CLAP and a VST3 instance in
  one process share one registry and one socket per device.
- `clap-validator` in CI; Reaper and Bitwig on the bench; the rename, hide, clear and module rows
  added to the Phase 1 report as `docs/spikes/phase5b-clap.md`.

**Done when:** the Phase 3 and Phase 4 done-whens pass in Bitwig and Reaper over CLAP; an unbound
slot is absent from the host's list; a stale lane is cleared from the host on rebind; a chunk
saved from the VST3 opens in the CLAP with identical bindings and back; `clap-validator` is
clean on three platforms.

**Needs from the author:** nothing blocking. A Bitwig licence on the bench.

---

## Phase 6 — The editor finished, the plugin's own server, the Monitor · M

**Goal:** the PRD's UX section complete and the headless path dogfooded.

- Drag from tree to slot; bulk bind of a multi-argument node across consecutive slots; `$instance`
  across instances; *clear all stale*; `RANGE` from the device greyed when overridden; *Copy tree
  as JSON*; search on path and `DESCRIPTION`; reachability dot and last-seen time.
- Status bar per device: packets per second each way, websocket state, last error.
- **The plugin's own OSCQuery server**: the slot table at `/plugin/<instance>/slot/<n>/...`
  served over HTTP and websocket from the registry (one server per process, announced over
  DNS-SD by the library's announcer). Binding a slot through it does exactly what the editor does.
  This is the first server the conformance CLI runs against in Phase 8, so it must pass its own
  matrix.
- **Monitor plugin**: a separate plugin ID with zero slots that opens the registry view: devices,
  every slot across instances, rates, errors, global settings. Owns nothing; any instance can open
  the same view from a button.
- Global settings (recents, default rate limit, loop guard window, gesture timeout, direct send)
  in the per-user settings file, editable from the Monitor.

**Done when:** everything in the PRD's UX section is reachable; a script binds a slot over the
plugin's OSCQuery server with no editor open and the host lane renames; the Monitor shows 32
instances against two devices with live counters.

**Needs from the author:** a look at the layout before the drag-bind and bulk operations are
polished; the words for the failure-state tooltips.

---

## Phase 7 — AU, Linux, variants, installers · M

**Goal:** the same session file opens in all three formats. PRD milestone 5.

- AU, built by JUCE since Phase 3, now measured: `kAudioUnitProperty_ParameterInfo` change
  notification, `IsHidden`, `clump` per device, gesture events for record. Logic's partial rename
  behaviour written down in `docs/bench/`.
- Linux VST3 and CLAP (the Phase 5b shim); LV2 only if JUCE gives it for free.
- The slot-count variants (8/16/32/64/128) as plugin IDs, generated from one CMake loop for the
  JUCE formats and listed by the factory for CLAP; all of them share the registry through the
  shared core library from Phase 3.
- Installers from WFS-DIY's scripts: Inno Setup on Windows, `.pkg` with codesign and
  notarisation on macOS, tarball with `install.sh` on Linux; CLAP paths added.
- pluginval on every format in CI; `auval` on macOS.
- A cross-format test: a state chunk saved from the CLAP opens in the VST3 and the AU with the
  same bindings, since the chunk is format-neutral by construction.

**Done when:** one Reaper session on the Mac mini opens the plugin as CLAP, VST3 and AU with
identical bindings; Live 12 and GarageBand run the AU and record (the Mac mini has no Logic,
inventoried 2026-11-04; a Logic row waits for a machine that has it); the NUC runs the Linux
CLAP and VST3 in a host against WFS-DIY on the farm's network; all installers produced from a
clean checkout.

The NUC has REAPER 7.84 (installed 2026-11-04) and a GNOME desktop, but SSH sessions have no
display: before this phase the `pob` user is logged into the desktop, or autologin is enabled,
so `DISPLAY=:0` works over SSH; pluginval and clap-validator are installed on both farm machines
at the same time.

**Needs from the author:** signing identities. Nuendo is decided (VST3, best-effort naming);
this phase writes down what `kParamTitlesChanged` actually does there, in `docs/bench/`.

---

## Phase 8 — Conformance CLI, the library on its own · M

**Goal:** the interoperability matrix produced by a tool, not by hand. PRD milestone 6.

- `tools/conform`: one executable, `conform <host:port>` or `conform --browse`, runs C1–C7 and
  L1–L5 as the PRD defines them, prints the matrix row as Markdown and JSON, exits non-zero on
  any Core failure. C5 (path stability across reboot) takes a `--before`/`--after` pair of tree
  snapshots. L5 reports the time, not just pass/fail.
- Run against: the plugin's own server, the mock device (with fixtures that fail each check on
  purpose, as the CLI's own test), WFS-DIY, XOA/Tight-WFS, the PRISM haptic floor controller,
  Holophonix, ossia score, Chataigne.
- The library moves to its own GPLv3 repo with the CLI as its first consumer; the plugin pins it
  as a submodule, and go.dot can replace its own client with it when it wants to.
  `docs/matrix.md` holds the rows with dates and firmware versions.
- The CLI ships as a signed binary per platform on the release page, since a manufacturer
  running it against their device is the one artefact of this project they will touch.

**Done when:** the CLI's own tests pass; rows exist for every subject on the bench; a
manufacturer can run it on their device with one command and get the same row.

**Needs from the author:** bench time with each subject; who at each manufacturer receives their
row.

**Companion in WFS-DIY:** DNS-SD announcement of `_oscjson._tcp` (C3), via the same announcer
the library ships, with the instance name equal to `NAME` (C2). WFS-DIY honours bundle
timetags per the PRD; its bench row records that the per-device setting was ticked.

---

## Phase 9 — Public beta · M

**Goal:** v0.9 on GitHub, the matrix published, the profile draft published with it. PRD
milestone 7. Standardisation is out of scope (removed 2026-11-04): a profile becomes a standard
when manufacturers adopt it, and that is not code.

- Release workflow from WFS-DIY's `plugins-release.yml`: tagged builds, signed installers, a
  changelog.
- README for users (bind in three clicks), README for manufacturers (the Core profile in one
  page and how to run `conform`), README for implementers of other clients (the library).
- Profile draft 0.2 incorporating what the matrix taught, published in `docs/spec/` with the
  release.
- Each manufacturer contacted with their row and the CLI.

**Done when:** the release is public, the matrix page is live, the profile draft is in the
repository, and the issue tracker is open.

**Needs from the author:** who at each manufacturer receives their row.

---

## Cross-cutting, every phase

- **Three-platform CI** green before merge; pluginval from Phase 3 on.
- **RT-safety instrumentation** on the process path stays on in tests.
- **Locale test**: everything that serialises numbers (state chunk, export, the library's JSON)
  runs under `fr_FR` as well as `C`.
- **State chunk fixtures**: every version ever shipped stays loadable; a fixture per version in
  `tests/state/`.
- **Two-instance test** from Phase 3 on: two instances in one process share one registry entry
  per device and produce one bundle.
- **Mock device first**: no test needs a real device; real devices are bench checks recorded in
  `docs/bench/` with the date and firmware.
- **No typed address** anywhere in `src/`; addresses appear only in fixtures.
- **No *(proposed)* spec clause implemented without a recorded yes.**
- Every failure visible in the editor, never silent (PRD *Access control and failure states*); a
  new failure mode gets a row in that table before it gets code.

---

## Needs from the author, collected

None open as of 2026-11-04. The standardisation route and the spec's institutional home were
removed from the PRD the same day: they depend on adoption by manufacturers, which is not this
project's deliverable.

Decided already, all on 2026-11-04: language and framework (C++20, JUCE 9), licence (GPLv3),
copyright holders (Pierre-Olivier Boulant, Julien Bayle), no new `HOST_INFO` fields (no UUID, no
`TIMETAGS`), the target pointed at once through the registry and the Monitor, Nuendo on VST3
with best-effort naming, record over LISTEN when declared and otherwise over a UDP port shown in
the editor and configured on the device side, VST3 and AU first through JUCE's wrappers with our
own CLAP shim in Phase 5b, five slot-count plugin IDs and the registry in a shared core library.
