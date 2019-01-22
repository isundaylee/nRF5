import time
import textwrap

RSSI_AVG_ALPHA = 0.95
TTL_AVG_ALPHA = 0.95
BATTERY_AVG_ALPHA = 0.95


class StatusProcessor:
    def __init__(self, nodes):
        self.nodes = nodes

    def process_status(self, timestamp, status):
        op, *params = status.split(' ')

        if op == 'health':
            addr, ttl, rssi, faults = params

            addr = int(addr)
            ttl = int(ttl)
            rssi = float(rssi)

            self.add_node(timestamp, addr)
            self.update_faults(timestamp, addr, faults)
            self.update_packet_metadata(timestamp, addr, ttl, rssi)
        elif op == 'battery':
            addr, ttl, rssi, battery = params

            addr = int(addr)
            ttl = int(ttl)
            rssi = float(rssi)
            battery = (float(battery) * 6.0 * 0.6) / float(1 << 14)

            self.add_node(timestamp, addr)
            self.update_packet_metadata(timestamp, addr, ttl, rssi)
            self.update_battery(timestamp, addr, battery)
        elif op == 'onoff':
            addr, ttl, rssi, onoff = params

            addr = int(addr)
            ttl = int(ttl)
            rssi = float(rssi)
            onoff = bool(int(onoff))

            self.add_node(timestamp, addr)
            self.update_packet_metadata(timestamp, addr, ttl, rssi)
            self.update_onoff(timestamp, addr, onoff)
        else:
            raise RuntimeError("Unknown op: " + op)

    def add_node(self, timestamp, addr):
        if addr in self.nodes:
            return

        self.nodes[addr] = {
            'last_seen': timestamp,
            'faults': [],
            'avg_rssi': None,
            'avg_ttl': None,
            'avg_rssi_by_ttl': {},
            'msg_count': 0,
            'msg_count_by_ttl': {},
            'battery': None,
            'name': 'Node 0x{:04X}'.format(addr),
            'onoff_status': None}

    def touch(self, timestamp, addr):
        self.nodes[addr]['last_seen'] = timestamp

    def update_faults(self, timestamp, addr, faults):
        self.nodes[addr]['faults'] = list(map(
            lambda h: int(h, 16),
            textwrap.wrap(faults[1:-1], 2)))
        self.touch(timestamp, addr)

    def update_packet_metadata(self, timestamp, addr, ttl, rssi):
        # Average TTL
        if self.nodes[addr]['avg_ttl'] is None:
            self.nodes[addr]['avg_ttl'] = ttl
        else:
            self.nodes[addr]['avg_ttl'] = TTL_AVG_ALPHA * \
                self.nodes[addr]['avg_ttl'] + (1 - TTL_AVG_ALPHA) * ttl

        # Average RSSI
        if self.nodes[addr]['avg_rssi'] is None:
            self.nodes[addr]['avg_rssi'] = rssi
        else:
            self.nodes[addr]['avg_rssi'] = RSSI_AVG_ALPHA * \
                self.nodes[addr]['avg_rssi'] + (1 - RSSI_AVG_ALPHA) * rssi

        # Average RSSI grouped by TTL
        if ttl not in self.nodes[addr]['avg_rssi_by_ttl']:
            self.nodes[addr]['avg_rssi_by_ttl'][ttl] = rssi
        else:
            self.nodes[addr]['avg_rssi_by_ttl'][ttl] = RSSI_AVG_ALPHA * \
                self.nodes[addr]['avg_rssi_by_ttl'][ttl] + \
                (1 - RSSI_AVG_ALPHA) * rssi

        # Message count
        self.nodes[addr]['msg_count'] += 1

        # Message count grouped by TTL
        if ttl not in self.nodes[addr]['msg_count_by_ttl']:
            self.nodes[addr]['msg_count_by_ttl'][ttl] = 1
        else:
            self.nodes[addr]['msg_count_by_ttl'][ttl] += 1

        self.touch(timestamp, addr)

    def update_battery(self, timestamp, addr, battery):
        if self.nodes[addr]['battery'] is None:
            self.nodes[addr]['battery'] = battery
        else:
            self.nodes[addr]['battery'] = BATTERY_AVG_ALPHA * \
                self.nodes[addr]['battery'] + (1 - BATTERY_AVG_ALPHA) * battery
        self.touch(timestamp, addr)

    def update_onoff(self, timestamp, addr, onoff):
        self.nodes[addr]['onoff_status'] = onoff
        self.touch(timestamp, addr)
