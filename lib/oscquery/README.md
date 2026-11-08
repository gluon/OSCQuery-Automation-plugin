# lib/oscquery

The OSCQuery client library (devplan Phase 2, built 2026-11-08): reads any OSCQuery server
headless, under test, with no plugin code above it. Working name `oqa::oscquery`, target
`oqa::oscquery`, includes as `<oscquery/...>` (the author, 2026-11-08); Phase 8 renames it when
it becomes a repository of its own, with the conformance CLI as its first consumer.

It links `oqa::deps` only: JUCE's headers and switches, never a JUCE module's sources, so it goes
into the plugin (which compiles its own JUCE) and into the tests and tools (which get theirs from
`oqa_juce`) without JUCE compiling twice in either. No GUI module, no `juce::Timer`, no
`MessageManager`: a headless tool runs it.

## What is in it

| Unit | What it does | Origin |
| --- | --- | --- |
| `osc/Value`, `osc/Address`, `osc/Codec` | OSC 1.1 values (`i h f d s b T F N I t`), address rules, a bounds-checked decoder that never guesses and an encoder that refuses what the wire cannot carry; locale-independent number text | go.dot `src/wfg/engine/osc/` at `1c8aaa0a` |
| `osc/Framing` | SLIP (OSC 1.1, the default) and length-prefix (OSC 1.0 stream) framing, pure, with a deframer that drops what is too long and counts what is wrong | here |
| `osc/Bundler` | Messages into bundles of at most the MTU (1,400 bytes by default), one time tag each, in order; what cannot fit alone is refused by index | here |
| `net/HttpGet` | One HTTP GET over a raw socket, status read off the wire, chunked bodies joined. Not `juce::URL`, which rewrites `?HOST_INFO` into `?HOST_INFO=` | go.dot `OscQueryClient::get` |
| `net/UdpEndpoint`, `net/SocketBuffers` | A UDP socket on its own thread that names each datagram's sender; sends from a bound or an ephemeral socket; asks the system for buffer room | go.dot |
| `net/TcpSender` | One connection to a TCP `OSC_PORT`, each packet framed and written whole; a failed write drops the connection so the next send reconnects | here |
| `HostInfo` | `HOST_INFO` as the Core profile reads it (C1): the three required fields, the `OSC_IP` and `WS_PORT` fallbacks, `EXTENSIONS` never inferred, `missing` for what C1 would fail | here |
| `tree/Node`, `tree/Tree` | The tree model: per-argument views of `fff`, the PRD's bindable predicate, `merge`/`remove`/`rename` for the notifications, `loaded` for containers sent without `CONTENTS`, extras kept verbatim, and `treeHash()` (C5, SHA-256 over sorted `FULL_PATH TAB TYPE` lines of every typed leaf) | here |
| `Device` | One device: probe, fetch, websocket `LISTEN`/`IGNORE`, values (messages and bundles), the four `PATH_*` notifications, reconnect with backoff and re-`LISTEN`, OSC out over the websocket. Callbacks on its own thread; snapshots and a revision for pollers | here (the LISTEN shape and the re-LISTEN on open follow WFS-DIY's client; nothing copied) |
| `mdns/Mdns`, `mdns/Discovery`, `mdns/MulticastJoin` | Multicast DNS: the codec (pure), a browser and announcer sharing port 5353 with the system's responder on every IPv4 interface, falling back to a port of its own asked directly | go.dot `src/wfg/engine/oscquery/` |
| `Finder` | Found and typed devices in one list, each probed with `GET /?HOST_INFO` before it is listed; the unreachable are shown, not hidden | here |
| `Recents` | The per-user recents store: `NAME`, last host and port, the identity leaf when set; a `juce::PropertiesFile` at the standard location | here |

Copied files keep go.dot's header and carry an `Origin:` line (`scripts/check-headers.py`).

## What the device says goes

Nothing optional is assumed: the websocket opens only when `EXTENSIONS` lists `LISTEN` or a
`PATH_*` notification, and `listen()` answers *unsupported* on a device that does not list
`LISTEN`. One recorded leniency: a notification that *arrives* is handled whether or not it was
listed, because WFS-DIY sends `PATH_CHANGED` without listing it, and the library copes with
WFS-DIY as it is. Nothing is ever sent that the device did not list.

Large trees, as far as OSCQuery allows: the proposal has no depth-limit field and `GET /` is
always the whole tree, so a tree above `Device::Options::largeTreeThreshold` (5,000 nodes) is
fetched once and never whole again; every later change arrives by subtree GET of the path a
notification names.

## Tests and the example

`tests/` is the library's own executable, `oqa_oscquery_tests`, run by CTest as `oscquery.C` and
`oscquery.fr_FR`. It drives the mock device in `../../tests/mock-device/` and never a real one;
every path comes from a fixture or from `tests/support/TreeGen.h`'s counters.

`examples/oscquery-dump` is Phase 2's done-when: `oscquery-dump --browse` lists what DNS-SD finds;
`oscquery-dump host:port [--json] [--listen <path> | --listen-first]` prints `HOST_INFO`, the tree
and, with `--listen-first`, the values of the first leaf this client may read, until Ctrl-C.
