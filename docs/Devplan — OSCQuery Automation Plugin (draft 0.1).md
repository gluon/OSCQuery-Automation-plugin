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
| WFS-DIY **does not honour OSC bundle timetags**: its parser reads the eight bytes and drops them; bundles apply on arrival from a 16 ms drain on its message thread. Its outgoing bundles carry timetag 1 (checked 2026-11-08) | `spatcore/control/osc/OSCParser.h`, `OSCIngestQueue.h`, `OSCManager::handleIncomingBundle` in WFS-DIY | The PRD's "WFS-DIY does" is stale and amended. Phase 3 builds the timetag path against the mock device only; no bench device verifies scheduling yet. |
| WFS-DIY's OSC router accepts the OSCQuery short form `/wfs/input/<n>/<name>`, the channel in the address, beside its standard form with the channel as the first argument (checked 2026-11-08) | `Source/Network/OSCMessageRouter.cpp`, `GoDotProtocol.h` in WFS-DIY | The tree's `FULL_PATH` is sendable as is, with `TYPE`'s tags. **Confirmed live 2026-11-08** against the running WFS-DIY: a float sent to a per-input leaf's `FULL_PATH` over UDP 8000, no address typed, read back over HTTP with the new value. WFS-DIY's own feedback goes to UDP 9000 on the client (the author, same day; Phase 4's UDP receiver). |

---

## Where code comes from

| Piece | Origin | What changes |
| --- | --- | --- |
| HTTP GET, websocket, LISTEN/IGNORE, notifications | go.dot `src/wfg/engine/oscquery/OscQueryClient::get` (the raw-socket GET, chunked bodies); WFS-DIY `Plugin/Source/Shared/OscQueryClient` as the behavioural reference only (amended 2026-11-08: it has no licence header, parses no `HOST_INFO` and decodes one message; nothing copied) | A typed tree model, `HOST_INFO` parsing with `EXTENSIONS`, subtree GET, all four `PATH_*`, reconnect with backoff: `lib/oscquery/oscquery/Device` |
| DNS-SD browse and announce | go.dot `src/wfg/engine/oscquery/Mdns`, `Discovery` | Extracted with no dependency on go.dot's engine; the announcer serves the plugin's own server in Phase 6 |
| OSC encode/decode, UDP send and receive | go.dot `src/wfg/engine/osc/OscValue`, `OscAddress`, `OscCodec`, `UdpEndpoint`, `SocketBuffers` (decided by the author 2026-11-08, replacing WFS-DIY `OscTransport` plus `juce_osc`: JUCE's byte-level OSC streams are private to its `.cpp` files and `juce::OSCArgument` has no bool, int64, double or time tag) | Adds bundles under the MTU with one timetag (`osc/Bundler`), SLIP and length-prefix framing (`osc/Framing`), a TCP sender (`net/TcpSender`); the non-blocking `sendto` path waits for Phase 3 |
| Rate limiter | WFS-DIY `RateLimiter`, its shape only (latest value per key, compared with what was last sent) | Per bound path, default off, driven by the network thread's clock: no mutex, no timer (`src/core/send/RateLimiter`, 2026-11-08) |
| Realtime-safe encoder and UDP sender | Written here, 2026-11-08: neither repository had a send path a realtime thread may call (go.dot's `UdpEndpoint::send` builds a `juce::String` and may resolve the host; `osc::encode` returns a vector) | `osc/FixedEncoder` (a message or a bundle into a caller's buffer, byte for byte the codec's) and `net/RawUdpSender` (resolved once, non-blocking `sendto`, a full buffer a counted drop) in the library |
| Shared library across plugin binaries | WFS-DIY Bridge (`BridgeApi.h`, `BridgeLoader`), the ABI pattern only: exact-match version, found by walking up from the plugin's own module | One exported table (`registry/RegistryApi.h`) over the registry, in `OSCQuery-Automation-Core`; every binary also links the core and falls back to it, saying so, where WFS-DIY's plugins go silent (2026-11-08) |
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

**Record, 2026-11-04.** Built in four commits on `main`: the spec draft (`docs/spec/`), the
scaffold, CI, the mock device.

- *Decided by the author the same day:* the spec text is CC BY 4.0, the rest GPLv3; the plugin's
  identity is Pix et Bel, `PxBl`, plugin codes `Q008`…`Q128` and `QMon` (fixed for good, see
  `src/shims/juce/CMakeLists.txt`); the shims are `src/shims/juce/` and `clap/` (layout above
  amended).
- *Pins:* JUCE at WFS-DIY's `d60b7a78` (the 9.0.5 tag; go.dot's `7ff644f6` is 9.0.4 plus 26);
  `juce_simpleweb` from go.dot's fork pin `b72ec947`, because WFS-DIY's vendored copy cannot report
  a port-0 bind, which a mock device on an ephemeral port needs; doctest 2.5.3 as its own
  submodule (go.dot borrows Tracktion's); CLAP 1.2.10 and clap-helpers `a380c978`, not built.
  `scripts/check-pins.py` holds `THIRD_PARTY_NOTICES.md` to the gitlinks.
- *Fact corrected:* the VST3 SDK bundled in JUCE 9.0.5 is **MIT** (Steinberg, 2025, its
  `LICENSE.txt`), not GPLv3-or-agreement as the facts table above and the PRD's licence reasoning
  say. It changes nothing decided (JUCE's AGPLv3 still makes the binary GPL in practice) but the
  PRD's sentence is stale.
- *The proposal* (Vidvox README at `e3d8e3b9`) reads two ways in eight places, listed in §6 of
  the spec draft with how a profile client reads each. The draft's *(proposed)* clauses are the
  reviewers' questions.
- *Done-when:* the spec is written and waits for the author to send it to the reviewers; the
  mock device serves `generic-device.json` and `MockDeviceTests.cpp` reads every node back,
  under C and fr_FR; CI builds the do-nothing VST3 on three platforms and pluginval passes it at
  strictness 5.
- *On the farm, the same day, at the mock-device commit:* the Mac mini (macOS 15.8.2, arm64,
  Xcode 26.3) and the NUC (Ubuntu 24.04, GCC 13.3) configure `dev`, build with no warning in our
  code, pass 11 cases and 230 assertions under C and fr_FR, build the Release VST3 (and the AU on
  the Mac) and pass pluginval 1.0.4 at strictness 5 (the NUC headless, `--skip-gui-tests`). The
  check is one script, `scripts/farm-check.sh`, run over SSH on a `git bundle` clone. GitHub warns that
  `ubuntu-latest` moves to Ubuntu 26 from 2026-10-19; CI follows it unless a job breaks.

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

**Record, 2026-11-08.** Report: [`docs/spikes/phase1-formats.md`](spikes/phase1-formats.md);
the spike is deleted (its code is in the history at `aaac2c4`).

- *VST3 rename works in every host but REAPER:* Samplitude 2026 (build 26215), Live 12.4.16 and
  Bitwig 6.1.4 follow on lanes already drawn, during playback too, on both machines where
  installed. REAPER renames the parameter but not a lane already drawn, until reopen or FX
  offline/online.
- *No host loses automation through bind, unbind or rebind, unless slots are hidden.* A rebound
  slot's curve drives the new target; in REAPER under the old title.
- *AU cannot hide without cost:* AUv2 has no `IsHidden` flag; leaving the slot out of the
  parameter list (tried through a JUCE patch in the spike) deletes its automation in REAPER and
  keeps it out of GarageBand. Decided by the author the same day: AU slots are never hidden and
  are named `(unbound)` as in VST3; the PRD's Binding model rule and format table are amended.
- *Mac bundles must be ad-hoc signed as a whole* for the AU registrar to see them; JUCE's CMake
  under Ninja does not do it.
- *Done-when:* the report exists, the five-ID decision is in the PRD (2026-11-04), the spike is
  deleted. Bitwig was installed on both machines on 2026-11-04; Nuendo and Logic rows stay empty;
  clap-juce-extensions builds on JUCE 9.0.5 (information only).

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

**Record, 2026-11-08.** Built in `lib/oscquery/` in two commits on `main` after the target's
first (2a: the target and go.dot's OSC codec; then the rest of the library; then the example and
the records). Report of the bench: [`docs/bench/2026-11-08-phase2-library.md`](bench/2026-11-08-phase2-library.md).

- *Decided by the author the same day:* the library's **working name** is `oqa::oscquery`
  (namespace, target, `<oscquery/...>` includes), renamed when Phase 8 splits it out; **OSC comes
  from go.dot**, not from WFS-DIY's `OscTransport` plus `juce_osc` (the table above is amended);
  the **TCP sender is built now, framing selectable, SLIP the default**, the PRD having decided
  SLIP for the plugin while the spec draft still marks it *(proposed)*.
- *Implementer's decisions, the author's to overrule:* `juce::JSON` stays the tree's JSON (go.dot's
  own parser exists for shortest-round-trip doubles and >int64 literals, which a device tree has
  no need of; `tests/LocaleTests.cpp` proves the number text locale-independent). A notification
  that *arrives* is handled whether or not `EXTENSIONS` lists it, because WFS-DIY sends
  `PATH_CHANGED` without listing it; nothing is ever *sent* that is unlisted. `treeHash()` runs over
  every leaf with a `TYPE`, whatever its `ACCESS`, so two clients a device treats differently (C6)
  agree; the algorithm is C5's *(proposed)* one and changes if the reviewers change it. Every
  callback but `onState` fires on the Device's own thread; `onState` fires on the thread that
  changed the state, the caller's for the blocking calls. No `juce::Timer`, no `MessageManager`.
- *Lazy loading, as OSCQuery allows it:* the proposal has no depth-limit field and `GET /` is the
  whole tree, so a tree above 5,000 nodes (`Device::Options::largeTreeThreshold`) is fetched once
  and never whole again, every later change arriving by subtree GET of the path a notification
  names; containers a device sends without `CONTENTS` are `loaded = false` until fetched. The
  PRD's "device advertises a depth limit" has no field to read.
- *Found on the way:* `juce::var` answers `isObject()` for an array too, and the `DynamicObject`
  behind an array is null (a crash on `EXTENSIONS` given as a list, caught by the suite). WFS-DIY's
  `OscQueryClient` decodes the first argument of `,fff` from the wrong offset (type-tag block
  assumed four bytes); not copied. juce_simpleweb's client closes an idle connection after 1000 s
  on its own; the reconnect path covers it.
- *The mock device gained:* `hostInfoOnly`, `notifyUnlisted`, `oscTransport` (a TCP listener behind
  `OSC_PORT`), `oscIp`, a reader behind `OSC_PORT` (datagrams, or a TCP stream's bytes), binary
  websocket frames kept, `setValueBundle`, `closeAllConnections`; the runner the matching flags and
  `--generate N` from `tests/support/TreeGen.h`. Still missing: a distinct `WS_PORT`, per-client
  `ACCESS` (Phase 5).
- *Done-when:* `oqa_oscquery_tests`, **90 cases, 31,830 assertions under C and fr-FR on Windows**,
  as `oscquery.C` and `oscquery.fr_FR` in CTest; **CI green on Linux, macOS and Windows** (run
  42509175235) after two portability fixes GCC and clang asked for (a nested struct's default
  member initialisers in a default argument; floating-point `std::to_chars` absent below macOS
  13.3, this plugin's floor being 11.0). The example prints **WFS-DIY's tree (1,393 nodes, 1,347 leaves)**, go.dot's (820
  nodes) and the mock's, and subscribed to a WFS-DIY input position with no address typed in code
  it printed the current value and then three changes sent over UDP as they landed (WFS-DIY's
  OSCQuery server is off by default, `networkOscQueryEnabled`; the author switched it on in its
  Network tab). L5 on loopback: 10,000 leaves fetched and parsed in 1.4 s, Debug.
  DNS-SD browse **finds an instance announced from the Mac mini** (Bonjour's `dns-sd -R`) across
  the home network, probes it and lists it down with the reason; it found nothing announced on
  this same Windows machine (four processes already on 5353), a same-host matter kept in the
  bench report.

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
  (`kParamTitlesChanged`, `(unbound)` names; on AU `(unbound)` names too, never hidden, decided
  2026-11-08, and `clump`), AU measured in Logic
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

**Planned, 2026-11-08.** Broken into six subphases, each one commit on `main` with its tests, as
Phase 2 was: **3a** slot model and scaling (`src/core`, target `oqa::core`, tests `core.C` and
`core.fr_FR`); **3b** state chunk, `version` 1, fixtures in `tests/state/`; **3c** registry and
send path (the C ABI `RegistryApi.h`, the shared core library, the per-device realtime network
thread, the ring, the block bundler, the rate limiter, timetags, a realtime-safe UDP sender and a
fixed-buffer encoder added to the library, the lipogram test, the latency harness); **3d** the JUCE
shim's real parameters, state and registration; **3e** the editor's first cut; **3f** the bench in
Reaper on Windows and the Mac mini, the records, the amendments.

- *Decided by the author the same day:* **32 slots alone** through Phase 3; **`$instance` in
  Phase 6**; the **timetag path is built and verified on the mock only**, the per-device setting
  default off; the audio thread wakes the network thread with **one non-blocking notify per block
  per device**, the one named exception to the lipogram's "no syscalls", recorded as such.
- *Facts found while planning, from the sibling repositories:* **WFS-DIY discards bundle
  timetags** (`spatcore/control/osc/OSCParser.h` reads the eight bytes and drops them; bundles
  apply on arrival from a 16 ms drain on its message thread), so the PRD's "WFS-DIY does" is
  amended: no device on the bench honours them yet. WFS-DIY's **Bridge is a registry only**: no
  lock-free hand-off (mutexes, allocation and UDP sends on the thread that changed the parameter),
  so its ABI pattern is copied and its threading is not. **go.dot's lipogram counts allocations**
  through a replaced global `operator new`, test-only here (a replaced allocator inside a plugin
  could leak into the host); locks and syscalls fall to a Clang RTSan job when the runner has
  Clang 20. **No realtime-safe send exists** in either repository: `UdpEndpoint::send` builds a
  `juce::String` and may resolve the host, `osc::encode` returns a vector; both are written new
  in 3c. WFS-DIY's OSC router **accepts the tree's short form** `/wfs/input/<n>/<name>` with the
  channel in the address (`OSCMessageRouter.cpp`), so `FULL_PATH` is sendable as is; confirmed on
  the bench first thing in 3f.
- *Implementer's decisions, the author's to overrule:* only the registry crosses the shared
  library's ABI (slots, scaling, chunk and editor are C++ in the plugin binary; structured data
  crosses as JSON text, the hot path as scalars); every plugin binary **also links the core
  statically and falls back to it** when the shared library is missing or its ABI version differs,
  saying so in the status bar, where WFS-DIY's plugins go silent; the shared library is built with
  hidden visibility and exports the C ABI only, so two copies of JUCE in one process do not meet;
  one value per slot per block, because JUCE's `AudioProcessor` delivers no sample-accurate
  parameter events (the CLAP shim will, Phase 5b); the device key in Phase 3 is `NAME` plus last
  host:port, re-identification being Phase 5's.

**Record, 2026-11-08 (built; the DAW rows of the done-when open).** Built in eight commits on
`main`, 3a to 3e: `src/core` (`oqa::core`), the shared core library `OSCQuery-Automation-Core` and
its loader, the shim's real parameters, the editor's first cut. Report of the bench so far:
[`docs/bench/2026-11-08-phase3-send.md`](bench/2026-11-08-phase3-send.md).

- *Done so far, measured on this machine against the mock (Debug, loopback):* two instances, 25
  blocks, **25 bundles of two messages**; an `fff` one message with the tree's values for the
  arguments not automated; 32 messages at an MTU of 200 in four bundles; CRITICAL on a UDP device
  over the websocket; a TCP device's SLIP-framed bundles; the rate limiter holding to 20 Hz; the
  NTP tag at now plus the offset when the setting is on; an unreachable device sending nothing
  until pointed right, every instance following; **500 blocks allocating nothing** on the audio
  path, direct send on or off; **post to the wire median 96 µs, p99 227 µs**. The core's suite is
  **69 cases** as `core.C` and `core.fr_FR`; the library's 98. pluginval 1.0.4 passes at
  strictness 5 on the VST3 with the editor. **Live against WFS-DIY the same day:** the whole path,
  Instance to registry to bundle to UDP 8000, drove three per-input floats found in its tree and
  read them back exactly (20 bundles, 60 messages, none dropped).
- *Implementer's decisions, the author's to overrule:* the rate limit is per bound path, not per
  argument, since a multi-argument node is one message anyway; `critical` and `unit` travel on the
  slot and in the chunk, because bindings arrive before the device's tree does on a reload; a
  device entry lives as long as the registry (its sockets and thread are not worth closing on
  every `releaseResources`); a session device whose address a second chunk disagrees on keeps the
  first address until pointed (Phase 5 resolves identity); the direct send sends a plain message
  with no bundle and no time tag; the arguments not automated start from the tree's `VALUE` at
  bind time and are not yet refreshed when the tree reloads (Phase 4's LISTEN will keep them
  current); the AU gets no parameter clump (fixed at construction, it could not follow a rebind;
  REAPER would put the unit's name in lane titles). NTP seconds wrap at the end of era 0 (2036) as
  NTP's do.
- *On the Mac mini the same day* (`scripts/farm-check.sh`, Debug): every CTest entry passes, the
  Release VST3 and AU build, pluginval passes the VST3; 25 blocks from two instances as 25 bundles;
  post to the wire median 209 µs, p99 337 µs. It took two fixes to get there: a device thread that
  spun to its gather deadline overran macOS's time-constraint budget and was demoted until its wakes
  came late (the gather now waits on the semaphore, the budget declared is a fraction of the
  deadline); and libc++'s `counting_semaphore::try_acquire_for` polls with a growing back-off instead
  of waking on release, so on Apple the wake is a dispatch semaphore (`registry/Wake.h`). The loader
  holds the system's own handle (LoadLibrary, dlopen), never closed, with no JUCE object for the leak
  detector to count at exit; `Registry::shutdown()` (the API table's first additive entry) lets a
  test process end with no thread of ours alive.
- *Found on the way:* a CMake target that links `oqa::core` gets the client library's archive
  appended after `oqa_juce` on GNU ld, which has already passed the SHA-256 it needs: name
  `oqa::oscquery` directly; for the plugin, whose JUCE modules live in JUCE's shared-code archive
  that the format targets put first, the core's and the library's object files go into that archive. `Node::argument()` returns a view by value, so a reference into its
  `range` dangles. MSVC's `std::counting_semaphore::try_acquire_for` is `[[nodiscard]]`. No range
  in the generic fixture excludes zero, so the log curve is tested on a user-entered range.
- *Still to fill, the author at the DAW:* the done-when's REAPER rows on Windows (the Release VST3
  and the core DLL are installed in `%LOCALAPPDATA%\Programs\Common\VST3\`; the steps are in the
  bench report) and on the Mac mini, and the 32-track capture.

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
  notification, `clump` (fixed per slot by JUCE's parameter groups, Phase 1), gesture events for
  record. Logic's partial rename
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
