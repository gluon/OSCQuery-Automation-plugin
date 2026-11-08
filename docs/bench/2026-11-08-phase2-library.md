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
| **WFS-DIY** | Release build at `../WFS_DIY_v1/Builds/VisualStudio2022/x64/Release/App/`, OSC Query enabled by the author in its Network tab (off by default: `networkOscQueryEnabled` is 0), bound to 172.22.1.11, UDP 8000 | `oscquery-dump 127.0.0.1:5005` and `172.22.1.11:5005`; then `--listen /wfs/input/1/positionX --seconds 14` on loopback while three `/wfs/input/positionX ,if 1 <x>` messages went to UDP 8000 at the interface address (WFS-DIY skips pushing to a websocket client at the UDP sender's IP, so the two had to differ) | **The done-when, met.** HOST_INFO (NAME `WFS-DIY`, UDP, websocket offered, EXTENSIONS as its source says: `LISTEN` listed, `PATH_CHANGED` not); **1,393 nodes, 1,347 leaves** printed on both addresses with the same tree hash; LISTEN accepted; the current value (`f:-7.7777777`) read over HTTP first, then **`f:3.25`, `f:-1.5`, `f:10`** as the three messages landed (12.0 clamped to 10 by WFS-DIY itself); `GET ?VALUE` afterwards agreed. The value was put back after. First attempt failed for a reason outside the library: Git Bash rewrote the `/wfs/...` arguments into Windows paths; `MSYS_NO_PATHCONV=1`. |
| Chataigne | installed at `C:\Program Files\Chataigne` | started, waited 25 s, listening ports of its process listed | **No listening TCP port opened**, so no OSCQuery server to read; the usual 42000 did not answer. Likely a first-run dialog or its OSCQuery server disabled; not investigated further without a window. Row open. |
| DNS-SD browse (`oscquery-dump --browse`) | this machine, go.dot announcing beside it as "Go.dot on …" | 6 s with go.dot serving, 8 s with Chataigne up | Mode `browsing` (port 5353 bound and the group joined), **nothing found either time.** Four processes already held UDP 5353 on this machine (the system's responder on 0.0.0.0 and three per-interface binds). Hypothesis, not measured: Windows hands a datagram on a shared port to one socket, so the query and the answer do not both reach the two programs that need them; this is the case the browser's unicast fallback and the Finder's typed entries exist for. The browser and announcer are proven against each other on loopback in `MdnsTests.cpp` and `FinderTests.cpp`. |
| DNS-SD browse, **another machine announcing** | the Mac mini (172.22.1.27, macOS 15.8.2) announcing `Bench on POB12` as `_oscjson._tcp` on 5005 through its own Bonjour, `dns-sd -R`, for 45 s | `oscquery-dump --browse 12` here, twice | **Found both times**, within the first seconds: name, `172.22.1.27:5005`, `dns-sd`, probed and listed **down** with `could not connect to 172.22.1.27:5005`, which is right (nothing serves there). So the browser reads a real responder across the LAN on this machine; the row above is a same-host matter. The entry outlived the announcer's 45 s in the second browse: it was killed by a signal and sent no goodbye, so its records live out their TTL, as they should. The Mac's only go.dot binary is an older spike build whose `serve` has another command line, so no reachable *found* device was had; a real device or a current go.dot build on the Mac gives that row later. |

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
