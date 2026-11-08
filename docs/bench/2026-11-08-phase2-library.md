# Bench, 2026-11-08: the client library's example against what was on hand

Phase 2's done-when: "a command-line example prints WFS-DIY's tree, subscribes to a path and prints
its changes, with no address typed". The example is `oscquery-dump` (`lib/oscquery/examples/`), a
Debug build on the author's Windows 11 machine, run from a shell with no window open, so nothing
could be clicked. Every row is what happened, including the ones that did not.

| Subject | Version | What was run | Result |
| --- | --- | --- | --- |
| go.dot `wfg serve` (fixture bundle `devices`, HTTP 5010, OSC 8010) | go.dot `1c8aaa0a`, Debug (`build/strict-vs`) | `oscquery-dump 127.0.0.1:5010`, then `--listen-first --seconds 4` | HOST_INFO read (NAME `Go.dot`, UDP, websocket offered, EXTENSIONS listed); **820 nodes, 680 leaves** printed, tree hash computed; LISTEN on `/desk/fader` accepted, state `listening`, no value printed: that leaf carries no `VALUE`, so the HTTP read after LISTEN answered 204 and nothing moved in 4 s. Correct, and nothing to see. |
| The mock device as a program (`oqa-mock-device`, generic fixture) | this repository, Debug | `oscquery-dump 127.0.0.1:<port>`, then `--listen-first --seconds 2` | Tree printed with ranges, units, values and descriptions; LISTEN on the first readable leaf, its current value read over HTTP and printed (`f:-6.5`). |
| The mock, `--generate 10000` | idem | `oscquery-dump 127.0.0.1:<port>` | 10,101 nodes, 10,000 leaves printed and hashed. The suite's L5 case on the same tree: `GET /` of 1,569,437 bytes in 567 ms, fetched and parsed in **1,382 ms**, one subtree of 100 leaves re-read in 20 ms (Debug, loopback). |
| **WFS-DIY** | Release build at `../WFS_DIY_v1/Builds/VisualStudio2022/x64/Release/App/` | started; `oscquery-dump 127.0.0.1:5005`, ten tries over 20 s | **Unreachable: WFS-DIY's OSCQuery server is off by default** (`networkOscQueryEnabled` is 0 in `WFSValueTreeState.cpp`; the toggle is in its Network tab, `NetworkTab.h`). It cannot be switched on from a shell, and the author's settings were not edited. **Row open for the author:** enable OSC Query in WFS-DIY's Network tab, then `oscquery-dump 127.0.0.1:5005 --listen-first` and move the first input's fader. |
| Chataigne | installed at `C:\Program Files\Chataigne` | started, waited 25 s, listening ports of its process listed | **No listening TCP port opened**, so no OSCQuery server to read; the usual 42000 did not answer. Likely a first-run dialog or its OSCQuery server disabled; not investigated further without a window. Row open. |
| DNS-SD browse (`oscquery-dump --browse`) | this machine, go.dot announcing beside it as "Go.dot on …" | 6 s with go.dot serving, 8 s with Chataigne up | Mode `browsing` (port 5353 bound and the group joined), **nothing found either time.** Four processes already held UDP 5353 on this machine (the system's responder on 0.0.0.0 and three per-interface binds). Hypothesis, not measured: Windows hands a datagram on a shared port to one socket, so the query and the answer do not both reach the two programs that need them; this is the case the browser's unicast fallback and the Finder's typed entries exist for. The browser and announcer are proven against each other on loopback in `MdnsTests.cpp` and `FinderTests.cpp`. **To bench on the home network** with a device on another machine (the Mac mini running go.dot, or a real device), and with `dns-sd -B _oscjson._tcp` on the Mac as the second opinion. |

## What the suite covers that the bench could not

`oqa_oscquery_tests`, 90 cases and 31,830 assertions, under C and fr-FR on Windows (Debug):
HOST_INFO shapes (the mock's, WFS-DIY's as read from its server source, one with `OSC_IP` and a
list for `EXTENSIONS`, one missing every Core field); the tree model on the generic fixture and on
hand-built shapes; the four notifications; a device that lists `LISTEN` but not `PATH_RENAMED`
(the rename goes unreported) and one that sends `PATH_CHANGED` without listing it (handled, as
WFS-DIY needs); no websocket; a dropped websocket coming back with its subscriptions; a device that
appears after the client started; OSC out over the websocket; SLIP and length-prefix framing on
hand-written bytes; bundles under the MTU; UDP and TCP to the mock's `OSC_PORT`; the recents file;
the large tree.

## Not benched, and why

- **Record from a controller move** in WFS-DIY: the row above.
- **ossia score**: not started; no window.
- **A distinct `WS_PORT`**: no device on hand serves one; the mock does not either (noted in the
  devplan record as a mock gap).
- **The announcer against another machine's browser**: Phase 6 makes the plugin announce; the
  announcer is tested on loopback now.
