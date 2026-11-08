# Spike, Phase 1: the formats shipped first (VST3 and AU)

**Draft, 2026-11-04, updated 2026-11-08. In progress:** REAPER is measured on Windows (VST3)
and on the Mac mini (VST3 and AU), with pluginval and auval; Samplitude, Bitwig and Live on
Windows by the author on 2026-11-08. The Mac mini's hosts driven by hand
([`phase1-checklist.md`](phase1-checklist.md): Live, GarageBand, Bitwig) are *pending*.

## Verdict so far

- **Rename, REAPER 7.84 (Windows) and 7.80 (macOS), VST3 and AU alike: half.** The parameter
  list follows every rename at once, during playback too. A lane already drawn keeps the title
  its parameter had when the lane was made; a VST3 lane catches up when the project is reopened
  or the plugin is set offline and online. A lane made after a bind takes the current name.
- **Every other VST3 host on the bench follows: Samplitude Suite 2026 (build 26215), Bitwig
  6.1.4 and Live 12.4.16 on Windows, and Live 12.4.16 on the Mac mini.** An existing lane takes each new name at once, during
  playback too, and nothing is lost through bind, unbind and rebind. REAPER is the only host
  measured so far whose lanes keep an old title. (A first run on Samplitude build 26015 showed
  names waiting for the stop; the author doubts his own handling there, and that build is
  uninstalled, so the observation is withdrawn.)
- **Stale lanes are real, and quiet.** Automation stays on the slot's stable ID through unbind
  and rebind, so a curve drawn for one target goes on driving the next one under the old title.
  The PRD's "Drop stale automation: not available" for VST3 is confirmed; the plugin's own editor
  is the only place the truth is shown.
- **AU hiding: the PRD names a flag that does not exist, and the substitute destroys
  automation in REAPER.** `kAudioUnitParameterFlag_IsHidden` is not in the macOS 15 SDK; AUv2
  has no hidden flag. Leaving an unbound slot out of `kAudioUnitProperty_ParameterList` does hide
  it in REAPER, but REAPER then deletes that slot's lane and its points, and they do not come back
  when the slot is bound again.
- **GarageBand 10.4.14, AU with hiding off: follows, and groups.** Renames show at once during
  playback, curves survive unbind and rebind, and the clumps appear as "Device A" and "Device B".
  With hiding on it never offers a slot at all.
- **An AU that never hides behaves like the VST3 in REAPER** (7.85, the Mac mini, spike 0.0.2
  with `no-hide`): all eight slots stay listed, unbound ones as `(unbound)`, and unbind and rebind
  keep every lane and point. The automation REAPER deleted was the hiding's doing, not AU's. One
  AU quirk remains: after offline/online or a reload every AU lane reads "(unbound) / OQA Spike 8",
  the names the slots had when REAPER created the plugin, before their state was restored.
- **REAPER keeps AU lanes on the parameter ID, not the index:** a lane stayed on its slot while
  other slots left and re-entered the list beneath it.
- **pluginval at strictness 5:** the spike's VST3 passes on Windows and on macOS. **auval:**
  passes on the Mac mini, run in the logged-in session, with every slot unbound (so with the
  eight slots out of the list).
- **On Apple silicon an AU must be signed as a bundle to register at all.** JUCE's CMake under
  Ninja leaves the bundle linker-signed only (`codesign --verify`: "code has no resources but
  signature indicates they must be present"); the registrar ignored it until
  `codesign --force --deep --sign -` sealed it. The unsigned AU that CI uploads has the same gap.
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
  out of `kAudioUnitProperty_ParameterList` and marks it `ExpertMode`. Generated byte-exact: the
  three files it touches are stored CRLF in JUCE's own repository.
- **`reaper/phase1-bench.lua`**: for VST3, draws lanes on unbound slots, then binds, renames
  during playback, binds all, unbinds, rebinds to another target, makes a lane after a bind,
  re-applies a lane's state chunk, sets the plugin offline and online, saves and reopens. For AU,
  binds all first (unbound slots cannot be drawn on), draws lanes, renames during playback, then
  hides and shows slots under them. Each lane is logged with the parameter REAPER attaches it to.
  REAPER is started with `-cfgfile` on a copy of the user's `reaper.ini`, so the real one is never
  written.

## What JUCE 9.0.5 does, read from its source

| Format | On `parameterInfoChanged` | Hidden | Groups |
| --- | --- | --- | --- |
| VST3 | `restartComponent (kParamTitlesChanged)` when a title changed (`juce_audio_plugin_client_VST3.cpp:1507`) | nothing in VST3 | VST3 units |
| AU | `PropertyChanged` on `ParameterList` and `ParameterInfo` (`juce_audio_plugin_client_AU_1.mm:2046`) | never set; unnamed parameters get `NonRealTime`; AUv2 has no hidden flag to set | clump ID = index of the group, fixed at construction (`:1091`) |

So an AU clump per *device* (devplan Phase 7) cannot follow a rebind through JUCE's parameter
groups: the clump is a property of the slot, not of what it is bound to.

## The matrix

| Check | Host | Format | Result |
| --- | --- | --- | --- |
| Rename while active | REAPER 7.84, Windows | VST3 | Parameter list: follows at once, also while playing. Existing lane: keeps its old title until reopen or FX offline/online. New lane: current name. |
| | REAPER 7.80, Mac mini | VST3 | Identical to Windows, step for step. |
| | REAPER 7.80, Mac mini | AU | Parameter list follows at once; the existing lane keeps its old title. |
| | Samplitude Suite 2026 (build 26015), Windows | VST3 | *Withdrawn:* a first run showed names waiting for the stop during playback, which the author attributes to his own handling; the build is uninstalled, so it cannot be repeated. The eight slots are one flat numbered list ("1: Device A /param/1" … "7: (unbound)"); the Device A / B groups do not appear. |
| | Samplitude Suite 2026 (build 26215), Windows | VST3 | **Follows at once, during playback too:** a rebind while playing changes the name straight away. |
| | Live 12.4.16, Windows | VST3 | **Follows at once, during playback too:** renames and bind/unbind while playing update the names, an existing clip envelope's chooser included ("Device A /param/20"). |
| | Bitwig 6.1.4, Windows | VST3 | **Follows at once, during playback too.** Slot 3's lane, rebound several times, carries each new name ("Device A /param/30", "OQA Spike 8" beneath); no group shown. |
| | Nuendo | VST3 | *no host* |
| | Live 12.4.16, Mac mini | VST3 | **Follows at once, during playback too**, as on Windows; all eight slots listed, bound or not. (The set saved, `test OQA spike 8.als`, records the device as `PluginDevice` / `Vst3PluginInfo`.) |
| | Live 12.4.16, Mac mini | AU | *Not run*: the AU proposal does not depend on it. |
| | GarageBand 10.4.14, Mac mini, hiding off | AU | **Follows at once, during playback too**; curves survive rename, unbind and rebind. |
| | GarageBand 10.4.14, Mac mini | AU | With list-omission hiding, the spike's slots are never offered for automation (the built-in EQ is), not even bound slots after saving and reopening: the plugin loads with every slot unbound, so its list is empty when GarageBand reads it, and GarageBand does not read it again. Loading it asks to lower GarageBand's security (the AU declares file and network access). Not pursued further: GarageBand is not a target (the author, 2026-11-08). |
| | Bitwig 6.1.4, Mac mini | VST3 | *pending, checklist* |
| | Logic | AU | *no host* |
| Hide an unbound slot | any | VST3 | Not available in the format; slots read `(unbound)`. REAPER and Samplitude list them without complaint, and Samplitude lets them be automated like any other. |
| | REAPER 7.80, Mac mini | AU | **Hides, destructively.** Unbound slots are absent from REAPER's list (3 parameters, REAPER's own). Unbinding a slot that has a lane deletes the lane and its points; binding it again brings neither back. |
| | REAPER 7.85, Mac mini, hiding off (`no-hide`) | AU | Not hidden, by design of the run: the eight slots stay listed. Unbind and rebind keep all three lanes and their points. |
| | GarageBand 10.4.14, Mac mini | AU | Hidden for good: no slot is ever offered, bound or not (above). |
| | GarageBand 10.4.14, Mac mini, hiding off (`no-hide`) | AU | All eight slots offered, **grouped by clump** as "Device A" and "Device B" (the first host to show the groups); listed in what looks like alphabetical order, so `(unbound)` slots come first and a slot moves in the list when it is bound. |
| | Live 12.4.16, Mac mini | AU | *Not run.* |
| Stale lane after rebind | REAPER, Windows and Mac | VST3 | Points kept on the slot's ID through unbind and rebind; the lane drives the new target under the old title. Fixed by reopening the project or setting the plugin offline and online (all titles refresh, state and points intact). Re-applying the lane's state chunk does not refresh it. |
| | REAPER 7.80, Mac mini | AU | With list-omission hiding, an unbind deletes the lane, so there is no stale lane, and no automation. After offline/online, the surviving lane's title lost its parameter name (" / OQA Spike 8"). |
| | REAPER 7.85, Mac mini, hiding off | AU | As VST3: the lane keeps its points and drives the new target under its old title. After offline/online or a reload, every lane reads "(unbound) / OQA Spike 8" while the parameters carry their bound names: REAPER takes AU lane titles from the plugin as it is created, before its state is restored. |
| | Samplitude Suite 2026, Windows | VST3 | Curves are neither removed nor changed by bind, unbind or rebinding to another path: the curve drives the new target under its new name. Saved, closed and reopened: every curve, bound or not, and every binding and name come back. |
| | Bitwig 6.1.4, Windows | VST3 | Slot 3 rebound several times: nothing lost, and the lane shows the current name each time. Saved and reloaded: bindings and curves come back. |
| | Live 12.4.16, Windows and Mac mini | VST3 | Nothing lost through bind, unbind and rebind during playback; saved and reloaded, everything comes back. |
| | the other hosts | | *pending* |
| pluginval, strictness 5 | Windows | VST3 | SUCCESS |
| | Mac mini (arm64) | VST3 | SUCCESS |
| auval | Mac mini, logged-in session | AU | AU VALIDATION SUCCEEDED (all slots unbound, hence hidden). Over SSH the component is not found: that session's registrar cached the bundle while it was still unsigned. |

Checked and ruled out, Samplitude: dragging an automation handle during playback moves the handle
to the play cursor or the cursor back to the handle. It does the same on a bound slot and on the
track's own volume curve, in Read mode: Samplitude's way of editing during playback, not the
plugin. Samplitude build 26215 no longer does it, and neither does Bitwig 6.1.4: points edit
freely during playback, wherever the cursor is.

REAPER puts a VST3 unit's name in each lane title (`Device A /bank 2 / OQA Spike 8 /
Device A`), so in REAPER the device grouping reaches the lane in VST3, against the PRD's "Flat;
group in name prefix". AU lanes show no clump name.

## For the author: what this changes in the PRD

1. **AU hidden slots.** The Binding model rule ("hidden where the format allows (CLAP
   `is_hidden`, AU `kAudioUnitParameterFlag_IsHidden`)") and the format table's AU column name
   a flag AUv2 does not have, and the one way to hide that AUv2 does have cost the automation in
   REAPER, and GarageBand never offers a slot that was hidden at load. With hiding off, REAPER
   keeps every AU lane and point through unbind and rebind, and GarageBand offers every slot,
   follows renames during playback, and shows the clumps. **Proposed:** AU behaves as VST3
   (unbound slots named `(unbound)`, never hidden), unless Live shows something better. CLAP's `is_hidden` is untouched by
   this. Since the rule is in a section `CLAUDE.md` copies verbatim, the change is made in the
   PRD and copied.
2. **The format table's VST3 rename cell ("Reaper honours it, others inconsistently") is the
   wrong way round** on today's hosts: Samplitude 26215, Bitwig 6.1.4 and Live 12.4.16 honour
   `kParamTitlesChanged` on lanes already drawn, during playback too; REAPER honours it for
   parameter names only, not for lanes already drawn, in either format.
3. **Stale lanes drive the wrong target silently** in VST3; a REAPER user's one-step fix is
   offline/online. Whether the plugin can refresh REAPER's titles itself (REAPER exposes its API
   to VST3 plugins) is a question for Phase 3, not measured here.
4. **The plugin needs the network in sandboxed AU hosts.** GarageBand asks the user to lower its
   security to load an AU that declares file or network access, and the real plugin cannot work
   without the network. Logic sandboxes AUs the same way; Phase 7 states what the user will be
   asked, and why.
5. **Mac bundles need an ad-hoc signature over the whole bundle** from the first build that a
   host is meant to load: a post-build `codesign --force --deep --sign -` for the plugin targets
   (Phase 3, or now in CI), real signing in Phase 7.

## clap-juce-extensions on JUCE 9.0.5 (information only)

Cloned at `7adee3a1` (2026-10-06) into `build/` and built with its own example CMake, CPM pointed
at the pinned JUCE 9.0.5 (`-DCPM_JUCE_SOURCE`), C++20: **`GainPlugin_CLAP` builds and links on
Windows with MSVC (VS 2026), no errors and no warnings.** Its wrapper already has a branch for
JUCE 8.0.11's `juce_audio_processors_headless` split. Not loaded in a host, not tried on macOS
or Linux. It stays out of the build, as decided; its source is what Phase 5b reads for embedding
a JUCE editor in a CLAP window.

## Rows that stay empty

Nuendo (no licence) and Logic (no machine), as the devplan allows.
