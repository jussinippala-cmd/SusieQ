#!/usr/bin/env python3
"""CoLight 12/8 Gang Switch Panel -- BLE-protokollan selvitystyökalu.

Tausta ja käyttöprosessi:
docs/superpowers/specs/2026-06-14-colight-ble-selvitys-design.md
"""

import argparse
import asyncio
import csv
import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from bleak import BleakClient, BleakScanner

OUTPUT_DIR = Path(__file__).parent / "output"
SCAN_TIMEOUT = 10.0


async def cmd_scan(args: argparse.Namespace) -> None:
    raise NotImplementedError


async def cmd_discover(args: argparse.Namespace) -> None:
    raise NotImplementedError


async def cmd_monitor(args: argparse.Namespace) -> None:
    raise NotImplementedError


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="CoLight 12/8 Gang Switch Panel -- BLE-protokollan selvitystyökalu"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("scan", help="Skannaa lähistön BLE-laitteet")

    discover_parser = subparsers.add_parser(
        "discover", help="Listaa GATT-palvelut ja characteristicsit"
    )
    discover_parser.add_argument(
        "address", help="BLE-laitteen osoite (esim. AA:BB:CC:DD:EE:FF)"
    )

    monitor_parser = subparsers.add_parser(
        "monitor", help="Tilaa notify-characteristicsit ja lokita arvomuutokset"
    )
    monitor_parser.add_argument(
        "address", help="BLE-laitteen osoite (esim. AA:BB:CC:DD:EE:FF)"
    )

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    commands = {
        "scan": cmd_scan,
        "discover": cmd_discover,
        "monitor": cmd_monitor,
    }
    try:
        asyncio.run(commands[args.command](args))
    except KeyboardInterrupt:
        print("\nLopetetaan.")


if __name__ == "__main__":
    main()
