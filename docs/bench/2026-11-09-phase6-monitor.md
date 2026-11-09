# Bench, 2026-11-09: Phase 6, the editor finished, the plugin's own server, the Monitor

What Phase 6 built was measured on the author's Windows machine (Windows 11, VS 2026, Debug unless
said otherwise) by the test suites against mock devices on loopback, by pluginval 1.0.4 on the two
VST3s and by clap-validator 0.4.1 on the Release module, and on the farm by `scripts/farm-check.sh`.
The DAW rows are the devplan's done-when moved to REAPER and Bitwig and are filled by hand; the rest
is the suites' own word, repeatable with `ctest --preset vs-debug`.

## Against the mock devices, loopback

- **Settings persisted (6a-1):** a second `Registry` on the same file reads back every key; bad JSON
  is ignored with a warning in the status (`tests/core/SettingsTests.cpp`).
- **The Monitor's data (6a-2):** 32 instances against two mock devices are listed with their bound
  slots and the registry's word on each, both devices with live rates over the registry's one-second
  window; an inactive instance keeps its id and sends nothing (`StatusTests.cpp`).
- **Timing process-wide (6a-3):** a timing set by hand from one instance stands through another's
  push and lands in both chunks on save (`TimingTests.cpp`); set by NAME from a zero-slot Monitor
  instance it does the same (`MonitorModelTests.cpp`).
- **The generic server (6b-1):** the library's own client against the library's own server: C1, C4,
  C6, C7, L1 to L4 as the spec words them, a refused write counted
  (`lib/oscquery/tests/ServerTests.cpp`).
- **The plugin's server (6b-2):** the library's client reads `/plugin/<id>/slot/<n>/...` for two
  instances; `treeHash` holds across a stop and start (C5); a LISTEN on `value` streams a host move;
  PATH_REMOVED when an instance goes; the announce is seen by the loopback responder; a pinned port
  in use is `server.error`, never another port (`tests/core/ServerTests.cpp`).
- **Writes over the server (6b-3):** a slot bound, ranged, scaled, directed, named and unbound by a
  write over websocket and over UDP; refused from a non-allowed address and counted; an unknown
  device refused with the reason (`CommandTests.cpp`). **The done-when's second clause, headless:**
  a write to the plugin's server with no gui makes the CLAP harness see `rescan (INFO)` and the new
  name (`tests/clap/ClapServerTests.cpp`). `tools/oqa-bind.py --dry-run` prints the packets.
- **The editor's words (6d-1):** every row of the PRD's failure table has its shade and its tooltip
  from the slot's standing alone (`PresentationTests.cpp`); *Clear stale* unbinds the missing, the
  retyped and the renamed and nothing else, with no `clear()` to a CLAP host; a user's range goes
  back to the device's.
- **The Monitor (6c, 6e):** every row of the view from a status fixture, instances and slots by id,
  the server line in words; live, a zero-slot instance registers beside a slot plugin's and reads the
  process-wide status (`MonitorModelTests.cpp`); as a CLAP it is created, activated, processed with
  audio through, has no params extension, is listed as an instance of zero slots, and its state is a
  chunk of no slots that loads back (`tests/clap/ClapMonitorTests.cpp`).
- **`$instance` (6d-2):** the substitution; a slot bound through a pattern keeps it, follows the
  number, keeps its binding and says why when the number has no path; the chunk carries `instance`
  and `path_pattern` (`tests/state/v1-instance.json`); a Monitor's command binds a pattern in another
  instance, which substitutes its own number (`PatternTests.cpp`).

The suites: **unit 11, oscquery 106, core 134, clap 15 cases**, each run under C and fr_FR.
**pluginval 1.0.4 at strictness 5 passes the Debug VST3 of the 32-slot plugin and of the Monitor.**
**clap-validator 0.4.1 on the Release module, six plugins: 229 tests run, 155 passed, 0 failed,
74 skipped** (the Monitor skips the parameter tests, having no params extension: the validator
divides by the parameter count otherwise, `src/tests/plugin_instance/params.rs:82`).

## At the DAW

Install, on this machine, the Release build of the commit the record names: the 32-slot VST3 and the
Monitor VST3 with a copy of `OSCQuery-Automation-Core.dll` beside each bundle into
`%LOCALAPPDATA%\Programs\Common\VST3\`, and the CLAP module with its copy into
`%LOCALAPPDATA%\Programs\Common\CLAP\` (as the Phase 5b report). Windows asks once whether the host
may listen on the network when the plugin's server opens its port: say yes for the private network,
or the server answers loopback only (`docs/bench/2026-11-09-phase5b-clap.md` has the CLAP paths).

| Check | How | Result |
| --- | --- | --- |
| Every PRD UX item reachable | REAPER and Bitwig, VST3 and CLAP: click-bind, drag a leaf onto a row, type a path with `$instance`, bulk `fff`, *Clear stale*, RANGE greyed when overridden and cleared back, inline name, *Copy JSON*, search, reachability and last seen, rates each way, websocket state, last error, *Test* | _to fill_ |
| A script binds a slot with no editor open | REAPER with the plugin's editor closed: `python tools/oqa-bind.py --list`, then `--plugin <id> --slot 3 --device <NAME> --path <p>` | _to fill_: the lane renames (REAPER on reopen of the lane list, Bitwig at once) |
| The Monitor with 32 instances against two devices | 32 tracks with the plugin, half on WFS-DIY, half on go.dot; the Monitor on a track or from any editor's *Monitor…* | _to fill_: 32 rows, live rates on both devices, the server line with its port |
| Two DAWs on one machine | REAPER and Bitwig both running the plugin; `oscquery-dump --browse` or the other's Finder | _to fill_: two NAMEs, "OSCQuery Automation @ <machine> (REAPER)" and "(Bitwig Studio)", distinct ports |
| The firewall prompt | the first host to load the plugin after install | _to fill_: one prompt, for the host, once |
| The timing by hand | the Monitor's Timetags tick and Offset on WFS-DIY; a save and reload of a session | _to fill_: the chunk carries it; the other instances' panes show it |
| Pointing by hand from the Monitor | WFS-DIY moved to another port; its Address cell edited | _to fill_: every instance follows |
| The farm | `scripts/farm-check.sh` on a bundle clone: the Mac mini and the NUC, the Monitor VST3 validated too | _see below_ |

## The farm

Both machines on `c265481` plus the exit fix below, from a bundle, `~/oqa-6b`. Neither had internet
that evening (the Mac mini's `curl` could not resolve github.com either), so pluginval and
clap-validator ran from the copies already there (`~/Documents/pluginval`, `~/clap-validator` on the
Mac; `/tmp/pv`, `/tmp/cv` on the NUC).

| Machine | Suites (C and fr_FR) | pluginval 32 | pluginval Monitor | clap-validator (six plugins) |
| --- | --- | --- | --- | --- |
| Mac mini (macOS 15.8, arm64) | 8/8 passed | SUCCESS | SUCCESS | 229 run, 156 passed, 0 failed, 73 skipped |
| NUC (Ubuntu 24.04, GCC 13) | 8/8 passed | SUCCESS | SUCCESS | first run: 155 passed, **1 crashed**, 1 warning (denormals 3.2x, a timing warning on a pass-through), 72 skipped; after the fix: three runs, each 229 run, 156 passed, 0 failed, 73 skipped |

The crash, read from the core with `coredumpctl`: the websocket thread of the
`process-audio-double-out-of-place` subprocess for `Q128` was answering an HTTP request (a sibling
subprocess's Finder probing the announced server) inside `reply()`, while the main thread sat in
`Registry::shutdown()` at process exit. SimpleWeb's status-code table is a function-local static
made on the first reply, destroyed before the registry singleton that was made earlier; the reply
read a destroyed map. The library now makes the table when it loads, and the registry's shutdown
stops the server's serving before anything else. The Mac mini after the fix, twice: 156 passed, 0 failed each time.

## How the second row is run

1. Install as above; start WFS-DIY with OSC Query on (HTTP 5005, OSC 8000).
2. REAPER: insert "OSCQuery Automation 32" (VST3) on a track, open its editor once so the device is
   added (type `127.0.0.1:5005`, Add, click the device), close the editor.
3. In a terminal: `python tools/oqa-bind.py --list` prints the server found over DNS-SD, its
   instances and their slots. Then `python tools/oqa-bind.py --plugin <id> --slot 3 --device
   "<WFS-DIY's NAME>" --path /<a writable float>`.
4. REAPER: the track's envelope list shows the lane under its new name on the next open of the
   list; Bitwig renames the lane at once.
