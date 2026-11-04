# OSCQuery Automation Plugin

A DAW plugin that records and plays back automation curves to and from any OSC-controlled
device, with the device's address space discovered over OSCQuery instead of typed into a
mapping file. CLAP, VST3 and AU from one codebase. The reference client for an OSCQuery
discovery profile, with a conformance tool that prints a device's interoperability row.

**Status:** Phase 0 of the plan. The repository builds a plugin that does nothing yet (VST3 on
Windows, macOS and Linux, AU on macOS), validated by pluginval in CI, and a mock OSCQuery device
the tests drive. The product requirements, the phase plan and the profile draft are in
[`docs/`](docs/).

- [PRD](docs/PRD%20—%20OSCQuery%20Automation%20Plugin%20(CLAP%20VST3%20AU).md)
- [Development plan, draft 0.1](docs/Devplan%20—%20OSCQuery%20Automation%20Plugin%20(draft%200.1).md)
- [OSCQuery discovery profile, draft 0.1](docs/spec/core-profile-draft-0.1.md): the Core and Live
  profiles the plugin is the reference client for, out for review.
- [`CLAUDE.md`](CLAUDE.md): the review criterion for every change, and where to look.

## Building

CMake 3.22 or later, a C++20 compiler, and the submodules:

```
git clone --recurse-submodules https://github.com/pob31/OSCQuery-Automation-plugin.git
```

| Where | Configure, build, test |
| --- | --- |
| Windows, any shell | `cmake --preset vs`, `cmake --build --preset vs-debug`, `ctest --preset vs-debug` |
| macOS, Linux (Ninja) | `cmake --preset dev`, `cmake --build --preset dev-debug`, `ctest --preset dev-debug` |

The suite runs twice, under `C` and under the French locale, and fails if the French locale is
missing (`sudo locale-gen fr_FR.UTF-8` on Debian and Ubuntu). The plugin lands in
`build/<preset>/src/shims/juce/oqa_plugin_32_artefacts/<config>/`. On Linux, `.github/workflows/ci.yml`
lists the packages JUCE needs.

`tests/mock-device/` has a mock OSCQuery device you can run by hand:
`oqa-mock-device --fixture tests/fixtures/trees/generic-device.json`.

Before pushing: `python3 scripts/check-claude-md.py`, `check-pins.py` and `check-headers.py`, as
CI's first job runs them.

## Licence

GPLv3. Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle. See [LICENSE](LICENSE).

The profile text in [`docs/spec/`](docs/spec/) is licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) instead, so that manufacturers can quote it.
