# OSCQuery Automation Plugin

A DAW plugin that records and plays back automation curves to and from any OSC-controlled
device, with the device's address space discovered over OSCQuery instead of typed into a
mapping file. CLAP, VST3 and AU from one codebase. The reference client for an OSCQuery
discovery profile, with a conformance tool that prints a device's interoperability row.

**Status:** planning. Nothing builds yet. The product requirements and the phase plan are in
[`docs/`](docs/); the plan's Phase 0 is next.

- [PRD](docs/PRD%20—%20OSCQuery%20Automation%20Plugin%20(CLAP%20VST3%20AU).md)
- [Development plan, draft 0.1](docs/Devplan%20—%20OSCQuery%20Automation%20Plugin%20(draft%200.1).md)
- [OSCQuery discovery profile, draft 0.1](docs/spec/core-profile-draft-0.1.md): the Core and Live
  profiles the plugin is the reference client for, out for review.
- [`CLAUDE.md`](CLAUDE.md): the review criterion for every change, and where to look.

## Licence

GPLv3. Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle. See [LICENSE](LICENSE).

The profile text in [`docs/spec/`](docs/spec/) is licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) instead, so that manufacturers can quote it.
