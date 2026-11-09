# Report, Phase 5b: CLAP, the reference format, from our own shim

**Begun 2026-11-09.** Not a spike: the shim is the product (`src/shims/clap/`), and this report is
the Phase 1 format report's missing column, the one the devplan asked for ("the rename, hide, clear
and module rows added to the Phase 1 report as `docs/spikes/phase5b-clap.md`"). What the test host
and clap-validator measured is filled; the DAW rows are the author's, at Bitwig and REAPER on this
machine and the Mac mini.

## Criterion

The PRD's format table, CLAP column (*Host parameter model per format*): stable `clap_id` per slot;
rename after bind by `rescan (CLAP_PARAM_RESCAN_INFO)`, immediate, while active; hierarchical
grouping by `module` from the OSC path; unbound slots hidden by `CLAP_PARAM_IS_HIDDEN`; stale
automation dropped by `clap_host_params->clear()`. The devplan's Phase 5b done-when: the Phase 3
and Phase 4 done-whens pass in Bitwig and REAPER over CLAP; an unbound slot is absent from the
host's list; a stale lane is cleared from the host on rebind; a chunk saved from the VST3 opens in
the CLAP with identical bindings and back; clap-validator is clean on three platforms.

## What the test host measured (Windows, Debug, loopback, 2026-11-09)

`tests/clap/`, a `clap_host` of our own that loads the built module as a DAW does and records every
call the plugin makes (`TestHost.h`). Repeatable with `ctest --preset vs-debug` (`clap.C`,
`clap.fr_FR`).

| Check | How | Result |
| --- | --- | --- |
| The module loads, the factory lists the five variants | `clap_entry`, `get_plugin_count`, each descriptor | **five, `io.github.pob31.oqa.Q008` … `Q128`, names "OSCQuery Automation 8" … "128", vendor Pix et Bel** |
| Lifecycle, audio through | each variant: init, activate, process a stereo block, deactivate, destroy | **the output is the input; `CLAP_PROCESS_CONTINUE`; one 50 ms timer registered** |
| State over CLAP's streams | a chunk read 7 bytes at a time, written back 5 at a time | **byte-identical to the chunk's own JSON, under C and fr_FR; `values` restored and written back; garbage refused** |
| Unbound slots hidden and named `(unbound)` | `get_info` on a fresh plugin | **every slot `AUTOMATABLE` and `HIDDEN`, never `STEPPED`, 0–1** |
| Rename, module, text on bind | a chunk binding one slot loaded | **`rescan (INFO | VALUES)` at once; the slot visible, named by its path, `module` = device NAME + the path's container; text in device units with the unit, read back through the same scaling** |
| Events reach the device | `PARAM_VALUE` at offset 17 in `process`; in `flush` while inactive | **the mock receives the move both ways; `get_value` agrees; an event in another space is ignored** |
| `clear()` by the rule | a slot `missing` then bound elsewhere; `renamed` then bound where it went; `missing` then unbound; live then rebound | **cleared (`CLEAR_AUTOMATIONS`, once) in the first case only** |
| Record in `process` | a value streamed by the mock for a `both` slot | **`GESTURE_BEGIN` then `PARAM_VALUE` (live, 0–1 by the slot) in the next block; `GESTURE_END` once idle past 200 ms; nothing sent back to the device** |
| Record while the host sleeps an active plugin | `stop_processing`, a value streamed | **the wake asks for a flush; the flush carries BEGIN and VALUE; the timer asks again while the gesture is open and stops once a flush has closed it** |
| A gesture open at deactivate | `deactivate` mid-gesture | **a flush asked for; the next flush carries the END** |
| A burst | 8 slots × 20 values from the mock | **160 values, whole, each slot's in order, the last at 1.0** |
| The gui | `is_api_supported`, `create`, `get_size`, `adjust_size`, `set_size`, `destroy`; on Windows `set_parent` to a window of the test's | **win32 embedded only; 1100×620; clamps to 720×420 and 2400×1600; the editor a child of the parent window at the size agreed, in physical pixels (the author's monitor at 175 %: 800×480 asked, 1260×735 agreed)** |

clap-validator 0.4.1 on the Release module (Windows): **192 tests run, 138 passed, 0 failed, 0
warnings, 54 skipped** (the skipped ones are for extensions the plugin does not implement: note
ports, state-context, and the like).

## What JUCE and CLAP say, read from their sources

| Question | Answer | Where |
| --- | --- | --- |
| Can the hidden flag flip while active? | Yes: `RESCAN_INFO` covers name, module, `is_periodic` and `is_hidden`, immediate | `clap/ext/params.h`, the rescan flags |
| Can `STEPPED` flip while active? | No: a critical flag, `RESCAN_ALL`, only while inactive; so no slot is ever `STEPPED` | same |
| Who pumps JUCE's messages in a non-JUCE host? | Windows: a hidden window fed by `PostMessage`, the host's loop dispatches. macOS: a run-loop source on the main loop. Linux: nobody; JUCE registers file descriptors and its VST3 wrapper hands them to the host's run loop | `juce_events/native/juce_Messaging_windows.cpp`, `juce_MessageQueue_mac.h`, `juce_audio_plugin_client_VST3.cpp:156-300` |
| Which thread is JUCE's message thread? | The one that creates the `MessageManager`: so JUCE is initialised in the plugin's `init()`, `[main-thread]` | `juce_MessageManager.cpp` |
| Sizes across the gui API | physical pixels on win32 and x11, logical on cocoa | `clap/ext/gui.h` |

## The matrix, at the DAW

Rows the author fills. Hosts: Bitwig Studio 6.1.4 and REAPER on this Windows machine and on the
Mac mini. The Release module and `OSCQuery-Automation-Core.dll` are installed as the bench report
says ([`../bench/2026-11-09-phase5b-clap.md`](../bench/2026-11-09-phase5b-clap.md)).

| Check | Host | Result |
| --- | --- | --- |
| Rename while active: a slot bound during playback takes its name on the lane | Bitwig 6.1.4, Windows | _to fill_ |
| | REAPER, Windows | _to fill_ |
| | Bitwig 6.1.4, Mac mini | _to fill_ |
| | REAPER, Mac mini | _to fill_ |
| Hide: an unbound slot is absent from the host's parameter list; bound, it appears; unbound again, its lane and points | Bitwig 6.1.4, Windows | _to fill_ |
| | REAPER, Windows | _to fill_ |
| Module: lanes grouped by device and node | Bitwig 6.1.4 (shows modules) | _to fill_ |
| | REAPER | _to fill_ |
| Clear: a slot whose path the device removed, rebound to another path, loses its curve; a rename rebind keeps it | Bitwig 6.1.4 | _to fill_ |
| | REAPER | _to fill_ |
| Record: a move in WFS-DIY (or go.dot) writes a lane in Touch; it plays back; the loop guard stays at zero | Bitwig 6.1.4, Windows and Mac mini | _to fill_ (Phase 4's gate) |
| | REAPER | _to fill_ |
| Phase 3's done-when over CLAP: WFS-DIY found, x/y/z bound without a typed address, curves move the source; 32 tracks one bundle per block | Bitwig, REAPER | _to fill_ |
| A chunk saved from the VST3 opens in the CLAP identically, and back | REAPER (both formats installed) | _to fill_ |
| VST3 and CLAP in one host share one registry | REAPER, both status bars naming the same core path; one websocket client at the device | _to fill_ |
| The editor at 150 % and 175 % display scaling | Bitwig, Windows | _to fill_ |
| Keyboard focus into the tree's search field | Bitwig, REAPER | _to fill_ |
| clap-validator | the Mac mini (`scripts/farm-check.sh`), the NUC | _to fill_ |

## Found on the way

- **Two copies of JUCE in one process on macOS meet through weak symbols.** The test host (its own
  JUCE), the module (its own) and the shared core library (its own) all compile JUCE with hidden
  visibility, and the macOS runner still coalesced `juce::var`'s constexpr type singletons across
  the executable and the module: a `var` made in the module was torn down through the executable's
  JUCE, whose `String` code compared the module's empty-string singleton with its own and freed a
  static. The linker is now told as well as the compiler: an export list on Apple (`_clap_entry`,
  `_oqa_core_api`), a version script with `-Bsymbolic` on GNU ld. The macOS job prints each image's
  external symbols before the tests. The test runner prints a backtrace on a fatal signal on POSIX
  now, as it did on Windows.
- **clap-helpers' checks are a second validator.** With `Terminate` and `Maximal` in Debug, the
  harness learnt from the plugin that `flush` is the audio thread's while the plugin is active, and
  that `set_size` may only be called with a size `adjust_size` leaves alone.
- **Deactivated, an instance has left the registry**, as the JUCE shim's does in `releaseResources`,
  so nothing streams to it; the flush path's case is a host that stopped processing an active plugin.
- **The test host cannot count the module's allocations**: the realtime counters are per module, and
  the executable's counting `operator new` sees only its own code (on macOS it is interposed into
  the module too, which is how the two-JUCE failure surfaced). The audio path's lipogram stays the
  core's tests' word.
- **The `clear()` rule has a 250 ms window**: the shim records each slot's standing from the status
  on its slow tick; a rebind inside that window after a path vanished is not cleared. The harness
  lets a tick pass after each state wait.
