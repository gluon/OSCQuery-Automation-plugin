# Spike, Phase 1: the formats shipped first (VST3 and AU)

**Draft, 2026-11-04. In progress:** REAPER on Windows is measured; the Mac rows and the hosts
driven by hand ([`phase1-checklist.md`](phase1-checklist.md)) are not yet. Rows still empty are
marked *pending* or *no host*.

## Verdict so far

- **VST3 rename, REAPER 7.84 on Windows: half.** The parameter list follows every rename at
  once, during playback too. An existing automation lane does **not** follow: it keeps the title
  its parameter had when the lane was made, until the project is reopened or the plugin is set
  offline and online again. A lane made after a bind takes the current name.
- **Stale lanes are real, and quiet.** Automation stays on the slot's stable ID through unbind
  and rebind, so a curve drawn for one target goes on driving the next one under the old title.
  The PRD's "Drop stale automation: not available" for VST3 is confirmed; the plugin's own editor
  is the only place the truth is shown.
- **AU hiding: the PRD names a flag that does not exist.** `kAudioUnitParameterFlag_IsHidden` is
  not in the macOS 15 SDK's `AudioUnitParameterOptions`; AUv2 has no hidden flag. The spike
  measures the nearest means instead (*pending* on the Mac mini, see below).
- **pluginval at strictness 5:** the spike's VST3 passes on Windows and on macOS. **auval:**
  *pending* (the component is not yet registered on the Mac mini).
- **Five plugin IDs:** already written into the PRD (*Host parameter model per format*,
  Decisions, 2026-11-04), as this phase's done-when asks.

## Criterion

The devplan, Phase 1:

> | Check | Where |
> | --- | --- |
> | Rename a slot while active; the automation lane name follows | Reaper, Samplitude, Live, Bitwig, Nuendo (VST3); Logic (AU) |
> | Hide an unbound slot; it leaves the host's parameter list | Logic (AU) |
> | A stale lane after rebind: what the host shows, what the user has to do | all six |
> | pluginval at strictness 5 on the VST3, `auval` on the AU | CI, and the Mac mini |

The bench as it stands: no Logic anywhere (its AU rows move to REAPER, Live and GarageBand on
the Mac mini), no Nuendo, Bitwig 6.1.4 on both machines since 2026-11-04.

## What was built

`spikes/phase1_formats/`, a CMake project of its own:

- **"OQA Spike 8"** (Pix et Bel, `PxBl`, plugin code `Qsp8`): eight normalised float slots with
  stable IDs `slot1`…`slot8`, renamed at runtime through
  `updateHostDisplay (ChangeDetails().withParameterInfoChanged (true))`, named `(unbound)` while
  unbound, in two parameter groups "Device A" (slots 1–4) and "Device B" (5–8). Bound from its
  editor or from a command file a script writes.
- **`juce-au-ishidden.patch`**, applied to a copy of the pinned JUCE in the spike's build folder:
  `AudioProcessorParameter::isHidden()`, read by the AU wrapper, which leaves a hidden parameter
  out of `kAudioUnitProperty_ParameterList` and marks it `ExpertMode`.
- **`reaper/phase1-bench.lua`**: binds, renames during playback, binds all, unbinds, rebinds to
  another target, makes a lane after a bind, re-applies a lane's state chunk, sets the plugin
  offline and online, saves and reopens; logs what REAPER reports at each step. REAPER is started
  with `-cfgfile` on a copy of the user's `reaper.ini`, so the real one is never written.

## What JUCE 9.0.5 does, read from its source

| Format | On `parameterInfoChanged` | Hidden | Groups |
| --- | --- | --- | --- |
| VST3 | `restartComponent (kParamTitlesChanged)` when a title changed (`juce_audio_plugin_client_VST3.cpp:1507`) | nothing in VST3 | VST3 units |
| AU | `PropertyChanged` on `ParameterList` and `ParameterInfo` (`juce_audio_plugin_client_AU_1.mm:2046`) | never set; unnamed parameters get `NonRealTime` | clump ID = index of the group, fixed at construction (`:1091`) |

So an AU clump per *device* (devplan Phase 7) cannot follow a rebind through JUCE's parameter
groups: the clump is a property of the slot, not of what it is bound to.

## The matrix

| Check | Host | Format | Result |
| --- | --- | --- | --- |
| Rename while active | REAPER 7.84, Windows | VST3 | Parameter list: follows at once, also while playing. Existing lane: keeps its old title until reopen or FX offline/online. New lane: current name. |
| | Samplitude, Windows | VST3 | *pending, checklist* |
| | Live 12, Windows | VST3 | *pending, checklist* |
| | Bitwig 6.1.4, Windows | VST3 | *pending, checklist* |
| | Nuendo | VST3 | *no host* |
| | REAPER 7.80, Mac mini | VST3, AU | *pending, script* |
| | Live 12.4.3, Mac mini | AU, VST3 | *pending, checklist* |
| | GarageBand 10.4.14, Mac mini | AU | *pending, checklist* |
| | Bitwig 6.1.4, Mac mini | VST3 | *pending, checklist* |
| | Logic | AU | *no host* |
| Hide an unbound slot | any | VST3 | Not available in the format; slots read `(unbound)` (REAPER lists eight of them without complaint). |
| | REAPER, Live, GarageBand, Mac mini | AU | *pending*: left out of the parameter list and marked `ExpertMode` |
| Stale lane after rebind | REAPER 7.84, Windows | VST3 | Points kept (3 of 3) on the slot's ID through unbind and rebind; the lane drives the new target under the old title. Fixed by reopening the project or setting the plugin offline and online (all titles refresh, state and points intact). Re-applying the lane's state chunk does not refresh it. |
| | the other hosts | | *pending* |
| pluginval, strictness 5 | Windows | VST3 | SUCCESS |
| | Mac mini (arm64) | VST3 | SUCCESS |
| auval | Mac mini | AU | *pending*: not yet registered |

REAPER also puts a VST3 unit's name in each lane title (`Device A /bank 2 / OQA Spike 8 /
Device A`), so in REAPER the device grouping reaches the lane even in VST3, against the PRD's
"Flat; group in name prefix".

## For the author: what this changes in the PRD

1. **AU hidden slots.** The Binding model rule ("hidden where the format allows (CLAP
   `is_hidden`, AU `kAudioUnitParameterFlag_IsHidden`)") and the format table's AU column name
   a flag AUv2 does not have. Once the Mac rows are in, the proposal is either "AU: left out of
   the parameter list" (if hosts hide it and keep its automation) or "AU: named `(unbound)`, as
   VST3". Since the rule is one of the two sections `CLAUDE.md` copies verbatim, the change is the
   author's, in the PRD, then copied.
2. **"Reaper honours it"** holds for parameter names, not for lanes already drawn.
3. **Stale lanes drive the wrong target silently** in VST3; a REAPER user's one-step fix is
   offline/online. Whether the plugin can refresh REAPER's titles itself (REAPER exposes its API
   to VST3 plugins) is a question for Phase 3, not measured here.

## clap-juce-extensions on JUCE 9.0.5 (information only)

Cloned at `7adee3a1` (2026-10-06). Its wrapper already has a branch for JUCE 8.0.11's
`juce_audio_processors_headless` split (`#if JUCE_VERSION >= 0x08000B`). The build attempt is
*pending* the author's go-ahead: running code fetched from outside the repository was held back on
2026-11-04.

## Rows that stay empty

Nuendo (no licence) and Logic (no machine), as the devplan allows.
