# Third-Party Notices

The OSCQuery Automation Plugin incorporates the third-party code below. Its licences and copyright
notices are recorded here, once, per dependency.

Five submodules under `ThirdParty/` supply everything, plus one nested inside another (`asio`, under
`juce_simpleweb`). Nothing is vendored by this repository itself, and nothing is fetched at configure
time. Each section names its submodule path and the commit it is pinned at;
`scripts/check-pins.py` fails CI when a gitlink and this file disagree, so the versions written here
are checked, not remembered.

---

## JUCE Framework

- **Website**: https://juce.com
- **Submodule**: `ThirdParty/JUCE`
- **Version**: 9.0.5, commit `d60b7a78030661574a615b586d9325ba7a7fa7dd` (the tag itself; the same
  commit WFS-DIY pins)
- **Licence**: AGPLv3 or commercial JUCE Licence
- **Copyright**: Raw Material Software Limited

The JUCE modules are dual-licensed under the [AGPLv3](https://www.gnu.org/licenses/agpl-3.0.en.html)
and the commercial [JUCE End User Licence Agreement](https://juce.com/legal/juce-8-licence/). This
project uses JUCE under the AGPLv3, which is compatible with its own GPLv3. JUCE bundles further
dependencies with their own licences; see `ThirdParty/JUCE/LICENSE.md` for the list.

---

## VST3 SDK (inside JUCE)

- **Website**: https://github.com/steinbergmedia/vst3sdk
- **Version**: as bundled by JUCE 9.0.5, under
  `ThirdParty/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/`
- **Licence**: MIT
- **Copyright**: Steinberg Media Technologies GmbH

Read on 2026-11-04 from the bundled `LICENSE.txt`, which reads "MIT License, Copyright (c) 2025,
Steinberg Media Technologies GmbH". It reaches every VST3 binary this project builds. VST is a
trademark of Steinberg Media Technologies GmbH.

---

## juce_simpleweb

- **Website**: https://github.com/benkuper/juce_simpleweb (this project pins the fork
  https://github.com/pob31/juce_simpleweb, as go.dot does)
- **Submodule**: `ThirdParty/juce_simpleweb`
- **Version**: commit `b72ec947548654cdd0a7e72b71ab5826fe574847`
- **Licence**: GPLv3
- **Copyright**: Ben Kuper

A JUCE module wrapping Simple-Web-Server, serving HTTP and WebSocket on one port, as the OSCQuery
proposal expects. Compiled with `SIMPLEWEB_SECURE_SUPPORTED=0` (plain HTTP and WS, no TLS) and with
the module's declared OpenSSL link libraries cleared. The fork carries a fix that lets a server bound
to port 0 report the port it was given, which the test mock device relies on; WFS-DIY's vendored copy
predates it. Full text: `ThirdParty/juce_simpleweb/LICENSE`.

---

## Simple-Web-Server

- **Website**: https://gitlab.com/eidheim/Simple-Web-Server
- **Version**: vendored inside `ThirdParty/juce_simpleweb/` (`webserver/`, `websocket/`, `common/`)
- **Licence**: MIT
- **Copyright**: Ole Christian Eidheim

The HTTP and WebSocket implementation that juce_simpleweb wraps. The vendored copy carries no licence
file of its own; the attribution is from the upstream project.

---

## asio (standalone)

- **Website**: https://think-async.com/Asio/
- **Submodule**: `ThirdParty/juce_simpleweb/asio` (nested; the fork `benkuper/asio`)
- **Version**: commit `6caa38aa03246140d745f36207892713895d245e`
- **Licence**: Boost Software License 1.0
- **Copyright**: Christopher M. Kohlhoff

The networking layer under Simple-Web-Server, used without Boost. The licence is stated in the banner
of every header.

---

## SHA-1 implementation (in juce_simpleweb's WSCrypto)

- **Licence**: BSD 3-Clause
- **Copyright**: Micael Hildenborg

`ThirdParty/juce_simpleweb/common/WSCrypto.h` and `.cpp`, whose notice is reproduced in full at the
top of the header. The WebSocket handshake needs SHA-1 (RFC 6455); this is the implementation it
calls, which is how the build needs no OpenSSL.

---

## doctest

- **Website**: https://github.com/doctest/doctest
- **Submodule**: `ThirdParty/doctest`
- **Version**: 2.5.3, commit `2d0a9359a60c51affe2a9bebb1be1dca47868151`
- **Licence**: MIT
- **Copyright**: Viktor Kirilov

The unit-test framework. It is compiled into the test executables only and never reaches a plugin
binary. Full text: `ThirdParty/doctest/LICENSE.txt`.

---

## CLAP

- **Website**: https://github.com/free-audio/clap
- **Submodule**: `ThirdParty/clap`
- **Version**: 1.2.10, commit `195b42a004144fab0b3cf95e9c067187d15365b7`
- **Licence**: MIT
- **Copyright**: Alexandre Bique

The CLAP C API headers, compiled into the project's own CLAP shim (`src/shims/clap`, devplan
Phase 5b). Full text: `ThirdParty/clap/LICENSE`.

---

## clap-helpers

- **Website**: https://github.com/free-audio/clap-helpers
- **Submodule**: `ThirdParty/clap-helpers`
- **Version**: `main`, commit `a380c978f429103e08afc1d17e9efbf3e3c96ee2` (2026-10-18; the project
  publishes no releases)
- **Licence**: MIT
- **Copyright**: Alexandre Bique

C++ helpers over the CLAP API (the `Plugin` and `HostProxy` templates), compiled into the same
shim. Full text: `ThirdParty/clap-helpers/LICENSE`.

---

## Used in CI only, not distributed

- **pluginval** 1.0.4 (https://github.com/Tracktion/pluginval, GPLv3, Tracktion Corporation):
  downloaded from its release page by `.github/workflows/ci.yml` to validate the built plugins. It
  never reaches the repository or a binary.
- **clap-validator** 0.4.1 (https://github.com/free-audio/clap-validator, MIT, Robbert van der
  Helm): downloaded from its release page by `.github/workflows/ci.yml` and `scripts/farm-check.sh`
  to validate the built CLAP (devplan Phase 5b). It never reaches the repository or a binary.
