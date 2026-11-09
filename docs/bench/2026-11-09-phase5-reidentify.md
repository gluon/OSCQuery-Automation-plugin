# Bench, 2026-11-09: Phase 5, re-identification, stale slots, export

What Phase 5 built was measured on the author's Windows machine (Windows 11, VS 2026, Debug unless
said otherwise), against mock devices on loopback and a DNS-SD responder of the tests' own. The DAW
rows are the devplan's done-when moved to real devices and are filled by hand at the DAW; the rest
is the test suite's own word (`tests/core/StaleTests.cpp`, `ReidentifyTests.cpp`, `ImportTests.cpp`,
`FailureStatesTests.cpp`), repeatable with `ctest --preset vs-debug`.

## Against the mock devices, loopback

| Check | How | Result |
| --- | --- | --- |
| A path the device renames shows stale, with where it went | a sending slot live, then `PATH_RENAMED` from the mock | **`renamed`, `renamed_to` the new path; nothing sent to either path; no LISTEN on either; the chunk keeps the binding** |
| A path the device removes, then grows back | `PATH_REMOVED`, a move posted, then `PATH_ADDED` of the saved node | **`missing`, nothing sent; `ok` again, LISTENed again, the next move arrives** |
| A path the device retypes | `f` retyped `i` | **`retyped`, the detail naming both types; nothing sent** |
| A chunk reopened against a tree missing one of its paths | two ranged floats bound and saved; the fixture less the second served by another mock, the chunk pointed there | **one slot `ok`, one `missing`; both lanes moved, one message, for the live slot; the stale binding still in the chunk** |
| The tree changed since the bind | a chunk whose `tree_hash` is not this tree's | **the warning "the tree changed since the bind", `tree_changed` true, `tree_hash_live` the tree's 64 hex characters; `setDeviceTreeHash` with it clears both** |
| ACCESS as reported to this client (C6) | the same fixture on two mocks, one allow-listing 127.0.0.1, one not; a sending slot on each, `both` on the refused one | **`unauthorised` on the refused device (its own tree still says 3, the tree served says 1), nothing sent there, the allowed one gets the move; the `both` slot still receives from the refused device** |
| A session reopens against the same device on another address, found by NAME | a chunk saved against the mock; the mock restarted on another port and announced over DNS-SD under its NAME; the chunk loaded with the old port | **`searching`, then `confirmed` at the new port; the slot sends and receives; `saveState()` carries the new port** |
| Two devices sharing a NAME, told apart by the identity leaf | two mocks named alike, serials `TWIN-A` and `TWIN-B`; the chunk names the leaf and `TWIN-B`, its last address dead | **`confirmed` at the `TWIN-B` mock, `identity_live` `TWIN-B`; the other never sent to** |
| Two devices sharing a NAME, no identity leaf | as above, nothing to tell them apart, two instances | **`unresolved`, two `candidates`, both in `found`, the warning "2 found under its name"; nothing driven; `pointDevice` from one instance, both `pointed` and live; the other instance saves the pointed address too** |
| A different device at the last address | a mock answering as another NAME where the chunk expects its device | **`mismatch`, `answered_name` the other's, `sender_ready` false, the warning "answers as"; nothing sent, no LISTEN; the slot `pending` with the words** |
| A device pointed at by hand, answering as another NAME | the chunk's device dead, `pointDevice` at a mock named otherwise | **`pointed`, used (the move arrives), the warning "answers as"; the chunk keeps its name, saves the pointed address** |
| Two devices sharing a NAME, told apart by the tree hash | two mocks named alike, one serving the fixture less a leaf; the chunk's hash the full tree's | **`confirmed` at the full tree's mock; two candidates listed, one `hash_match`; no `tree_changed`** |
| Export, import | a bound instance's `saveState()` loaded by a fresh one | **every field the same; the fresh one's `saveState()` byte-identical; under `fr_FR` too** |
| An export from a bigger variant | 32 slots, two bound (1 and 20), loaded into 8 | **slot 1 bound, one warning naming 20** |
| The converter's output | `tools/convert-mapping.py` on the ToscA and OSCar fixtures | **loads as a chunk: one slot per lane at index − 1, the gap kept, ranges overridden, `both`; `$1` substituted; CI diffs the committed outputs** |
| A converted mapping against a device that does not serve its paths | the ToscA output pointed at the mock | **every slot `missing`, none silent** |
| The PRD's failure-states table | one case per row | **unreachable: `reachable` false, `last_seen_ms` −1, `last_error` set; unauthorised, stale: the slot's state; range unknown: `inactive` and the warning, `ok` once a range is entered; echo loop: `loop_guarded` counts, nothing delivered; dropped sends: `rate_limited` counts** |
| pluginval 1.0.4, strictness 5 | the Debug VST3 with the new editor, GUI tests included | SUCCESS |

Found on the way, in the library: a path LISTENed as the websocket opens had its first value read
twice (once by `onOpened`, once by the `listenNow` queued behind it); the worker now keeps the paths
on the wire since the open, dropped with a removed or renamed path. The 100 Hz receive case caught
it when LISTEN moved to after the device's confirmation.

Of what the status now carries: per instance `slots` (`id`, `state` of `pending`, `ok`, `missing`,
`retyped`, `renamed`, `unauthorised`, `inactive`, `detail`, `renamed_to`); per device `resolution`
(`checking`, `confirmed`, `mismatch`, `searching`, `unresolved`, `pointed`), `answered_name`,
`identity_path`, `identity_live`, `resolution_note`, `hand_pointed`, `candidates` (`host`, `port`,
`identity`, `identity_match`, `hash_known`, `hash_match`), `tree_hash`, `tree_hash_live`,
`tree_changed`, `last_seen_ms`; per process `found` (the Finder's list) and `finder_running`. The
core's suite is **110 cases** as `core.C` and `core.fr_FR`.

## At the DAW

The Release VST3 and `OSCQuery-Automation-Core.dll` built from `0d4f939` are installed in
`%LOCALAPPDATA%\Programs\Common\VST3\` (2026-11-09), over the Phase 4 build. The Phase 3 and Phase 4
rows ([`2026-11-09-phase4-record.md`](2026-11-09-phase4-record.md)) are still open and are filled
in the same sitting.

| Check | How | Result |
| --- | --- | --- |
| The hand path: a session moved to a device at another address | a REAPER session bound to WFS-DIY on this machine (`127.0.0.1:5005`), saved; WFS-DIY here closed, WFS-DIY.app on the Mac mini started; reopen | _to fill_: every WFS-DIY row reads "searching" (WFS-DIY does not announce over DNS-SD, C3); *Point…* on one instance's row, then the Mac mini's address typed or its row clicked; every instance goes "ready" |
| The DNS-SD path: a device re-found with no click | the same with go.dot, which announces: bound here, saved, go.dot here closed, Go.dot.app on the Mac mini running; reopen | _to fill_: "searching", then "ready" at the Mac mini's address without a click; the next save carries it |
| Two devices sharing a NAME | two mocks: `oqa-mock-device --name Twin --port 5101` and `--name Twin --port 5102`, each announced (or typed); a session bound to one with its `/system/serial` as identity (*Identity…*, then the leaf) | _to fill_: reopened with the first dead, the right one is picked; without the identity, "unresolved: 2 found" with both candidates beneath, one click points |
| Stale, live | a parameter renamed or removed in Chataigne or ossia score while bound | _to fill_: the row red, the old path shown with where it went, *Rebind* binds it there in one click (or arms the slot when the device only removed it) |
| Stale, at reopen | a WFS-DIY channel deleted between save and reopen (WFS-DIY sends no `PATH_REMOVED`, L2) | _to fill_: red at reopen, not live |
| Export, import across hosts | *Export…* from a REAPER instance; *Import…* into a Bitwig (VST3) instance | _to fill_: identical bindings, the lanes renamed |
| The converter against a real mapping | a ToscA or OSCar file of the author's, when one turns up | _to fill_ |
| The Mac mini and the NUC | `scripts/farm-check.sh` on a bundle clone | _to fill_ |

## How the hand-path row is run

1. Rescan plugins in REAPER if the plugin is new to it (Preferences, Plug-ins, VST, Re-scan).
2. Insert "OSCQuery Automation 32" on two tracks, open an editor; add `127.0.0.1:5005`, click WFS-DIY
   (it joins the session once it has answered HOST_INFO), bind a few leaves on each track. Save the
   session.
3. Close WFS-DIY here; start WFS-DIY.app on the Mac mini with OSC Query on. Reopen the session: the
   device rows read "searching, seen N s ago"; the slots' value cells say the device is being looked
   for; nothing is sent.
4. In one instance's editor, select the device row, click *Point…*, then type `macmini:5005` and Add
   (or click the device's row under found, if the Finder has it). Every instance's row reads "pointed,
   ready" within a second; the lanes drive the Mac mini's WFS-DIY. Save: the chunk holds the new address.
5. For the identity leaf: select a device row, click *Identity…*, click a read-only leaf in the tree
   (WFS-DIY's tree has none unique per installation yet; the mock's `/system/serial` is the example).
   The row shows `(id …)`; *Clear identity* undoes it.
6. For a stale slot: in Chataigne, rename a bound parameter; the slot row turns red, its path cell
   shows `old -> new`, the Test button reads *Rebind*; click it.
