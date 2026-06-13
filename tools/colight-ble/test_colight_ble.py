from colight_ble import format_uuid_short


def test_format_uuid_short_extracts_16bit_form():
    assert format_uuid_short("0000ffe0-0000-1000-8000-00805f9b34fb") == "ffe0"


def test_format_uuid_short_is_case_insensitive():
    assert format_uuid_short("0000FFE0-0000-1000-8000-00805F9B34FB") == "ffe0"


def test_format_uuid_short_returns_custom_uuid_lowercased():
    custom = "12345678-1234-5678-1234-56789abcdef0"
    assert format_uuid_short(custom) == custom.lower()
