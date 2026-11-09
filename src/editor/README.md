# src/editor

The plugin editor: device browser, slot table, status bar, and the Monitor view. A client of
`src/core`; nothing reaches the editor before the core exposes it and a headless test drives it.

`EditorRoot` is a plain `juce::Component` with its size hints as constants (Phase 5b-0): the JUCE
shim wraps it in an `AudioProcessorEditor` (`shims/juce/EditorWindow`), the CLAP shim parents it
to the host's window. It compiles with `juce_gui_basics` only, inside whichever shim links it.

`SlotPresentation.h` holds what a slot's row says, as pure functions over the slot's standing (the
shade and the tooltip in the PRD's failure table's words), so `tests/core/PresentationTests.cpp`
asserts them headless and `SlotTable` only paints (Phase 6d-1).

First cut in devplan **Phase 3**, finished in **Phase 6**.
