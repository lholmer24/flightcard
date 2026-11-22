#!/usr/bin/env python3
"""Provision a Flightcard C firmware over USB CDC.

Usage:
    python3 tools/provision.py --port /dev/ttyACM0
    python3 tools/provision.py --port /dev/ttyACM0 --demo
    python3 tools/provision.py --port /dev/ttyACM0 --show

The password is read with getpass(), never echoed and never written to a
shell history or a file. The firmware stores it in its own flash config.
"""

from __future__ import annotations

import argparse
import getpass
import os
import select
import sys
import termios
import time


def open_serial(port: str) -> int:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0  # iflag
    attrs[1] = 0  # oflag
    attrs[2] = termios.CREAD | termios.CLOCAL | termios.CS8
    attrs[3] = 0  # lflag
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def drain(fd: int, seconds: float) -> str:
    out = bytearray()
    end = time.time() + seconds
    while time.time() < end:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        out.extend(chunk)
    return out.decode("utf-8", "replace")


def send(fd: int, line: str, settle: float = 0.25) -> None:
    os.write(fd, line.encode() + b"\n")
    time.sleep(settle)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--ssid")
    ap.add_argument("--lat", type=float)
    ap.add_argument("--lon", type=float)
    ap.add_argument("--radius", type=float)
    ap.add_argument("--ceiling", type=float)
    ap.add_argument("--poll", type=int)
    ap.add_argument("--tz")
    ap.add_argument("--palette", choices=("auto", "cabin", "amber"))
    ap.add_argument("--demo", action="store_true")
    ap.add_argument("--show", action="store_true",
                    help="print the current config and exit")
    args = ap.parse_args()

    try:
        fd = open_serial(args.port)
    except OSError as exc:
        print(f"cannot open {args.port}: {exc}", file=sys.stderr)
        return 1

    drain(fd, 0.5)

    if args.show:
        send(fd, "show")
        print(drain(fd, 2.0))
        os.close(fd)
        return 0

    ssid = args.ssid or input("WiFi SSID: ").strip()
    if not ssid:
        print("SSID is required", file=sys.stderr)
        return 1
    password = getpass.getpass("WiFi password: ")

    commands = [f"set ssid {ssid}", f"set password {password}"]
    if args.lat is not None and args.lon is not None:
        commands.append(f"set home {args.lat} {args.lon}")
    if args.radius is not None:
        commands.append(f"set radius {args.radius}")
    if args.ceiling is not None:
        commands.append(f"set ceiling {args.ceiling}")
    if args.poll is not None:
        commands.append(f"set poll {args.poll}")
    if args.tz:
        commands.append(f"set tz {args.tz}")
    if args.palette:
        commands.append(f"set palette {args.palette}")
    if args.demo:
        commands.append("set demo 1")
    commands += ["save", "show", "reboot"]

    for command in commands:
        send(fd, command)
        time.sleep(0.3)

    # The reboot command resets USB; read whatever is still buffered first.
    print(drain(fd, 3.0))
    os.close(fd)
    print("provisioned; device rebooting")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
