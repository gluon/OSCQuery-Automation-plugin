# Spike reports

One report per spike, `phaseN-<name>.md`, each with a verdict, the criterion it was judged
against (quoted from the PRD or the plan), what was built, and what was measured on which host
and date. The spike's code lives in `spikes/` only until its report is written.

| # | Spike | Verdict | Report |
| --- | --- | --- | --- |
| 1 | Formats shipped first: rename, hide, stale lanes in VST3 and AU | **In progress**: REAPER measured on both machines; AU has no IsHidden flag and hiding by the parameter list costs the automation in REAPER; hand-driven hosts pending | [phase1-formats.md](phase1-formats.md) |
