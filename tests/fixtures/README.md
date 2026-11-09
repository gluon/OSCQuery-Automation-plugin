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

## mappings/

ToscA and OSCar mapping files for `tools/convert-mapping.py` (Phase 5c), and what it writes from
each (`*.expected.json`, a version-1 state chunk), diffed in CI's `pins` job and loaded by
`tests/core/ImportTests.cpp`.

| Fixture | What it is |
| --- | --- |
| `tosca-example.xml` | The shape of the ToscA user manual's example: `<tosca prefix>` over `<parameter index name min max scaling>`, one index left out to show the gap kept. Written from the manual and forum, no schema having been published; a real file replaces it when one turns up. |
| `oscar-example.xml` | The shape of the IRCAM forum's OSCar examples: `<oscar>` over `<parameter name pattern min max default>`, `$1` the track's id. As above. |
