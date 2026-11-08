# Handoff, 2026-11-08: Phases 0 and 1 done, Phase 2 next

For the next Claude Code session on this repository, and for the author. Read this, then
`CLAUDE.md`, then the devplan's *How to read this*, its Phase 0 and Phase 1 records, and Phase 2.

## Where things stand

- **Phase 0 closed** (2026-11-04). The profile draft is in `docs/spec/core-profile-draft-0.1.md`
  (CC BY 4.0). The scaffold builds a do-nothing plugin (`src/shims/juce/`, 32-slot ID `Q032`,
  Pix et Bel / `PxBl`). CI in `.github/workflows/ci.yml` runs `pins` (three Python checks), then
  Linux, macOS and Windows: build, ctest under C and fr_FR, pluginval 5, unsigned artefacts.
  The mock OSCQuery device is in `tests/mock-device/`, with `MockDeviceTests.cpp` reading a
  fixture back.
- **Phase 1 closed** (2026-11-08). Report: `docs/spikes/phase1-formats.md`. The spike is deleted
  (code at commit `aaac2c4`). The PRD is amended the same day: AU never hides unbound slots
  (AUv2 has no `IsHidden`), and the format table's rename cells say what was measured.
- **Next: Phase 2, the client library** (`lib/oscquery/`, empty but for its README). It is size
  M. Start it in plan mode.

## Open, and the author's to decide

1. **Ad-hoc signing of Mac bundles:** a post-build `codesign --force --deep --sign -` for the plugin
   targets, in CI now or with the real plugin in Phase 3. Until then CI's macOS AU will not
   register on a Mac (Phase 1, finding 5).
2. **The profile draft goes out for review** to Holophonix and one ossia maintainer; the author
   sends it.
3. **The library's public name**, if it is to become its own repository (devplan Phase 2, *Needs
   from the author*; Phase 8 splits it out).

## For Phase 2: what already exists

- **Build targets** (`cmake/OqaThirdParty.cmake`): `oqa::deps` (JUCE headers and module
  definitions, no sources), `oqa_juce` (JUCE and `juce_simpleweb` compiled once, for non-plugin
  executables), `oqa::warnings`. The library links `oqa::deps` only, so it can go into the plugin
  (which compiles its own JUCE) and into the tests without JUCE compiling twice.
- **The mock device** serves a fixture tree: `?HOST_INFO` with a real bound UDP `OSC_PORT`, `GET /`,
  subtrees, attribute queries (200 / 204 / 400 / 404), `LISTEN`/`IGNORE` with binary OSC values,
  and `rename`/`remove`/`retype`/`add` announcing `PATH_RENAMED`/`REMOVED`/`CHANGED`/`ADDED` as its
  `Options::extensions` allow; `Options::websocket = false` for a device without one. It has no UDP
  receiver behind `OSC_PORT` yet. The `oqa-mock-device` executable serves a fixture by hand.
- **Test helpers** (`tests/support/`): a raw-socket HTTP GET (not `juce::URL`, which turns
  `?HOST_INFO` into `?HOST_INFO=`) and a websocket test client, both from go.dot.
- **Code to start from** (`CLAUDE.md`, *Code to reuse*): WFS-DIY's
  `Plugin/Source/Shared/OscQueryClient.{h,cpp}` (357 lines: raw-socket GET with a 3 s timeout,
  `SimpleWebSocketClient` started as `host:port` without `ws://`); go.dot's
  `src/wfg/engine/oscquery/Mdns.{h,cpp}` (standard library only) and `Discovery.{h,cpp}` (plus
  `MulticastJoin`, JUCE core; the only go.dot-specific detail is a hard-coded `"godot-"` host
  label), with their tests `tests/MdnsTests.cpp` and `tests/DiscoveryTests.cpp`. Copied files keep
  their header and gain an `Origin:` line; `scripts/check-headers.py` enforces it.
- **The spec draft** marks what is *(proposed)*; nothing proposed is implemented without a
  recorded yes (devplan, cross-cutting).

## Lessons that cost time here

- **Building on Windows:** cmake and ctest are not on `PATH`; use Visual Studio 2026's bundled copy,
  `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\`,
  with the `vs` presets. A full Debug build takes a few minutes.
- **CI:** pushing to `main` cancels the run still in progress. GitHub's macOS runners sometimes
  queue for ten minutes; `scripts/farm-check.sh` on the Mac mini answers the same question.
- **Farm machines:** `ssh macmini` and `ssh nuc` on the home network (172.22.x); from elsewhere
  (10.162.x on 2026-11-08) the Mac answered only as `ssh macmini-work`. Move unpushed work with a
  `git bundle` (clone it with `-b main`).
- **macOS:** bundles built with Ninja are only linker-signed, and the AU registrar ignores them
  until `codesign --force --deep --sign -`. `auval` over SSH talks to a different registrar from
  the logged-in session: run it in the session through a `.command` file opened with `open`.
  Launch GUI hosts with `open -n -a …` and poll for a result file; there is no `timeout`, and a
  stuck host cannot be killed from here.
- **Things the permission classifier refused:** killing processes on the farm, and building code
  fetched from outside the repository (clap-juce-extensions went ahead once the author agreed).
  Ask first.
- **Hosts update themselves:** REAPER went from 7.80 to 7.85 on the Mac within the phase. Every
  bench row carries the host's version and the date.
- **Text editing from the shell:** a Python heredoc can turn `\n` escapes into real newlines; use
  the editor tool or a script file. Python's `write_text` on Windows writes CRLF unless given
  `newline="\n"`; `.gitattributes` normalises to LF anyway. Some JUCE files are stored CRLF in
  JUCE's own repository, so a patch against them must carry CRLF.
- **Removing what a host may have loaded:** check which hosts are open first. The spike's bundles
  were deleted while REAPER and Bitwig were open on the Mac, and its cached entry then loaded as
  an empty placeholder.

## Working agreements

- Commit and push straight to `main` as often as useful (the author, 2026-11-08), with the
  attribution trailer; branches and pull requests once Julien Bayle contributes. Never rewrite
  pushed history.
- Before a push: `python scripts/check-claude-md.py`, `check-pins.py`, `check-headers.py`.
- The PRD wins over `CLAUDE.md`; its two sections are changed in the PRD and copied, never edited
  in place.
