#!/usr/bin/env python3
# This file is part of the OSCQuery Automation Plugin —
# https://github.com/pob31/OSCQuery-Automation-plugin
#
# Copyright (C) 2026 Pierre-Olivier Boulant, Julien Bayle
#
# The OSCQuery Automation Plugin is free software: you can redistribute it
# and/or modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of the License,
# or (at your option) any later version. It is distributed in the hope that it
# will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
# Public License (LICENSE, at the repository root) for more details.
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Submodule pins against THIRD_PARTY_NOTICES.md.

Produces: nothing. Exits 0 when every submodule's gitlink is the commit that
         THIRD_PARTY_NOTICES.md names for it, 1 otherwise.
Usage:   python3 scripts/check-pins.py
Needs:   python3 and git. The nested asio pin is read from juce_simpleweb's own
         index, so ThirdParty/juce_simpleweb must be initialised (not recursively).

Why: a notices file states versions, and a stated version that nobody checks
drifts (go.dot's did, by two JUCE releases). Here every section that names a
`**Submodule**` path must also name `commit <sha>`, and that sha must be the
gitlink. Every submodule must have a section, and every section a submodule.
Submodule URLs must be HTTPS, so a keyless CI runner can clone them.
"""

import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
NOTICES = REPO_ROOT / "THIRD_PARTY_NOTICES.md"

SUBMODULE_RE = re.compile(r"\*\*Submodule\*\*:\s*`([^`]+)`")
COMMIT_RE = re.compile(r"commit\s+`([0-9a-f]{40})`")


def git(*args: str, cwd: Path = REPO_ROOT) -> str:
    return subprocess.run(["git", *args], cwd=cwd, check=True,
                          capture_output=True, text=True).stdout


def gitlink(path: str) -> str | None:
    """The commit recorded in the index for submodule `path`, or None."""
    parent = REPO_ROOT
    rel = path
    # A nested submodule is recorded in its parent submodule's index.
    for outer in ("ThirdParty/juce_simpleweb",):
        if path.startswith(outer + "/"):
            parent = REPO_ROOT / outer
            rel = path[len(outer) + 1:]
            if not (parent / ".git").exists():
                return None
    out = git("ls-files", "-s", "--", rel, cwd=parent).split()
    if len(out) >= 2 and out[0] == "160000":
        return out[1]
    return None


def notices_pins(failures: list[str]) -> dict[str, str]:
    pins: dict[str, str] = {}
    for section in NOTICES.read_text(encoding="utf-8").split("\n---\n"):
        sub = SUBMODULE_RE.search(section)
        if not sub:
            continue
        path = sub.group(1)
        commit = COMMIT_RE.search(section)
        if not commit:
            failures.append(f"THIRD_PARTY_NOTICES.md names submodule {path} but no `commit <sha>`")
            continue
        pins[path] = commit.group(1)
    return pins


def gitmodules() -> dict[str, str]:
    """path -> url for every submodule declared in .gitmodules."""
    out = git("config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.(path|url)$")
    paths: dict[str, str] = {}
    urls: dict[str, str] = {}
    for line in out.splitlines():
        key, value = line.split(" ", 1)
        name = key[len("submodule."):].rsplit(".", 1)[0]
        (paths if key.endswith(".path") else urls)[name] = value
    return {paths[n]: urls.get(n, "") for n in paths}


def main() -> int:
    print(f"check-pins: {REPO_ROOT}")
    failures: list[str] = []

    pins = notices_pins(failures)
    declared = gitmodules()
    declared["ThirdParty/juce_simpleweb/asio"] = "(nested)"

    for path, url in sorted(declared.items()):
        if url != "(nested)" and not url.startswith("https://"):
            failures.append(f"{path}: URL {url} is not HTTPS; a keyless CI runner cannot clone it")
        if path not in pins:
            failures.append(f"{path} has no section with a **Submodule** line in THIRD_PARTY_NOTICES.md")
            continue
        actual = gitlink(path)
        if actual is None:
            failures.append(f"{path}: no gitlink found (is its parent submodule initialised?)")
        elif actual != pins[path]:
            failures.append(f"{path}: gitlink is {actual}, THIRD_PARTY_NOTICES.md says {pins[path]}")
        else:
            print(f"  ok  {path} @ {actual[:12]}")

    for path in sorted(set(pins) - set(declared)):
        failures.append(f"THIRD_PARTY_NOTICES.md names {path}, which is not a submodule")

    if failures:
        print("\ncheck-pins: FAILED\n")
        for i, f in enumerate(failures, start=1):
            print(f"  {i}. {f}")
        return 1

    print("\ncheck-pins: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
