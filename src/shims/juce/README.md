# src/shims/juce

The JUCE shim: one `juce::AudioProcessor` that JUCE's own wrappers build as VST3 everywhere and
AU on macOS. Format-specific behaviour (VST3 `kParamTitlesChanged`, AU `IsHidden` and `clump`)
lives here too; where a format cannot do what the shim interface asks, the shim answers
*unsupported*. Thin: everything that matters is in `src/core`.

Phase 0 builds the 32-slot ID as a plugin that does nothing (audio through, no parameters), so
the plugin's identity is fixed before anything is saved against it. `CMakeLists.txt` records
that identity and why it never changes: Pix et Bel, manufacturer code `PxBl`, plugin codes
`Q008` … `Q128` and `QMon`.
