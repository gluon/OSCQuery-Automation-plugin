# Bench, 2026-11-09: Phase 4, record

What Phase 4 built was measured on the author's Windows machine (Windows 11, VS 2026, Debug unless
said otherwise), against the mock device on loopback. The DAW rows are the devplan's done-when and
are filled by hand at the DAW, together with the Phase 3 rows still open (decided by the author
2026-11-09: one sitting for both); the rest is the test suite's own word (`tests/core/ReceiveTests.cpp`,
`tests/core/UdpReceiveTests.cpp`, `tests/core/GestureTests.cpp`), repeatable with `ctest --preset vs-debug`.

## Against the mock device, loopback

| Check | How | Result |
| --- | --- | --- |
| Every bound path is LISTENed, sending ones included; an unbound one is IGNOREd | a sending and a receiving slot, then the receiving one unbound | **both paths have one listener; the unbound path none, the other still one** |
| A streamed value arrives scaled to 0–1 by its slot | a read-only float at 37.5 % of its range; a toggle; a user-entered log range 20–20000 given 2000 | **0.375; 1 then 0; the log curve's 0.667**, each within 1e-4, one wake per value |
| On LISTEN the device's current value arrives once | every receiving slot, at bind | the slot starts where the device is (the library reads the value over HTTP on LISTEN) |
| An `fff`'s arguments no slot drives follow the device | one argument of three bound, the device moving all three, then the bound one automated | **one message of three, the two undriven carrying what the device last said** |
| A plan made before the tree arrived is rebuilt when it lands | the same case: bindings pushed at connect, the tree loading after | the first plan had two arguments; the rebuilt one three (`plans` 2 in the status) |
| Loop guard, inside the window | the mock echoing what it is sent after 10 ms, a slot in both directions, the guard at 50 ms | **nothing delivered, `loop_guarded` counts it** |
| Loop guard, outside the window | the same echoing after 150 ms | **delivered as 0.6 (the device moving), `loop_guarded` unchanged** |
| The host handing back what was received sends nothing | the delivered value fed to `processBlock` | **no datagram**; a real move after it, one |
| 32 slots in both directions at the device's rate | a generated tree, 32 slots as `both`, the mock bundling 32 values every 10 ms for 2 s | **6,400 values delivered, every slot's 200 in order, none missed, none echoed back, guard 0** |
| A receiving slot without a range | an unranged float set to receive | not LISTENed, nothing delivered, the warning says "neither sends nor receives" |
| Two instances receiving on one path | both bound to one read-only float | one websocket, both get the value |
| The audio path allocates nothing with receiving slots | `both` slots and a receiving one, the host handing a received value back, 500 blocks | **0 allocations** |
| A saved chunk with receiving slots | `tests/state/v1-receive.json` moved to the mock's address | receive and both slots record, the sending one's path is LISTENed and delivers nothing |
| A device without LISTEN, told the port | the mock HTTP-only (honest `EXTENSIONS`), `setFeedbackTarget` at the registry's UDP port | **nothing before it is told; after, delivered exactly as over LISTEN, bundles too; `udp_received` 1** |
| Two devices without LISTEN on one machine | two mocks serving the same fixture, distinct paths bound, then one path on both | each gets its own; **the shared path goes to both and `udp_ambiguous` counts it** (the sender's address tells nothing apart on loopback) |
| A device whose websocket streams | the mock with LISTEN also sending UDP copies | **delivered once; the UDP copies counted as `udp_ignored`** |
| The receive port setting | set to a free port, then to 0 | **bound there, works there; 0 gives an ephemeral port again** |
| The gesture | the tracker on the test's clock | opened by the first value, held at 100 Hz for 5 s, closed 201 ms after the last, reopened by the next; 32 slots each on its own clock; `closeAll` |
| pluginval 1.0.4, strictness 5 | the Debug VST3 with the gesture path and the new editor, GUI tests included | SUCCESS |
| Post to the wire, after the change | the Phase 3 harness, 200 blocks | median 140–150 µs, p99 320–510 µs this day (Phase 3 measured 96 / 227 µs; a Debug number that moves with the machine's load) |

Of what the status now carries: per device `listen` (`listening`, `unsupported`, `pending`),
`values_received`, `loop_guarded`, `udp_received`, `udp_ignored`; per process `receive_port`,
`receive_addresses`, `udp_datagrams`, `udp_refused`, `udp_unrouted`, `udp_ambiguous`; per instance
`received`, `received_dropped`, `plans`.

## At the DAW

The Release VST3 and `OSCQuery-Automation-Core.dll` built from `8d548a8` are installed in
`%LOCALAPPDATA%\Programs\Common\VST3\` (2026-11-09), over the Phase 3 build. WFS-DIY runs on this
machine with OSC Query enabled (HTTP 5005, OSC UDP 8000, its feedback to the client's UDP 9000).

### The Phase 3 rows, still open

| Check | How | Result |
| --- | --- | --- |
| REAPER (Windows): pick WFS-DIY, bind x/y/z of one input, draw curves, the source moves | the author, at the DAW; the steps are in [`2026-11-08-phase3-send.md`](2026-11-08-phase3-send.md) | _to fill_ |
| 32 tracks, one bundle per block on the wire | Wireshark on loopback, filter `udp.port == 8000` | _to fill_ |
| The session reloads with its bindings | save, close, reopen | _to fill_ |
| REAPER (macOS, the Mac mini): the same | the farm, `scripts/farm-check.sh` on a bundle clone | _to fill_ |

### The Phase 4 rows

| Check | How | Result |
| --- | --- | --- |
| WFS-DIY as the record source | one input's position on three slots, direction `both`; the lanes in Touch; move the input in WFS-DIY's map | _to fill_: the lanes write while the input moves, and stop 200 ms after |
| Playback after the record | the track in Read; play | _to fill_: the source follows the lanes; the status bar's `guard` stays at 0 during playback |
| Chataigne as the source | bind one float of its tree to a receiving slot, move it there | _to fill_ |
| ossia score as the source | the same | _to fill_ |
| go.dot as the source | the same | _to fill_ |
| 32 receiving slots at the device's rate | 32 floats of go.dot's or WFS-DIY's tree, swept there; the status bar's rx/s against the lanes | _to fill_: no missed value at the device's update rate |
| A device without LISTEN, over UDP | the mock HTTP-only (`oqa-mock-device --no-websocket --extensions ACCESS,VALUE,RANGE,TYPE`), the status bar's receive port typed in by hand (`setFeedbackTarget` has no flag: a `--feedback host:port` is a one-line addition when the bench wants it); WFS-DIY too if its client table can refuse the plugin's websocket while still sending to 9000 (receive port set to 9000 in the status bar) | _to fill_ |
| Bitwig 6.1.4 over VST3 (an extra row, not a gate) | the WFS-DIY row again | _to fill_ |
| The Mac mini: the WFS-DIY row | REAPER there, against WFS-DIY.app | _to fill_ |

## How the record row is run

1. Rescan plugins in REAPER if the plugin is new to it (Preferences, Plug-ins, VST, Re-scan).
2. Insert "OSCQuery Automation 32" on a track, open its editor; add `127.0.0.1:5005`, click WFS-DIY,
   bind an input's `positionX`, `positionY`, `positionZ` to slots 1–3 (click slot 1, then the leaf;
   the selection advances). Set each slot's Direction to `both` in the table.
3. The status bar's device line reads "... rx/s (LISTEN)" once the websocket is open; the slot rows
   show "rx <value>" beside the host value as soon as WFS-DIY says anything.
4. Show the three envelopes; set the track's automation mode to Touch; press play (or record);
   drag the input in WFS-DIY's map. The lanes write while it moves; each lane's gesture ends 200 ms
   after WFS-DIY's last message for it.
5. Set the track to Read, play from the start: the source follows the lanes in WFS-DIY, and the
   `guard` count in the status bar stays absent (zero).
6. For the other sources: run Chataigne, ossia score or go.dot with their OSCQuery servers on; add
   each by host:port; bind one float; move it there.
