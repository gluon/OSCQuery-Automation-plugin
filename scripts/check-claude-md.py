#!/usr/bin/env python3
# This file is part of Go.dot — https://github.com/pob31/go.dot
#
# Copyright (C) 2026 Pierre-Olivier Boulant
#
# Go.dot is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. Go.dot is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# (LICENSE, at the repository root) for more details.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Origin: go.dot scripts/check-claude-md.py at 1c8aaa0a, adapted to this repo's two
# sections and to a PRD whose sections end at the next heading rather than a rule.
"""CLAUDE.md drift gate.

Produces: nothing. Exits 0 when each of CLAUDE.md's two PRD sections is
         identical to the PRD's, 1 with a unified diff when one has drifted.
Usage:   python3 scripts/check-claude-md.py
Needs:   python3 only. It runs in the `pins` CI job, which has no compiler.

CLAUDE.md says of itself that it holds the PRD's *Goals and non-goals* and
*Binding model* verbatim, that the PRD wins, and that it is fixed by copying,
never by editing. That sentence is worth nothing unless something checks it.

Both sides are extracted the same way, from the heading line to the next
`## ` heading or `---` rule, so a mismatch is always a real difference in the
text. The PRD is found by glob, so a rename does not touch this file.
"""

import difflib
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

HEADINGS = [
    "## Goals and non-goals",
    "## Binding model: slots, paths, scaling",
]


def extract_section(path: Path, heading: str) -> list[str] | None:
    """The block from `heading` up to (not including) the next heading or rule."""
    lines = path.read_text(encoding="utf-8").splitlines()
    try:
        start = lines.index(heading)
    except ValueError:
        return None
    body = [lines[start].rstrip()]
    for line in lines[start + 1:]:
        if line.rstrip() == "---" or line.startswith("## "):
            break
        body.append(line.rstrip())
    while body and not body[-1].strip():
        body.pop()
    return body


def find_prd(failures: list[str]) -> Path | None:
    matches = sorted((REPO_ROOT / "docs").glob("PRD*.md"))
    if not matches:
        failures.append("no docs/PRD*.md found - is the PRD still in docs/?")
        return None
    if len(matches) > 1:
        names = ", ".join(m.name for m in matches)
        failures.append(
            f"more than one PRD in docs/ ({names}).\n"
            "    With two present there is no single answer to what CLAUDE.md must match."
        )
        return None
    return matches[0]


def main() -> int:
    print(f"check-claude-md: {REPO_ROOT}")
    failures: list[str] = []

    prd = find_prd(failures)
    claude = REPO_ROOT / "CLAUDE.md"

    if prd is not None:
        print(f"  ok  PRD located: docs/{prd.name}")
        if not claude.is_file():
            failures.append("CLAUDE.md does not exist.")
        else:
            for heading in HEADINGS:
                want = extract_section(prd, heading)
                got = extract_section(claude, heading)
                if want is None:
                    failures.append(
                        f"docs/{prd.name} has no line reading exactly:\n        {heading}\n"
                        "    If the section was renamed on purpose, update HEADINGS here and re-copy."
                    )
                elif got is None:
                    failures.append(f"CLAUDE.md has no line reading exactly:\n        {heading}")
                elif want == got:
                    print(f"  ok  {heading[3:]}: identical ({len(want)} lines)")
                else:
                    diff = "\n".join(
                        "      " + ln
                        for ln in difflib.unified_diff(
                            want, got,
                            fromfile=f"docs/{prd.name} (authoritative)",
                            tofile="CLAUDE.md (stale)",
                            lineterm="",
                        )
                    )
                    failures.append(
                        f"CLAUDE.md's '{heading[3:]}' has drifted from the PRD.\n"
                        "    The PRD wins. Fix this by RE-COPYING the section, never by editing\n"
                        "    CLAUDE.md to taste.\n\n"
                        f"{diff}"
                    )

    if failures:
        print("\ncheck-claude-md: FAILED\n")
        for i, f in enumerate(failures, start=1):
            print(f"  {i}. {f}\n")
        return 1

    print("\ncheck-claude-md: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
