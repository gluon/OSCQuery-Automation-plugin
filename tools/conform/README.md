# tools/conform

The conformance CLI: `conform <host:port>` or `conform --browse` runs the Core (C1–C7) and Live
(L1–L5) checks of the profile in [`docs/spec/`](../../docs/spec/) and prints the device's matrix
row as Markdown and JSON, exiting non-zero on any Core failure.

Empty until devplan **Phase 8**. Its first subjects are the mock device in `tests/mock-device/`
and the plugin's own OSCQuery server.
