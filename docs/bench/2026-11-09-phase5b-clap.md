# Bench, 2026-11-09: Phase 5b, the CLAP shim

What Phase 5b built was measured on the author's Windows machine (Windows 11, VS 2026, Debug unless
said otherwise) by a CLAP host of the tests' own against mock devices on loopback, and by
clap-validator 0.4.1 on the Release module. The DAW rows are the devplan's done-when moved to
Bitwig and REAPER and are filled by hand; the rest is the test suite's own word (`tests/clap/`),
repeatable with `ctest --preset vs-debug`.

## Against the mock devices, loopback

The full table is in the format report, [`../spikes/phase5b-clap.md`](../spikes/phase5b-clap.md):
the factory's five variants, the lifecycle, state over CLAP's streams byte for byte with the
parameters' values, hidden and `(unbound)` until bound, rename and module and device-unit text on
bind, events reaching the device from `process` and from `flush`, the `clear()` rule's four cases,
record as `GESTURE_BEGIN`, `PARAM_VALUE`, `GESTURE_END` in `process` and in the flush the wake asks
for, a burst of 160 values whole and in order, the gui created, sized, clamped, parented (Windows)
and destroyed. **13 cases** as `clap.C` and `clap.fr_FR`. clap-validator: **138 passed, 0 failed,
54 skipped**. The core's suite gained the loader's reuse case and the registry's restart after
`shutdown()`, **113 cases**; the chunk's `values` has its fixture (`tests/state/v1-values.json`).
The Release VST3 and CLAP, each with its copy of `OSCQuery-Automation-Core.dll`, are installed as
below (2026-11-09). pluginval 1.0.4
at strictness 5 still passes the Debug VST3, whose editor is now a plain component in a window of
the JUCE shim's.

## At the DAW

Install, on this machine (the Release build of the commit the record names):

```
copy "build\vs\src\shims\clap\oqa_clap_artefacts\Release\OSCQuery Automation.clap" "%LOCALAPPDATA%\Programs\Common\CLAP\"
copy "build\vs\src\shims\clap\oqa_clap_artefacts\Release\OSCQuery-Automation-Core.dll" "%LOCALAPPDATA%\Programs\Common\CLAP\"
```

beside the Release VST3 and its own copy of the DLL in `%LOCALAPPDATA%\Programs\Common\VST3\`
(both from the same build, so the two share the chunk's `values`). Whichever format a host loads
first loads its copy of the library; the other finds that copy already in the process (5b-5) and
the status bar of both says the same path. Bitwig scans `%LOCALAPPDATA%\Programs\Common\CLAP`
by default; REAPER needs it added under Preferences, Plug-ins, CLAP (then re-scan). On the Mac
mini: `OSCQuery Automation.clap` (a bundle) and `libOSCQuery-Automation-Core.dylib` into
`~/Library/Audio/Plug-Ins/CLAP/`, built there by `scripts/farm-check.sh`, which also runs
clap-validator.

| Check | How | Result |
| --- | --- | --- |
| Phase 3's done-when over CLAP | Bitwig or REAPER: insert "OSCQuery Automation 32" (CLAP), pick WFS-DIY from the found list or type `127.0.0.1:5005`, bind x/y/z of one input, draw curves | _to fill_: the source moves; 32 tracks produce one bundle per block (Wireshark on UDP 8000) |
| Phase 4's done-when over CLAP (the Bitwig record gate) | a `both` slot; the lane armed in Touch; the value moved in WFS-DIY or go.dot | _to fill_: the lane records the move, plays it back, the loop guard counter stays at zero |
| An unbound slot is absent from the host's list | the plugin fresh: Bitwig's parameter page, REAPER's parameter list | _to fill_: only bound slots listed; a slot bound during playback appears at once with its name; unbound again, it disappears, its lane and points kept |
| A stale lane is cleared on rebind | a slot bound to a Chataigne or ossia score parameter with a curve drawn; the parameter deleted; the slot rebound to another leaf | _to fill_: the curve is gone; the same with a *renamed* parameter and *Rebind* keeps the curve |
| Module grouping | several slots bound on one device, two containers | _to fill_: Bitwig groups the lanes by `Device/container`; REAPER's lane titles |
| A chunk from the VST3 opens in the CLAP, and back | REAPER: *Export…* from a VST3 instance, *Import…* into a CLAP instance; and the reverse | _to fill_: identical bindings, the lanes renamed, the values restored |
| VST3 and CLAP in one host share one registry | both formats on two tracks of one REAPER session, the same device | _to fill_: both status bars name the same shared core path; the mock (or WFS-DIY's client table) shows one websocket client |
| Display scaling | Bitwig on Windows at 150 % and 175 % | _to fill_: the editor crisp, sized right, resizable from the corner |
| Keyboard focus | typing into the tree's search field | _to fill_ |
| The farm | `scripts/farm-check.sh` on a bundle clone: the Mac mini and the NUC, clap-validator included | _to fill_ |

## How the first row is run

1. Install as above; start WFS-DIY with OSC Query on (HTTP 5005, OSC 8000).
2. Bitwig: Settings, Locations, Plug-in locations should include the CLAP folder; re-scan. Insert
   "OSCQuery Automation 32" from the CLAP list on a track.
3. The editor opens in Bitwig's plugin window: the found list has WFS-DIY if it answered the Finder,
   else type `127.0.0.1:5005` and Add; click the device; click slot 1 then the x leaf of an input,
   y and z follow on slots 2 and 3. The lanes appear in Bitwig's automation chooser under the
   device's module as soon as each is bound.
4. Draw curves, play: the source moves. Save, close, reopen: everything comes back, the values too.
