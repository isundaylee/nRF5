import time


FAULT_MAP = {
    0x0001: "Friendless"
}


def format_faults(data):
    if len(data['faults']) == 0:
        return 'Healthy'

    return ', '.join(map(lambda n: FAULT_MAP[n], data['faults']))


def format_last_seen(data):
    seconds = int(time.time() - data['last_seen'])

    if seconds < 11:
        return ""
    else:
        return "%d seconds ago" % seconds


def format_ttl(ttl):
    if ttl is None:
        return 'N/A'
    else:
        return "%.1f hops" % ttl


def format_rssi(rssi):
    if rssi is None:
        return 'N/A'
    else:
        return "%3.1f dB" % rssi


def format_onoff_status(status):
    if status is None:
        return 'N/A'
    else:
        return ('On' if status else '')


def format_battery(battery):
    if battery is None:
        return 'N/A'
    else:
        return "%.5f V" % battery


def format_percentage(ratio):
    return "%.1f %%" % (100.0 * ratio)


def render(nodes):
    result = ''

    for addr, data in nodes.items():
        result += (('-' * 110) + '\n')

        result += ("%-15s | %-30s %9s %10s %10s %7s | %-3s | %-20s\n" % (
            data['name'],
            format_faults(data),
            format_battery(data['battery']),
            format_ttl(data['avg_ttl']),
            format_rssi(data['avg_rssi']),
            '',
            format_onoff_status(data['onoff_status']),
            format_last_seen(data)))

        for ttl in sorted(data['avg_rssi_by_ttl'].keys(), reverse=True):
            result += ("%-15s | %-30s %9s %10s %10s %7s | %-3s | %-20s\n" %(
                    '',
                    '',
                    '',
                    format_ttl(ttl),
                    format_rssi(data['avg_rssi_by_ttl'][ttl]),
                    format_percentage(1.0 * data['msg_count_by_ttl'][ttl] / data['msg_count']),
                    '',
                    '',
                ))

    result += (('-' * 110) + '\n')

    return result
