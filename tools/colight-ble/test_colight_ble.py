from colight_ble import (
    ScanResult,
    build_discover_report,
    describe_uuid,
    format_monitor_row,
    format_scan_table,
    format_uuid_short,
)


def test_format_uuid_short_extracts_16bit_form():
    assert format_uuid_short("0000ffe0-0000-1000-8000-00805f9b34fb") == "ffe0"


def test_format_uuid_short_is_case_insensitive():
    assert format_uuid_short("0000FFE0-0000-1000-8000-00805F9B34FB") == "ffe0"


def test_format_uuid_short_returns_custom_uuid_lowercased():
    custom = "12345678-1234-5678-1234-56789abcdef0"
    assert format_uuid_short(custom) == custom.lower()


def test_describe_uuid_recognizes_ffe1():
    assert describe_uuid("0000ffe1-0000-1000-8000-00805f9b34fb") is not None


def test_describe_uuid_recognizes_ffd9():
    assert describe_uuid("0000ffd9-0000-1000-8000-00805f9b34fb") is not None


def test_describe_uuid_recognizes_ffe0():
    assert describe_uuid("0000ffe0-0000-1000-8000-00805f9b34fb") is not None


def test_describe_uuid_recognizes_ffd5():
    assert describe_uuid("0000ffd5-0000-1000-8000-00805f9b34fb") is not None


def test_describe_uuid_returns_none_for_unknown():
    assert describe_uuid("0000abcd-0000-1000-8000-00805f9b34fb") is None


def test_format_scan_table_sorts_by_rssi_descending():
    results = [
        ScanResult(name="Weak", address="AA:AA:AA:AA:AA:AA", rssi=-80),
        ScanResult(name="Strong", address="BB:BB:BB:BB:BB:BB", rssi=-40),
    ]
    table = format_scan_table(results)
    assert table.index("Strong") < table.index("Weak")


def test_format_scan_table_shows_placeholder_for_unnamed_device():
    results = [ScanResult(name=None, address="CC:CC:CC:CC:CC:CC", rssi=-60)]
    table = format_scan_table(results)
    assert "(nimetön)" in table


def test_build_discover_report_annotates_candidate_uuids():
    services_info = [
        {
            "uuid": "0000ffe0-0000-1000-8000-00805f9b34fb",
            "characteristics": [
                {
                    "uuid": "0000ffe1-0000-1000-8000-00805f9b34fb",
                    "properties": ["write", "notify"],
                    "value_hex": None,
                },
                {
                    "uuid": "0000abcd-0000-1000-8000-00805f9b34fb",
                    "properties": ["read"],
                    "value_hex": "010203",
                },
            ],
        },
        {
            "uuid": "0000abcd-0000-1000-8000-00805f9b34fb",
            "characteristics": [],
        },
    ]
    report = build_discover_report(services_info, "AA:BB:CC:DD:EE:FF")

    assert report["device_address"] == "AA:BB:CC:DD:EE:FF"
    service = report["services"][0]
    assert service["short_uuid"] == "ffe0"
    assert service["note"] is not None

    char_ffe1, char_abcd = service["characteristics"]
    assert char_ffe1["short_uuid"] == "ffe1"
    assert char_ffe1["note"] is not None
    assert char_ffe1["properties"] == ["write", "notify"]

    assert char_abcd["short_uuid"] == "abcd"
    assert char_abcd["note"] is None
    assert char_abcd["value_hex"] == "010203"

    assert report["services"][1]["short_uuid"] == "abcd"
    assert report["services"][1]["note"] is None


def test_format_monitor_row_returns_csv_row():
    row = format_monitor_row("2026-06-14T12:00:00+00:00", "0000ffe1-0000-1000-8000-00805f9b34fb", b"\x01\x02")
    assert row == ["2026-06-14T12:00:00+00:00", "ffe1", "0102"]
