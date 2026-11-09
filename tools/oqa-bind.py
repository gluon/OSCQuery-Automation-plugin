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
"""Bind a slot of a running plugin over its own OSCQuery server (devplan Phase 6b-3).

The worked example of the PRD's headless use: no editor open, a script binds a slot
and the host's lane renames. Standard library only.

    oqa-bind.py HOST:PORT --list
    oqa-bind.py HOST:PORT --plugin ID --slot N --path /dev/path [--device NAME]
    oqa-bind.py HOST:PORT --plugin ID --slot N --unbind
    oqa-bind.py HOST:PORT --plugin ID --slot N --name "Lane name" | --scaling log | --direction both
                                                   | --min -60 | --max 12
    oqa-bind.py HOST:PORT --plugin ID --instance 7

HOST:PORT is the server's HTTP address (the Monitor shows it; `oscquery-dump --browse` finds
it by its NAME, "OSCQuery Automation @ <machine> (<host app>)"). The script reads
/?HOST_INFO for the OSC port and sends one OSC message over UDP; writes are accepted from
loopback by default, elsewhere when the plugin's `server_allow` setting says so.
--dry-run prints the datagram instead of sending it.

Produces: nothing on success (the lane renames in the host); the plugin's tree says what it
made of the write a moment later (--list again). Exits 1 when the server does not answer.
"""

import argparse
import http.client
import json
import socket
import struct
import sys


def host_info(host: str, port: int) -> dict:
    connection = http.client.HTTPConnection(host, port, timeout=3)
    connection.request("GET", "/?HOST_INFO")
    reply = connection.getresponse()
    if reply.status != 200:
        raise SystemExit(f"{host}:{port} answered {reply.status} to ?HOST_INFO")
    return json.loads(reply.read().decode("utf-8"))


def get_tree(host: str, port: int, path: str) -> dict:
    connection = http.client.HTTPConnection(host, port, timeout=3)
    connection.request("GET", path)
    reply = connection.getresponse()
    if reply.status != 200:
        raise SystemExit(f"{host}:{port} answered {reply.status} to GET {path}")
    return json.loads(reply.read().decode("utf-8"))


def osc_string(text: str) -> bytes:
    data = text.encode("utf-8") + b"\0"
    return data + b"\0" * (-len(data) % 4)


def osc_message(address: str, args: list) -> bytes:
    tags = ","
    payload = b""
    for arg in args:
        if isinstance(arg, bool):
            tags += "T" if arg else "F"
        elif isinstance(arg, int):
            tags += "i"
            payload += struct.pack(">i", arg)
        elif isinstance(arg, float):
            tags += "f"
            payload += struct.pack(">f", arg)
        else:
            tags += "s"
            payload += osc_string(str(arg))
    return osc_string(address) + osc_string(tags) + payload


def value_of(node: dict, leaf: str):
    contents = node.get("CONTENTS", {})
    values = contents.get(leaf, {}).get("VALUE", [])
    return values[0] if values else None


def list_plugins(host: str, port: int) -> None:
    plugins = get_tree(host, port, "/plugin").get("CONTENTS", {})
    for plugin_id in sorted(plugins, key=int):
        plugin = plugins[plugin_id]
        label = value_of(plugin, "label")
        number = value_of(plugin, "instance")
        active = "active" if value_of(plugin, "active") else "inactive"
        print(f"plugin {plugin_id}: {label}  ($instance = {number}, {active})")
        slots = plugin.get("CONTENTS", {}).get("slot", {}).get("CONTENTS", {})
        for n in sorted(slots, key=int):
            slot = slots[n]
            path = value_of(slot, "path")
            if not path:
                continue
            print(f"  slot {n:>3}: {path}  on {value_of(slot, 'device')}  [{value_of(slot, 'state')}]"
                  f"  {value_of(slot, 'display_name') or ''}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog=__doc__)
    parser.add_argument("server", help="HOST:PORT of the plugin's OSCQuery server")
    parser.add_argument("--list", action="store_true", help="print every instance and its bound slots")
    parser.add_argument("--plugin", type=int, help="the instance's id, from --list")
    parser.add_argument("--slot", type=int, help="the slot's index")
    parser.add_argument("--path", help="the device's OSC path to bind")
    parser.add_argument("--device", help="the device's NAME, when the instance has several or none yet")
    parser.add_argument("--unbind", action="store_true")
    parser.add_argument("--name", help="the lane's display name")
    parser.add_argument("--scaling", choices=["linear", "log", "exp"])
    parser.add_argument("--direction", choices=["send", "receive", "both"])
    parser.add_argument("--min", type=float, dest="range_min", help="the slot's range, device units")
    parser.add_argument("--max", type=float, dest="range_max")
    parser.add_argument("--instance", type=int, help="what $instance stands for in this instance's paths")
    parser.add_argument("--dry-run", action="store_true", help="print the datagram, send nothing")
    args = parser.parse_args()

    host, _, port_text = args.server.rpartition(":")
    if not host or not port_text.isdigit():
        parser.error("server must be HOST:PORT")
    port = int(port_text)

    if args.list:
        list_plugins(host, port)
        return 0

    if args.plugin is None:
        parser.error("--plugin ID is needed (see --list)")

    writes: list[tuple[str, list]] = []
    plugin_root = f"/plugin/{args.plugin}"

    if args.instance is not None:
        writes.append((f"{plugin_root}/instance", [args.instance]))

    if args.slot is not None:
        slot_root = f"{plugin_root}/slot/{args.slot}"
        if args.unbind:
            writes.append((f"{slot_root}/path", [""]))
        elif args.path:
            writes.append((f"{slot_root}/path", [args.path] + ([args.device] if args.device else [])))
        elif args.device:
            writes.append((f"{slot_root}/device", [args.device]))
        if args.name is not None:
            writes.append((f"{slot_root}/display_name", [args.name]))
        if args.scaling:
            writes.append((f"{slot_root}/scaling", [args.scaling]))
        if args.direction:
            writes.append((f"{slot_root}/direction", [args.direction]))
        if args.range_min is not None:
            writes.append((f"{slot_root}/range/min", [float(args.range_min)]))
        if args.range_max is not None:
            writes.append((f"{slot_root}/range/max", [float(args.range_max)]))

    if not writes:
        parser.error("nothing to write: give --path, --unbind, --device, --name, --scaling, --direction, --min, --max or --instance")

    info = host_info(host, port)
    osc_port = int(info.get("OSC_PORT", 0))
    if osc_port <= 0:
        raise SystemExit("the server names no OSC_PORT")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        for address, values in writes:
            datagram = osc_message(address, values)
            if args.dry_run:
                print(f"{host}:{osc_port}  {address} {values}  ({len(datagram)} bytes)")
            else:
                udp.sendto(datagram, (host, osc_port))

    return 0


if __name__ == "__main__":
    sys.exit(main())
