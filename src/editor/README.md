# src/editor

The plugin editor: device browser, slot table, status bar, and the Monitor view. A client of
`src/core`; nothing reaches the editor before the core exposes it and a headless test drives it.

`EditorRoot` is a plain `juce::Component` with its size hints as constants (Phase 5b-0): the JUCE
shim wraps it in an `AudioProcessorEditor` (`shims/juce/EditorWindow`), the CLAP shim parents it
to the host's window. It compiles with `juce_gui_basics` only, inside whichever shim links it.

`SlotPresentation.h` holds what a slot's row says, as pure functions over the slot's standing (the
shade and the tooltip in the PRD's failure table's words), so `tests/core/PresentationTests.cpp`
asserts them headless and `SlotTable` only paints (Phase 6d-1).

Binding is click a slot then a leaf, drag a leaf onto a row (`EditorRoot` is the
`DragAndDropContainer`, `TreePane`'s items the sources, `SlotTable` the target), or type a path
into the Path cell, `$instance` allowed (`core/PathPattern.h`; the pattern stays on the slot and
follows the number by the title). *Monitor...* opens the registry's view in a window of its own
(Phase 6d-2).

The Monitor (Phase 6c, 6e) is `MonitorRoot` over `MonitorView`: the registry's devices with their
timing and address editable in place, every bound slot of every instance, the server line and the
per-user settings form. Its rows are `MonitorModel`, read from the process-wide status JSON with
juce_core only, so `tests/core/MonitorModelTests.cpp` asserts them from a fixture. The Monitor
plugin (`QMon`, zero slots) is this root as its editor; a slot plugin opens the same view (6d-2).

First cut in devplan **Phase 3**, finished in **Phase 6**.
