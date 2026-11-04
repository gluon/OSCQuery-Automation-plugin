# tests/fixtures

Inputs the tests read, compared byte for byte on every platform (`.gitattributes` pins them to LF).
Addresses appear here and nowhere else: a test takes every path it uses from a fixture, never from
a string typed into its code (devplan, *How to read this*).

## trees/

Device trees in exactly the shape a device answers `GET /` with, served by the mock device in
`tests/mock-device/`.

| Fixture | What it is |
| --- | --- |
| `generic-device.json` | Synthetic, no real product's namespace. One of each case a client meets: nested containers; `f` with `RANGE`, `UNIT` and `CLIPMODE`; `i` with `MIN`/`MAX` and with `VALS`; a `T` toggle; an `fff` leaf with one `RANGE` entry per argument; an `f` with no `RANGE`; an `s` leaf (not a v1 bindable type); a read-only leaf unique per unit (an identity-leaf candidate); a write-only leaf with no `VALUE`; a `CRITICAL` leaf. |

Captures of real devices (WFS-DIY first) are added as their own fixtures when a phase needs them,
named after the device and its version.
