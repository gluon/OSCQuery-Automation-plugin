# src/core

The plugin's core (devplan Phase 3): slots, scaling, the state chunk, the device registry, the
send and record paths. Everything that matters lives here; the format shims call into it and the
editor is a client of it. No typed OSC address, ever: addresses come from the device. No JUCE GUI
module, no `juce::Timer`, no `MessageManager`: a headless test drives all of it.

Target `oqa::core` (static), namespace `oqa::core`, includes as `<core/...>`. It links
`oqa::oscquery`, nothing more. Tests in `tests/core/`, run by CTest as `core.C` and `core.fr_FR`.

## What is in it

| Unit | What it does | Phase |
| --- | --- | --- |
| `Scaling` | Host 0–1 to device units and back: linear, logarithmic (equal travel, equal ratio; needs a range that excludes zero, else linear and says so) and exponential (one decade over the travel, fine near min); every curve its own inverse to rounding; `noexcept`, allocation-free | 3a |
| `Slot`, `SlotTable` | The PRD's slot table as a struct: bound from a `tree::Node` and an argument index, never a typed address; `f`, `i`, `T`/`F` only; `RANGE` absent leaves the slot inactive until min and max are entered; an `i` with `VALS` spans them; a multi-argument node binds consecutive slots (`bindAll`, all or nothing) and `groupOf` finds them again for the one message the send path makes of them | 3a |

Coming: the state chunk (3b), the registry behind its C ABI and the shared core library, the
block bundler, the rate limiter, timetags (3c).
