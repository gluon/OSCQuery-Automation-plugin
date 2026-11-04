# lib/oscquery

The OSCQuery client library: its own CMake target and tests, and no JUCE GUI dependency. It
reads any OSCQuery server headless: tree model, `HOST_INFO` with `EXTENSIONS`, subtree GET,
websocket `LISTEN`/`IGNORE` and the `PATH_*` notifications, DNS-SD browse and announce, OSC
encode/decode and the UDP and TCP senders, the recents store.

Empty until devplan **Phase 2**. It links JUCE's headers through `oqa::deps` (never a JUCE
module's sources), so it can go into the plugin and into the tests without JUCE compiling twice
in either. Phase 8 moves it to a repository of its own, with the conformance CLI as its first
consumer.
