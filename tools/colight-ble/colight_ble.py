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


def format_uuid_short(uuid_str: str) -> str:
    """Palauttaa 16-bittisen lyhytmuodon (esim. 'ffe0') Bluetooth SIG:n
    base UUID -kaavalle 0000XXXX-0000-1000-8000-00805f9b34fb, muuten
    palauttaa koko UUID:n pienillä kirjaimilla."""
    normalized = uuid_str.lower()
    if normalized.startswith("0000") and normalized.endswith("-0000-1000-8000-00805f9b34fb"):
        return normalized[4:8]
    return normalized


CANDIDATE_UUIDS = {
    "ffe0": "HM-10-tyylinen serial-over-BLE -palvelu",
    "ffe1": "HM-10-tyylinen serial-over-BLE write/notify -characteristic",
    "ffd5": "Triones-tyylinen LED-ohjauspalvelu",
    "ffd9": "Triones-tyylinen LED-ohjaus write-characteristic (56 RR GG BB 00 F0 AA)",
}


def describe_uuid(uuid_str: str) -> str | None:
    """Palauttaa kuvauksen jos UUID vastaa tunnettua candidate-UUID:ta
    (ks. docs/superpowers/specs/2026-06-14-colight-ble-selvitys-design.md),
    muuten None."""
    return CANDIDATE_UUIDS.get(format_uuid_short(uuid_str))


@dataclass
class ScanResult:
    name: str | None
    address: str
    rssi: int


def format_scan_table(results: list[ScanResult]) -> str:
    """Muotoilee skannaustulokset taulukoksi, vahvimmasta signaalista
    heikoimpaan."""
    header = f"{'Nimi':<30} {'Osoite':<20} RSSI"
    rows = [header]
    for r in sorted(results, key=lambda x: x.rssi, reverse=True):
        name = r.name or "(nimetön)"
        rows.append(f"{name:<30} {r.address:<20} {r.rssi}")
    return "\n".join(rows)


def build_discover_report(services_info: list[dict], device_address: str) -> dict:
    """Muodostaa JSON-yhteensopivan raportin GATT-rakenteesta, lisäten
    candidate-UUID-huomautukset (ks. describe_uuid)."""
    report: dict = {"device_address": device_address, "services": []}
    for service in services_info:
        service_entry = {
            "uuid": service["uuid"],
            "short_uuid": format_uuid_short(service["uuid"]),
            "note": describe_uuid(service["uuid"]),
            "characteristics": [],
        }
        for char in service["characteristics"]:
            service_entry["characteristics"].append(
                {
                    "uuid": char["uuid"],
                    "short_uuid": format_uuid_short(char["uuid"]),
                    "note": describe_uuid(char["uuid"]),
                    "properties": char["properties"],
                    "value_hex": char.get("value_hex"),
                }
            )
        report["services"].append(service_entry)
    return report


def format_monitor_row(timestamp: str, uuid: str, data: bytes) -> list[str]:
    """Muotoilee yhden CSV-rivin notifikaatiolokiin."""
    return [timestamp, format_uuid_short(uuid), data.hex()]


async def cmd_scan(args: argparse.Namespace) -> None:
    print(f"Skannataan {SCAN_TIMEOUT:.0f} sekuntia...")
    devices = await BleakScanner.discover(timeout=SCAN_TIMEOUT, return_adv=True)
    results = [
        ScanResult(name=device.name, address=address, rssi=adv.rssi)
        for address, (device, adv) in devices.items()
    ]
    if not results:
        print("Ei löytynyt BLE-laitteita.")
        return
    print(format_scan_table(results))


async def cmd_discover(args: argparse.Namespace) -> None:
    OUTPUT_DIR.mkdir(exist_ok=True)
    print(f"Yhdistetään {args.address}...")

    async with BleakClient(args.address) as client:
        services_info = []
        for service in client.services:
            characteristics = []
            for char in service.characteristics:
                properties = list(char.properties)
                value_hex = None
                if "read" in properties:
                    try:
                        value = await client.read_gatt_char(char)
                        value_hex = value.hex()
                    except Exception as exc:
                        value_hex = f"<virhe: {exc}>"
                characteristics.append(
                    {"uuid": char.uuid, "properties": properties, "value_hex": value_hex}
                )
            services_info.append({"uuid": service.uuid, "characteristics": characteristics})

    report = build_discover_report(services_info, args.address)

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output_path = OUTPUT_DIR / f"discover_{timestamp}.json"
    output_path.write_text(json.dumps(report, indent=2, ensure_ascii=False))
    print(f"Tallennettu: {output_path}")

    for service in report["services"]:
        if service["note"]:
            print(f"  [HUOM] Palvelu {service['short_uuid']}: {service['note']}")
        for char in service["characteristics"]:
            if char["note"]:
                print(f"    [HUOM] Characteristic {char['short_uuid']}: {char['note']}")


async def cmd_monitor(args: argparse.Namespace) -> None:
    OUTPUT_DIR.mkdir(exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output_path = OUTPUT_DIR / f"monitor_{timestamp}.csv"
    print(f"Lokitiedosto: {output_path}")

    with open(output_path, "w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(["timestamp", "characteristic", "value_hex"])

        def handler(characteristic, data: bytearray) -> None:
            row = format_monitor_row(
                datetime.now(timezone.utc).isoformat(), characteristic.uuid, bytes(data)
            )
            writer.writerow(row)
            csv_file.flush()
            print(" ".join(row))

        while True:
            disconnected = asyncio.Event()

            async with BleakClient(
                args.address, disconnected_callback=lambda _c: disconnected.set()
            ) as client:
                subscribed = 0
                for service in client.services:
                    for char in service.characteristics:
                        if "notify" in char.properties or "indicate" in char.properties:
                            try:
                                await client.start_notify(char, handler)
                                subscribed += 1
                            except Exception as exc:
                                print(
                                    f"  [varoitus] start_notify epäonnistui "
                                    f"{format_uuid_short(char.uuid)}: {exc}"
                                )
                print(
                    f"Tilattu {subscribed} characteristicsia. "
                    "Kuunnellaan (Ctrl+C lopettaa)..."
                )
                await disconnected.wait()

            print("Yhteys katkesi, yhdistetään uudelleen 5 s kuluttua...")
            await asyncio.sleep(5)


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
