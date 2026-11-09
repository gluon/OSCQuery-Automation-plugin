# Handoff, 2026-11-09: Phase 5 built, its DAW rows and Phases 3 and 4's open, Phase 5b next

For the next Claude Code session on this repository, and for the author. Read this, then
`CLAUDE.md`, then the devplan's *How to read this*, its Phase 5 record, and Phase 5b.

## Where things stand

- **Phase 5 is built** (2026-11-09, one session, the plan then commits 5a to 5d on `main`): every
  bound slot checked against its device's tree when a plan is built (stale slots in no group, their
  lanes kept; unauthorised ones still receiving; `PATH_REMOVED` and `PATH_RENAMED` wired, renames
  remembered for the one-click rebind; the `tree_hash` notice); a device driven only once confirmed
  by NAME and identity leaf, else searched for over DNS-SD by the resolver, which owns the
  process-wide Finder, tells candidates apart by identity leaf then tree hash, adopts one or leaves
  them for the user; pointing through the Instance, every instance following and saving the address;
  export and import as the chunk itself, a ToscA/OSCar converter in `tools/`; the editor's Point,
  Identity, Rebind, Export and Import. Bench report: `docs/bench/2026-11-09-phase5-reidentify.md`.
- **Measured** (Debug, loopback): 110 core cases under `C` and `fr_FR`; every done-when row of Phase
  5 against the mock, two mocks and a loopback DNS-SD responder; pluginval strictness 5 green with
  the editor.
- **Open, the author at the DAW:** the Phase 5 rows moved to real devices (WFS-DIY here then on the
  Mac mini by hand, go.dot re-found over DNS-SD, two mocks sharing a NAME, stale live from Chataigne
  or ossia score, export from REAPER into Bitwig), with the Phase 3 and Phase 4 rows in the same
  sitting. The Release VST3 and the core DLL from `0d4f939` are installed in
  `%LOCALAPPDATA%\Programs\Common\VST3\`; the steps are in the bench report. Fill the rows, then
  close the three records in the devplan.
- **CI green on Linux, macOS and Windows** on `e4be98e` (run 42608007320), and on every
  subphase's push before it. The farm (Mac mini, NUC) has not run Phase 5 yet:
  `scripts/farm-check.sh` on a bundle clone when the author is home.
  Watch the libc++ build: the resolver thread joins while a throwaway Device may be mid-HTTP
  (3 s timeout), and `std::set` erase-while-iterating in the library's worker.

## Decided this session (the author, 2026-11-09)

1. The Finder lives in the registry, one browser per process; editors read `found` from the status.
2. A device pointed at by hand is used whatever NAME answers, with a warning; the chunk's name is kept.
3. The export file is the state chunk itself, no envelope.
4. The converter's fixtures are written from the ToscA manual and the OSCar forum examples, swapped
   for real files when one turns up.

The devplan's Phase 5 record holds the implementer's decisions (pending slots planned as before;
`T`/`F` one type; a device gone unreachable goes searching; candidates kept after adoption; the test
rig naming the mock after the session's device; a device joining the session only once probed; the
editor's warnings from the registry's status), the author's to overrule.

## What Phase 5b (the CLAP shim) builds on

- The shim interface the CLAP needs is what `core::Instance` offers the JUCE shim: `connect` and
  `disconnect`, `processBlock (values, count)`, `setReceiveWake` and `pollReceived` for record,
  `saveState` and `loadState` for the chunk over `clap_istream`/`clap_ostream`, `revision()` for the
  rename rescan. The editor (`editor::EditorRoot`) takes a `Host` struct, no shim type.
- Stale slots are a per-instance list in the status (`slots`); CLAP's `clap_host_params->clear()` on
  a stale lane reads it from there.
- The ABI is version 1 with five additive entries (`shutdown`, the two receive entries, the two
  Finder entries); the CLAP binary links the same shared core library and falls back to the static
  core like the JUCE shim.

## Lessons that cost time here

- **A `juce::var` array pointer from a temporary dangles.** `status.getProperty ("x").getArray()`
  with `status` a temporary is a use-after-free on the next line; hold the `var`. Twice in a day.
- **A LISTEN made as the websocket opens was read twice** by the library (`onOpened` and the queued
  `listenNow`); the worker now records what is on the wire since the open. Timing moved by a
  phase can expose a race the previous phase never saw.
- **The mock's HTTP server is dual-stack**: a loopback client's address is `::ffff:127.0.0.1`.
- **Every registry test's device must be named as the mock's HOST_INFO NAME** now that the registry
  compares them; `Rig::device (name)` does it, a hand-made `DeviceRef` needs `mock.setName`.
- **The Bash tool mangles backslashes in heredocs** (known): files with escapes are written with
  the Write tool; the converter and the fixtures were.

## Working agreements

Unchanged: commit and push straight to `main` with the attribution trailer; before a push,
`python scripts/check-claude-md.py`, `check-pins.py`, `check-headers.py`; a push cancels the CI run
in progress, so wait for the previous run; the PRD wins over `CLAUDE.md`.
