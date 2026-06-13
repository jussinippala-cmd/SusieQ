from colight_ble import ScanResult, describe_uuid, format_scan_table, format_uuid_short


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
