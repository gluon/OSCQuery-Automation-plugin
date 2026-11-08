# tests/mock-device

The mock OSCQuery device: what every test drives instead of a real device, and the conformance
CLI's first subject (devplan Phase 0). It serves a tree from a JSON fixture in `GET /` shape over
HTTP and WebSocket on one port: `?HOST_INFO` with a real `OSC_PORT`, subtree GETs, attribute
queries, `LISTEN`/`IGNORE` with values streamed as binary OSC. A test changes the device under the
client's feet with `setValue`, `rename`, `remove`, `retype` and `add`, which send `PATH_RENAMED`,
`PATH_REMOVED`, `PATH_CHANGED` and `PATH_ADDED` when `Options::extensions` lists them.
`Options::websocket = false` is a device without a websocket; `hostInfoOnly` one that answers
`?HOST_INFO` and nothing else; `notifyUnlisted` one that sends notifications it does not list (as
WFS-DIY sends `PATH_CHANGED`); `oscTransport = "TCP"` one whose `OSC_PORT` is a TCP listener;
`oscIp` one that states `OSC_IP`. `setValueBundle` streams one bundle frame, `closeAllConnections`
drops every websocket client, and what arrives on `OSC_PORT` (datagrams, or a TCP stream's bytes)
and as binary websocket frames is kept for a test to read back. `MockDevice.h` has the details.

By hand, after a build:

```
oqa-mock-device --fixture tests/fixtures/trees/generic-device.json [--port N] [--name NAME]
oqa-mock-device --generate 10000                      # a synthetic tree of 10,000 leaves (tests/support/TreeGen.h)
oqa-mock-device http 60068 osc 49670
curl "http://127.0.0.1:60068/?HOST_INFO"
```

Flags: `--no-websocket`, `--extensions A,B,C`, `--host-info-only`, `--notify-unlisted`, `--tcp`,
`--osc-ip IP`. It knows no namespace: every address it serves comes from the fixture or from the
generator's counters.
