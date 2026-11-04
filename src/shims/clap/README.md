# src/shims/clap

The project's own CLAP shim, written against the CLAP C API with `clap-helpers` (both pinned in
`ThirdParty/`): a factory listing every slot-count variant, the params extension with
`CLAP_PARAM_IS_HIDDEN`, info rescans, `clear()` and `module`, and the JUCE editor in the host's
window. Neither `clap-juce-extensions` nor `clap-wrapper` is used (decided 2026-11-04).

Empty until devplan **Phase 5b**. It calls the same core as the JUCE shim; no core code moves for it.
