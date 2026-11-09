# src/shims/clap

The project's own CLAP shim (devplan Phase 5b), written against the CLAP C API with
`clap-helpers` (both pinned in `ThirdParty/`). Neither `clap-juce-extensions` nor `clap-wrapper`
is used (decided 2026-11-04). It calls the same core as the JUCE shim; no core code moves for it.

One module, `OSCQuery Automation.clap`, whose factory lists the five slot-count variants
(`Descriptors.h`) under the ids `io.github.pob31.oqa.Q008` … `Q128`, fixed for good on
2026-11-09. `Entry.cpp` is `clap_entry` and the factory; `ClapPlugin` is one plugin over one
`core::Instance`: `connect()` in `init` and `activate`, `disconnect()` in `deactivate` and
`destroy`, audio through untouched, the state chunk over `clap_istream`/`clap_ostream` with the
parameters' values in it, a 50 ms host timer.

Built so far: **5b-1** (the module, the factory, lifecycle, audio, state); **5b-2** the params
extension (`ClapParams.cpp`: one 0–1 parameter per slot, `CLAP_PARAM_IS_HIDDEN` while unbound,
`module` from the device NAME and the path's container, text in device units, `rescan (INFO)` when
the bindings change, `clear()` by the rule of 2026-11-09), and `StatusExtension.h`, a vendor
extension answering the instance's status and the settings as JSON for the test host and for
diagnosis without the editor. **5b-3** record (`ClapRecord.cpp`): the values a device streams are
drained on the audio thread in `process` and `flush` into `PARAM_GESTURE_BEGIN`, `PARAM_VALUE` and
`PARAM_GESTURE_END` out-events, the gesture closed after `gesture_timeout_ms`; the registry's wake
asks the host to flush, and the 50 ms timer keeps asking while a gesture is open and the host is not
processing. To come: the JUCE editor in the host's window (5b-4), the loader reusing a core already
in the process (5b-5).

`tests/clap/` is a host of our own that loads the built module and drives it through the C API;
clap-validator runs on it in CI.
