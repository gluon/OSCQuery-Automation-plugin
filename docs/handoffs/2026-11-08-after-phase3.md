# Handoff, 2026-11-08: Phase 3 built, its DAW rows open, Phase 4 next

For the next Claude Code session on this repository, and for the author. Read this, then
`CLAUDE.md`, then the devplan's *How to read this*, its Phase 3 record, and Phase 4.

## Where things stand

- **Phase 3 is built** (2026-11-08, one session, commits 3a to 3e plus records on `main`):
  `src/core` (`oqa::core`: slots, scaling, the state chunk, the lipogram's counters, the ring, time
  tags, the rate limiter, the registry behind its C ABI, the plugin-side `Instance`, the loader),
  the shared core library `OSCQuery-Automation-Core` (`src/core/core-shared`), the JUCE shim's real
  parameters, state and lifecycle, and the editor's first cut (`src/editor`). Two units joined the
  library: `osc/FixedEncoder` and `net/RawUdpSender`. The core's suite is 69 cases as `core.C` and
  `core.fr_FR`; the library's 98. Bench report: `docs/bench/2026-11-08-phase3-send.md`.
- **Measured** (Debug, loopback): two instances, 25 blocks, 25 bundles of two messages; 500 blocks
  allocating nothing on the audio path; post to the wire median 96 µs, p99 227 µs. **Live against
  WFS-DIY:** the whole path drove three per-input floats found in its tree and read them back
  exactly, 20 bundles, 60 messages, none dropped.
- **Open, the author at the DAW:** the done-when's REAPER rows (bind x/y/z of one input, draw
  curves, the source moves; 32 tracks one bundle per block by capture; the session reloads) on
  Windows and on the Mac mini. The Release VST3 and the core DLL are installed in
  `%LOCALAPPDATA%\Programs\Common\VST3\`; the steps are in the bench report. Fill the rows, then
  close the record in the devplan.
- **CI** was green through 3c-2a (run 42514922031); the 3c-3, 3d and 3e pushes each cancelled the
  previous run, and the one for 3e (42517557126) was in progress at hand-off. The Mac mini farm
  check (`scripts/farm-check.sh` on a bundle clone at `~/oqa-phase3`) was started at hand-off.

## Decided this session (the author, 2026-11-08)

1. 32 slots alone through Phase 3; the other four IDs in Phase 7.
2. `$instance` in Phase 6.
3. The timetag path built and verified on the mock only (WFS-DIY discards timetags; the PRD was
   amended).
4. One non-blocking wake per block per device from the audio thread: the lipogram's one named
   exception.

The devplan's Phase 3 record holds the implementer's decisions (rate limit per bound path;
`critical` and `unit` in the chunk; device entries live as long as the registry; the direct send
is a plain message; no AU clump), the author's to overrule.

## What Phase 4 builds on

- `Registry` already owns one `oscquery::Device` per device with its websocket open when offered,
  and `Group::initial` holds the tree's values for arguments not automated: LISTEN on bound paths
  and the incoming values go through the same entry. `Slot::hostValue()` maps a received value to
  0–1. The shim's `SlotParameter` is where a received value becomes a host parameter change
  (`setValueNotifyingHost` with begin/end gestures).
- WFS-DIY's own OSC feedback goes to the client's UDP 9000 (the author): the "devices without
  LISTEN" fallback has a real subject on this machine, though WFS-DIY also offers LISTEN.
- The loop guard, the gesture timeout and `Direction::both` have their fields in place and no
  code yet.

## Lessons that cost time here

- **Archive order on GNU ld:** a target that links `oqa::core` gets `oqa::oscquery` appended after
  `oqa_juce`, which was already scanned; name `oqa::oscquery` directly (the CI failure of 3b).
- **`Node::argument()` returns a view by value**; `const auto& vals = node.argument (0).range->vals`
  dangles. Copy the view first.
- **Bindings arrive before the device's tree** on a reload, so anything the send path needs from the
  tree (CRITICAL, units) must travel in the chunk.
- **The Bash tool mangles backslashes in heredocs** (known); a Windows path in a Python heredoc
  reached the file right only because Python keeps an invalid escape as written.
- **`%LOCALAPPDATA%\Programs\Common\VST3`** is where REAPER picks up a per-user VST3 on this
  machine without admin rights; the DLL goes beside the bundle, where the loader walks.

## Working agreements

Unchanged: commit and push straight to `main` with the attribution trailer; before a push,
`python scripts/check-claude-md.py`, `check-pins.py`, `check-headers.py`; the PRD wins over
`CLAUDE.md`.
