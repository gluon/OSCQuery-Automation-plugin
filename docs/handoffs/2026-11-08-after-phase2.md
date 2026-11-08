# Handoff, 2026-11-08: Phase 2 built, Phase 3 next

For the next Claude Code session on this repository, and for the author. Read this, then
`CLAUDE.md`, then the devplan's *How to read this*, its Phase 2 record, and Phase 3.

## Where things stand

- **Phase 2 is built** (2026-11-08, one session): `lib/oscquery/`, the client library, under test
  against the mock device: 90 cases, 31,830 assertions under C and fr-FR on Windows, as
  `oscquery.C` and `oscquery.fr_FR` in CTest. `lib/oscquery/README.md` lists every unit and where
  it came from. The example `oscquery-dump` is the done-when; the bench report is
  `docs/bench/2026-11-08-phase2-library.md`.
- **The done-when is met against WFS-DIY** (its OSC Query switched on by the author, who is at
  home: 172.22.1.11): the tree printed, a position subscribed, its changes printed as OSC moved it.
  **One bench row is still open:** DNS-SD browse found nothing on this Windows machine with go.dot
  announcing beside it; try it on the home network with go.dot serving on the Mac mini
  (`wfg serve …`), and `dns-sd -B _oscjson._tcp` there. Two shell lessons for the next bench:
  Git Bash rewrites `/wfs/...` arguments into Windows paths unless `MSYS_NO_PATHCONV=1`, and
  WFS-DIY does not push a change back to a websocket client at the same IP as the UDP sender.
- **CI is green on Linux, macOS and Windows** (run 42509175235), after two fixes the Windows
  build had not asked for: GCC and clang refuse a nested struct's default member initialisers in
  a default argument of the enclosing class, and libc++ has no floating-point `std::to_chars`
  below macOS 13.3, where this plugin's deployment target is 11.0 (go.dot's is 13.3), so
  `osc/Value.cpp` finds the shortest round-trip text by trying precisions there. CTest hides the
  suite's output on success, so the L5 timings on the runners are not in the logs; Windows's are
  in the bench report.
- **Next: Phase 3, slots, registry, send** (`src/core`, the shared core library after WFS-DIY's
  Bridge, the first editor). Size L. Start it in plan mode. It builds on `Device` (one per
  resolved device in the registry), `Finder` (the device list in the editor), `osc::bundle` (one
  bundle per device per block), `net::UdpEndpoint` and `net::TcpSender` (the send path),
  `Recents` (the per-user list), and `Tree::treeHash()` (the chunk's `tree_hash`).

## Decided this session (the author, 2026-11-08)

1. Library working name `oqa::oscquery`; renamed at Phase 8.
2. OSC from go.dot (`OscValue`, `OscAddress`, `OscCodec`, `UdpEndpoint`, `SocketBuffers`), not
   WFS-DIY's `OscTransport` plus `juce_osc`.
3. TCP sender now, SLIP default, length-prefix available.

The devplan's Phase 2 record holds the implementer's decisions (juce::JSON kept; unlisted
notifications handled when they arrive; the tree hash over every typed leaf; the callback
threads; what "lazy" means under OSCQuery).

## Open, and the author's to decide

1. Ad-hoc signing of Mac bundles (from Phase 1), unchanged.
2. The profile draft goes out for review (from Phase 0), unchanged. Two Phase 2 facts belong in
   the reviewers' questions: `EXTENSIONS` given as a list by some servers, and `PATH_CHANGED` sent
   unlisted by WFS-DIY.
3. For Phase 3: the default slot count to ship first; whether `$instance` lands there or in
   Phase 6 (the plan says 6). Both in the devplan under Phase 3.

## Lessons that cost time here

- **The Bash tool's transport turns backslash escapes inside heredocs into real characters**, even
  with a quoted delimiter: a `"\n"` inside a Python heredoc became a newline inside a C++ string
  literal once, and a replacement pattern with `'\0'` failed to match once. Scripts with a
  backslash in them are written with the Write tool and run by path.
- **`juce::var::isObject()` is true for arrays**, and `getDynamicObject()` is then null. Check
  `isArray()` first (`isJsonObject` in `HostInfo.cpp` and `Tree.cpp`).
- **MSVC's `std::mutex` throws `resource_deadlock_would_occur` on a recursive lock**, which is how
  a test holding its own mutex across `device.stop()` (whose `onState` fires on the caller's
  thread) failed with an exception rather than a hang.
- **A pointer into `device.tree()` dangles**: it returns a copy. Copy once, or use `withTree()`.
- **The mock forgets its port once stopped** (`httpPort()` returns 0): capture it first.
- **`doctest` `MESSAGE` cannot hold a `#ifdef`** in its argument; and streaming a `const char*`
  into it printed `1`: wrap it in `std::string`.
- **WFS-DIY and Chataigne cannot be driven from a shell**: WFS-DIY's OSCQuery is a UI toggle, and
  Chataigne opened no port in 25 s without a window. go.dot's `wfg serve <bundle> --sample-rate=N
  --buffer=N` is the one subject that runs headless.
- **A full Debug build of the library's tests** on this machine is a few minutes; relinking the
  test executable alone about one. `--target oqa_oscquery_tests` builds only what it needs.

## Working agreements

- Commit and push straight to `main` as often as useful (the author, 2026-11-08), with the
  attribution trailer; branches and pull requests once Julien Bayle contributes. Never rewrite
  pushed history.
- Before a push: `python scripts/check-claude-md.py`, `check-pins.py`, `check-headers.py`.
- The PRD wins over `CLAUDE.md`; its two sections are changed in the PRD and copied, never edited
  in place. `CLAUDE.md`'s *Code to reuse* was rewritten this session (not a PRD section).
