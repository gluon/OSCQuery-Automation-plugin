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
"""A ToscA or OSCar mapping file as this plugin's bindings (devplan Phase 5c).

The plugin's export file is its state chunk (version 1): the devices a session
refers to and the slots bound to their paths, addressed by host_param_id. A
ToscA (IRCAM, 2015) or OSCar (IRCAM, 2023) mapping declares one lane per
<parameter> with a name, a range and a scaling, the OSC address being built
from a prefix (ToscA: prefix, then the track's id, then the name) or given as a
pattern with $1 for the track's id (OSCar). This script writes those lanes as
slots of one device, which the user names along with its last known address;
the plugin then checks each path against the device's live tree, and a path
the device does not serve shows stale, which is the honest outcome of a
converted mapping.

Formats, from the ToscA user manual (v4) and the OSCar forum examples, no
formal schema having been published; the root element tells them apart:

    <tosca version="0.9" prefix="/track">
      <parameter index="1" name="azim" min="-180" max="180" scaling="lin"/>
    </tosca>

    <oscar>
      <parameter name="trim" pattern="/track/$1/trim" min="-12" max="12" default="0"/>
    </oscar>

Produces: a version-1 state chunk as JSON on stdout (or --output), deterministic
         (two-space indent, fixed key order, LF), so a fixture's expected output
         can be diffed in CI.
Usage:   python3 tools/convert-mapping.py <mapping.xml> --device NAME --host HOST --port PORT
                 [--instance N] [--direction both|send|receive] [--output FILE]
Needs:   Python 3.10+, standard library only.
"""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SCALINGS = {
    "lin": "linear", "linear": "linear",
    "log": "log", "logarithmic": "log",
    "exp": "exp", "exponential": "exp",
}


def warn(message: str) -> None:
    print(f"convert-mapping: {message}", file=sys.stderr)


def number(text: str | None, default: float, what: str) -> float:
    if text is None or text.strip() == "":
        return default

    try:
        return float(text.replace(",", "."))
    except ValueError:
        warn(f"{what}: not a number: {text!r}; using {default}")
        return default


def scaling_of(element: ET.Element, what: str) -> str:
    raw = element.get("scaling") or element.get("scale") or "lin"
    name = SCALINGS.get(raw.strip().lower())

    if name is None:
        warn(f"{what}: unknown scaling {raw!r}; using linear")
        return "linear"

    return name


def tosca_path(root: ET.Element, element: ET.Element, instance: int) -> str:
    prefix = (root.get("prefix") or "").strip().rstrip("/")
    name = (element.get("name") or "").strip().strip("/")
    return f"{prefix}/{instance}/{name}"


def oscar_path(element: ET.Element, instance: int) -> str:
    pattern = (element.get("pattern") or element.get("address") or "").strip()

    if not pattern:
        name = (element.get("name") or "").strip().strip("/")
        pattern = f"/{name}"

    return pattern.replace("$1", str(instance)).replace("$instance", str(instance))


def convert(tree: ET.ElementTree, device: str, host: str, port: int, instance: int, direction: str) -> dict:
    root = tree.getroot()
    kind = root.tag.lower()

    if kind not in ("tosca", "oscar"):
        raise SystemExit(f"convert-mapping: the root element is <{root.tag}>, neither <tosca> nor <oscar>")

    slots = []
    next_index = 0

    for element in root.iter("parameter"):
        name = (element.get("name") or "").strip()
        what = f"parameter {name!r}" if name else "a parameter without a name"

        index_text = element.get("index")

        if index_text is not None and index_text.strip().isdigit() and int(index_text) >= 1:
            host_param_id = int(index_text) - 1
        else:
            if kind == "tosca" and index_text is not None:
                warn(f"{what}: index {index_text!r} is not a positive integer; taking the next free slot")
            host_param_id = next_index

        next_index = host_param_id + 1

        if not name:
            warn(f"slot {host_param_id}: no name; skipped")
            continue

        path = tosca_path(root, element, instance) if kind == "tosca" else oscar_path(element, instance)

        if not path.startswith("/"):
            warn(f"{what}: the address {path!r} does not start with '/'; skipped")
            continue

        minimum = number(element.get("min"), 0.0, what)
        maximum = number(element.get("max"), 1.0, what)

        if minimum == maximum:
            warn(f"{what}: min equals max ({minimum}); the slot has no range until one is entered")
            range_value = None
        else:
            range_value = {"min": minimum, "max": maximum}

        slots.append({
            "host_param_id": host_param_id,
            "device_index": 0,
            "path": path,
            "arg_index": 0,
            "type": "f",
            "range": range_value,
            "scaling": scaling_of(element, what),
            "direction": direction,
            "display_name": name,
            "range_overridden": True,
            "unit": "",
            "critical": False,
        })

    seen: dict[int, str] = {}

    for slot in slots:
        if slot["host_param_id"] in seen:
            warn(f"slot {slot['host_param_id']} is declared twice ({seen[slot['host_param_id']]!r} and "
                 f"{slot['display_name']!r}); the plugin keeps the first it reads")
        else:
            seen[slot["host_param_id"]] = slot["display_name"]

    return {
        "version": 1,
        "devices": [{
            "name": device,
            "last_host": host,
            "last_port": port,
            "tree_hash": "",
            "honours_timetags": False,
            "offset_ms": 0.0,
        }],
        "slots": slots,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="A ToscA or OSCar mapping file as the plugin's bindings (a version-1 state chunk).")
    parser.add_argument("mapping", type=Path, help="the ToscA or OSCar XML file")
    parser.add_argument("--device", required=True, help="the device's NAME, as its HOST_INFO gives it")
    parser.add_argument("--host", required=True, help="the device's last known host")
    parser.add_argument("--port", required=True, type=int, help="the device's OSCQuery (HTTP) port")
    parser.add_argument("--instance", type=int, default=1, help="the track's id in the addresses ($1 / the ToscA id); default 1")
    parser.add_argument("--direction", choices=("both", "send", "receive"), default="both",
                        help="every slot's direction; ToscA and OSCar send and receive, so both by default")
    parser.add_argument("--output", type=Path, help="write here instead of stdout")
    args = parser.parse_args()

    try:
        tree = ET.parse(args.mapping)
    except (OSError, ET.ParseError) as error:
        raise SystemExit(f"convert-mapping: cannot read {args.mapping}: {error}")

    chunk = convert(tree, args.device, args.host, args.port, args.instance, args.direction)
    text = json.dumps(chunk, indent=2, ensure_ascii=False) + "\n"

    if args.output is not None:
        args.output.write_text(text, encoding="utf-8", newline="\n")
    else:
        sys.stdout.reconfigure(newline="\n")      # LF on every platform, as the fixtures are
        sys.stdout.write(text)

    return 0


if __name__ == "__main__":
    sys.exit(main())
