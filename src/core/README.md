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
| `StateChunk` | The PRD's JSON, `version` 1: devices (name, last host and port, tree hash, identity leaf, timetag setting and offset) and bound slots (every PRD field, plus `unit` and `critical` from the tree). Unknown fields kept and written back at every level; a slot beyond the table or naming no device dropped with a warning; a reader per version ever shipped, a fixture each in `tests/state/` | 3b |
| `rt/RtCheck` | The audio-thread lipogram's regions and counters; a test executable's replaced `operator new` (`tests/support/RtAllocCounter.cpp`) reports to them, a plugin never has it; `OQA_AUDIO_THREAD` is `[[clang::nonblocking]]` under RTSan | 3c |
| `registry/ChangeRing` | Vyukov's bounded MPMC queue of twelve-byte slot changes from the audio threads to a device's network thread; full is a counted drop, never a wait | 3c |
| `send/TimeTags` | The wall clock to an NTP tag plus the per-device offset, wrapping at the end of era 0 as NTP does; the immediate tag | 3c |
| `send/RateLimiter` | Per bound path, default off: WFS-DIY's shape (latest value per key, compared with what was last sent) without its mutex or timer; an optional periodic resend | 3c |
| `registry/Registry` | One entry per device (`NAME` plus identity value), owning one `oscquery::Device`, one sender (`RawUdpSender`, or `TcpSender` when `OSC_TRANSPORT` says so) and one realtime network thread; instances hand over their chunk and get a plan (slot to group to device) behind one atomic pointer; the thread gathers a block's changes from every instance, makes one bundle per device, splits at the MTU, stamps the time tag, routes CRITICAL leaves on a UDP device to the websocket (L4); the optional direct send encodes and sends from the audio thread, default off | 3c |
| `registry/RegistryApi` | The C ABI over the registry: one exported table of functions, version 1, exact-match; the same table statically as `staticCoreApi()` for the fallback | 3c |
| `Instance` | One plugin instance's side: its chunk, `connect`/`disconnect` for the host's lifecycle, `processBlock` posting what moved and ending the block, bind/unbind/range/curve/direction/name for the editor, the Test button, the device's tree and the status as JSON; since Phase 5: `pointDevice`, `setDeviceIdentity`, `setDeviceTreeHash`, the Finder wrappers, and `saveState` writing a re-found or pointed address into the chunk | 3c, 5b |
| `registry/RegistryLoader`, `core-shared/` | The shared core library every plugin binary loads (exact-match ABI version, found beside the plugin by walking up from its own module) and the fallback to this binary's own core, said in the status | 3c |
| `receive/Gesture` | The host gesture a stream of received values is: opened by the first, closed after the idle timeout, one per slot; pure, on the caller's clock | 4c |
| `Identity` | An identity leaf's value as text, one spelling for the editor (from the tree's VALUE) and the registry (read without a tree): strings as they are, numbers without a trailing fraction | 5b |

Since Phase 4 the registry also records: every group's path LISTENed, incoming values scaled to 0–1
and queued per instance, the loop guard, one UDP receiver per process for devices without LISTEN.
Since Phase 5 it checks every bound slot against the tree (stale and unauthorised slots), confirms
a device by NAME and identity leaf before driving it, and re-finds a moved one over DNS-SD through
the resolver's Finder (see `registry/Registry.h`).

Measured on this machine (Debug, loopback, `tests/core/RegistryTests.cpp`): 25 blocks from two
instances arrive as 25 bundles of two messages; post to the mock's socket median 96 µs, p99
227 µs; 500 blocks allocate nothing on the audio path, with or without the direct send.
