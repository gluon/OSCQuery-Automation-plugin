# Spike reports

One report per spike, `phaseN-<name>.md`, each with a verdict, the criterion it was judged
against (quoted from the PRD or the plan), what was built, and what was measured on which host
and date. The spike's code lives in `spikes/` only until its report is written.

| # | Spike | Verdict | Report |
| --- | --- | --- | --- |
| 1 | Formats shipped first: rename, hide, stale lanes in VST3 and AU | **PASS**, two PRD amendments proposed: VST3 renames follow in every host but REAPER; no host loses automation unless slots are hidden; AU has no hidden flag and hiding by the parameter list costs automation | [phase1-formats.md](phase1-formats.md) |
