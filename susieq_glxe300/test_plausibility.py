import os

def read_f(path, default=''):
    try: return open(path).read().strip()
    except: return default

def write_f(path, val):
    open(path, 'w').write(str(val))

def remove_f(path):
    try: os.remove(path)
    except: pass

ntfy_calls = []

def ntfy(msg, priority='default'):
    ntfy_calls.append((msg, priority))

IMPLAUSIBLE_STREAK_LIMIT = 5


def check_plausible(name, value, lo, hi):
    counter_file = f'/tmp/susieq_{name}_implausible_count'
    notified_file = f'/tmp/susieq_{name}_implausible_notified'
    if value is None or not (lo <= value <= hi):
        count = int(read_f(counter_file, '0')) + 1
        write_f(counter_file, count)
        if count >= IMPLAUSIBLE_STREAK_LIMIT and not os.path.exists(notified_file):
            ntfy(f'{name}-sensori antaa järjettömiä arvoja {count} kertaa peräkkäin '
                 f'(viim. {value}) — tarkista BLE-yhteys/anturi', 'high')
            write_f(notified_file, count)
        return False
    write_f(counter_file, 0)
    remove_f(notified_file)
    return True


def cleanup(name):
    remove_f(f'/tmp/susieq_{name}_implausible_count')
    remove_f(f'/tmp/susieq_{name}_implausible_notified')


def test_single_implausible_value_does_not_notify():
    cleanup('testbatt')
    ntfy_calls.clear()
    result = check_plausible('testbatt', -302.0, 8.0, 16.0)
    assert result is False
    assert ntfy_calls == []
    cleanup('testbatt')


def test_five_consecutive_implausible_values_trigger_notify():
    cleanup('testbatt')
    ntfy_calls.clear()
    for _ in range(4):
        check_plausible('testbatt', -302.0, 8.0, 16.0)
    assert ntfy_calls == [], "ei pitäisi hälyttää ennen 5. kertaa"
    check_plausible('testbatt', -302.0, 8.0, 16.0)
    assert len(ntfy_calls) == 1
    assert 'järjettömiä' in ntfy_calls[0][0]
    cleanup('testbatt')


def test_plausible_value_resets_counter():
    cleanup('testbatt')
    ntfy_calls.clear()
    for _ in range(4):
        check_plausible('testbatt', -302.0, 8.0, 16.0)
    result = check_plausible('testbatt', 12.7, 8.0, 16.0)
    assert result is True
    assert read_f('/tmp/susieq_testbatt_implausible_count', '0') == '0'
    cleanup('testbatt')


def test_notify_only_sent_once_per_failure_streak():
    cleanup('testbatt')
    ntfy_calls.clear()
    for _ in range(7):
        check_plausible('testbatt', -302.0, 8.0, 16.0)
    assert len(ntfy_calls) == 1, "ei pitäisi spämmätä jokaisella kierroksella 5. jälkeen"
    cleanup('testbatt')


def test_value_within_range_boundary_is_plausible():
    cleanup('testbatt')
    assert check_plausible('testbatt', 8.0, 8.0, 16.0) is True
    assert check_plausible('testbatt', 16.0, 8.0, 16.0) is True
    cleanup('testbatt')


if __name__ == '__main__':
    test_single_implausible_value_does_not_notify()
    test_five_consecutive_implausible_values_trigger_notify()
    test_plausible_value_resets_counter()
    test_notify_only_sent_once_per_failure_streak()
    test_value_within_range_boundary_is_plausible()
    print('OK: all plausibility tests passed')
