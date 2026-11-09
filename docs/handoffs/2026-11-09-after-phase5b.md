# Handoff, 2026-11-09: Phase 5b built, its DAW rows and Phases 3 to 5's open, Phase 6 next

For the next Claude Code session on this repository, and for the author. Read this, then
`CLAUDE.md`, then the devplan's *How to read this*, its Phase 5b record, and Phase 6.

## Where things stand

- **Phase 5b is built** (2026-11-09, one session, commits 5b-0 to 5b-5 on `main`): our own CLAP shim
  in `src/shims/clap/`, one module listing the five slot-count variants under
  `io.github.pob31.oqa.Q008` … `Q128` (fixed for good); params hidden while unbound, `module` from
  the path, `rescan (INFO)` on every change of bindings, `clear()` by the rule; record as gesture
  and value out-events drained on the audio thread; the editor (a plain `juce::Component` since
  5b-0, the JUCE shim wrapping it in `EditorWindow`) in the host's window on win32, cocoa and x11;
  the loader reusing a core library already in the process. Slot values live in the chunk now
  (`values`), written by both shims. Reports: `docs/spikes/phase5b-clap.md` (the format matrix) and
  `docs/bench/2026-11-09-phase5b-clap.md`.
- **Measured** (Debug, loopback): 13 cases in `tests/clap/` under `C` and `fr_FR` through a CLAP
  host of the tests' own; clap-validator 0.4.1 on the Release module, 138 passed, 0 failed; the
  core's suite with the loader's reuse case and the registry's restart; pluginval strictness 5 on
  the VST3.
- **Open, the author at the DAW:** the Phase 5b rows in Bitwig and REAPER on this machine and the Mac
  mini (the Phase 3 and 4 done-whens over CLAP, hide, rename, clear, module, VST3 ↔ CLAP chunk, one
  registry for both formats, display scaling, keyboard focus), with the Phase 3, 4 and 5 rows in the
  same sitting. Install steps in the bench report. The farm has not run Phase 5b: `farm-check.sh`
  builds the CLAP and runs clap-validator now.
- **CI:** see the devplan's Phase 5b record for the run that went green on three platforms. The macOS
  runner caught the two-JUCE-copies failure (below) that Windows and Linux never showed.

## Decided this session (the author, 2026-11-09)

1. Phase 5b before Phase 6, in the plan's order.
2. The CLAP ids are the bundle ids, `io.github.pob31.oqa.<code>`.
3. One value per slot per block; the sample-accurate send waits for a device that honours timetags.
4. `clear()` only when a `missing` or `retyped` slot is rebound to a different device or path.
5. Slot values join the chunk (`values`), both shims.

The devplan's Phase 5b record holds the implementer's decisions (connect in `init` and `activate`;
the audio-thread drain; never `STEPPED`; rescans not waiting for the timer; the vendor status
extension; `deinit` stopping only a fallback registry; the loader's already-loaded step), the
author's to overrule.

## What Phase 6 builds on

- The editor is a `juce::Component` with size-hint constants; both shims host it. Its `Host` struct
  is unchanged. The CLAP's editor bind calls `rescan` at once through `Host::bindingsChanged`.
- `io.github.pob31.oqa.status/1` (`src/shims/clap/StatusExtension.h`) answers the instance's status
  and the settings as JSON through the CLAP API: a headless window the Monitor's design may reuse or
  retire.
- The registry's settings are still in memory only (`receive_port`, `gesture_timeout_ms`, …); their
  persistence is Phase 6's, beside the recents file.
- The Monitor (`QMon`, zero slots) is not a target yet; the JUCE shim's `static_assert` allows 8 to
  128 and the CLAP factory lists five.

## Lessons that cost time here

- **Two JUCE copies in one process on macOS coalesce weak symbols** (constexpr inline singletons)
  even with hidden visibility everywhere; a `String` of one copy released by the other's code frees
  a static. The module and the core library now export one symbol each at the linker's level too.
  Windows and Linux never showed it.
- **A CLAP host may deinit the module after a scan and init it again**: the registry's resolver
  thread restarts with the first instance registered after `shutdown()`.
- **clap-helpers (Terminate, Maximal) is a validator of its own**: `flush` is the audio thread's while
  active; `set_size` only with what `adjust_size` returned.
- **A deactivated instance has left the registry**: no device streams to it. The flush path's case is
  an active plugin the host stopped processing.
- **The device name is a process-wide key in tests**: a second mock on another port under a reused
  name is never reached; one name per subcase.
- **The realtime counters are per module**: the test host cannot count the plugin's allocations.
- **The Bash tool mangles quotes and backslashes in heredocs** (known): edit scripts go through the
  Write tool into the scratchpad.

## Working agreements

Unchanged: commit and push straight to `main` with the attribution trailer; before a push,
`python scripts/check-claude-md.py`, `check-pins.py`, `check-headers.py`; a push cancels the CI run
in progress, so wait for the previous run; the PRD wins over `CLAUDE.md`.
