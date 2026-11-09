# Handoff, 2026-11-09: Phase 4 built, its DAW rows and Phase 3's open, Phase 5 next

For the next Claude Code session on this repository, and for the author. Read this, then
`CLAUDE.md`, then the devplan's *How to read this*, its Phase 3 and Phase 4 records, and Phase 5.

## Where things stand

- **Phase 4 is built** (2026-11-09, one session, commits 4a to 4d plus the plan and the records on
  `main`): the receive path in the registry (every bound path LISTENed, incoming values to the
  instance's slot through two additive ABI entries, the loop guard, the undriven arguments of an
  `fff` following the device, a plan rebuilt when the tree lands after the bindings); one UDP
  receiver per process for devices without LISTEN (routed by path, then sender address; the port
  and this host's addresses in the status); the shim's gesture path (an AsyncUpdater from the
  registry's wake, `core::GestureTracker`, a 50 ms timer closing idle gestures); the editor's words
  for it (rx/s, the guard count, the receive line with an editable port, the "waiting for UDP port"
  state). Bench report: `docs/bench/2026-11-09-phase4-record.md`.
- **Measured** (Debug, loopback): 32 slots in both directions following the mock at 100 Hz for 2 s,
  6,400 values, none missed, none echoed back; the guard dropping a 10 ms echo and passing a 150 ms
  one; the audio path still allocating nothing; pluginval strictness 5 green with the editor.
- **Open, the author at the DAW:** the Phase 3 REAPER rows (bind x/y/z, curves, the 32-track
  capture, reload; Windows and the Mac mini) and the Phase 4 rows (WFS-DIY, Chataigne, ossia score
  and go.dot as record sources; 32 receiving slots at the device's rate; the UDP fallback; Bitwig over
  VST3 as an extra). The Release VST3 and the core DLL from `8d548a8` are installed in
  `%LOCALAPPDATA%\Programs\Common\VST3\`; the steps are in the bench report. Fill the rows, then
  close both records in the devplan.
- **CI:** the Phase 4 commits were pushed together after 4d; see the run on `main` for the three
  platforms. The farm (Mac mini, NUC) has not run Phase 4 yet: `scripts/farm-check.sh` on a bundle
  clone when the author is home. Watch the libc++ build: the Device thread now calls into the
  registry (`received`), and the entry's device is stopped outside the device lock.

## Decided this session (the author, 2026-11-09)

1. Gesture idle timeout 200 ms, a setting (`gesture_timeout_ms`).
2. UDP receive port ephemeral by default, a setting (`receive_port`) the editor shows and edits;
   9000 is what the bench types for WFS-DIY.
3. The Phase 3 REAPER rows are filled in the same DAW sitting as Phase 4's.
4. Bitwig over VST3 is an extra bench row, not a gate; the CLAP gate stays Phase 5b.

The devplan's Phase 4 record holds the implementer's decisions (LISTEN on every bound path; the
device's current value delivered once on LISTEN, so a receiving slot starts where the device is;
UDP copies ignored for a device whose websocket streams; no echo back from a `both` slot; the
routing rule; the lock order), the author's to overrule.

## What Phase 5 builds on

- `InstanceRecord::lastChunk` and `Impl::applyBindings` rebuild an instance's plan from its chunk
  under the registry's lock, from any thread: the reopen sequence (verify each bound path's
  existence and `TYPE`, mark stale) has its hook. `Device::Callbacks::onPathRemoved` and
  `onPathRenamed` are not yet wired in `DeviceEntry::openDevice`; `onTreeChanged` is (it
  triggers the rebuild).
- The device key is still `NAME` plus identity value; `Registry::setDeviceAddress` points a device
  process-wide. Re-identification over DNS-SD is the library's `Finder` plus a compare of `NAME`
  and the identity leaf.
- The status JSON has grown (`listen`, `values_received`, `loop_guarded`, `udp_*`, `plans`,
  `receive_port`, `receive_addresses`); the editor reads it at 8 Hz.

## Lessons that cost time here

- **The library delivers each path's current value once on LISTEN** (an HTTP read; a device need
  not push on LISTEN). A test that counts streamed values must drain that first (`Rig::settleListen`).
- **Bindings arrive before the tree on a reload, and the message was short:** a partly bound `fff`
  planned without the tree's `TYPE` sent only the bound arguments. The plan is now rebuilt when the
  tree lands; the fix found the bug the Phase 3 record had noted as an open item.
- **A Device thread must never need a lock held by whoever joins it.** `DeviceEntry` used to stop
  its Device under the device lock; `received()` on that Device's thread wants the registry's lock,
  whose holder (`setGroups`) wants the device lock: a three-way deadlock in waiting. Stop outside.
- **The test runner now prints a backtrace on an access violation on Windows** (`tests/TestMain.cpp`),
  which turned a bare SIGSEGV into the line (a plan published twice, the second time null).
- **Windows lists multicast groups among a machine's addresses**; the receive line filters them as
  the library's Discovery does.
- **The Bash tool mangles backslashes in heredocs** (known): a Python patch with a `\n` in a C++
  string literal reached the file as a real newline. Write patch scripts with the Write tool.

## Working agreements

Unchanged: commit and push straight to `main` with the attribution trailer; before a push,
`python scripts/check-claude-md.py`, `check-pins.py`, `check-headers.py`; the PRD wins over
`CLAUDE.md`.
