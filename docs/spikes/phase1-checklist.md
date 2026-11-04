# Phase 1 bench: the checklist for hosts driven by hand

For the author, 2026-11-04. REAPER is measured by script (`spikes/phase1_formats/reaper/`); these
hosts have no script interface we can drive, so their rows of
[`phase1-formats.md`](phase1-formats.md) wait for these answers. About ten minutes per host.

| Host | Machine | Format |
| --- | --- | --- |
| Samplitude | Windows | VST3 |
| Ableton Live 12 | Windows | VST3 |
| Bitwig Studio 6.1.4 | Windows | VST3 |
| Ableton Live 12.4.3 | Mac mini | AU, then VST3 |
| GarageBand 10.4.14 | Mac mini | AU |
| Bitwig Studio 6.1.4 | Mac mini | VST3 |

**Where the spike is.** "OQA Spike 8" by Pix et Bel.

- Windows: `%LOCALAPPDATA%\Programs\Common\VST3\OQA Spike 8.vst3`, the per-user VST3 folder. If a
  host does not scan it, add that folder to the host's VST3 paths (or copy the bundle to
  `C:\Program Files\Common Files\VST3`, which needs an administrator).
- Mac mini: `~/Library/Audio/Plug-Ins/Components/OQA Spike 8.component` and
  `~/Library/Audio/Plug-Ins/VST3/OQA Spike 8.vst3`.

Its editor has eight rows (Slot 1 to 8): a name field, **Bind**, **Unbind**, and the slot's state.
Slots 1–4 are in group "Device A", 5–8 in "Device B". Every slot starts unbound, named
`(unbound)`.

## Steps, per host

1. Insert the spike on an audio track. Open its editor.
2. In the host, show the automation lanes for **Slot 1** and **Slot 2** (whatever the host calls
   them now) and draw a curve on Slot 1. *Note what the lanes are called, and how the host's
   parameter list shows eight parameters with the same name.*
3. Click **Bind** on Slot 1. *Does the lane title change? Does the host's parameter list or
   automation menu show the new name?*
4. Start playback. Edit Slot 1's name field (add " renamed") and click **Bind** again. *Does the
   lane follow while playing?*
5. Stop. Click **Unbind** on Slot 1. *Back to `(unbound)`? On AU: does Slot 1 leave the host's
   parameter list? Is the curve still there?*
6. Type a different name for Slot 1 (say "Device B /source/1/position x") and click **Bind**.
   *What does the lane show now? Is the curve still there and still driving Slot 1? What would a
   user have to do to see the truth?*
7. On AU: *are the parameters grouped under Device A and Device B?*
8. Save, close and reopen the session. *Are the names and the curve back as they were?*

A line or two per step is plenty, or a screenshot. Anything odd (a crash, a parameter list that
goes blank, a lane that detaches) is worth more than the steps that behave.
