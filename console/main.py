import asyncio
import time
import aiofiles
import textwrap
import serial_asyncio
import sys


LEFT_MARGIN = 38

FAULT_MAP = {
    0x0001: "Friendless"
}

NODE_MAP = {
    0x0004: "LPN PCB",
    0x0003: "LPN Ant",
    0x0002: "Friend",
}

RSSI_AVG_ALPHA = 0.95
BATTERY_AVG_ALPHA = 0.95

nodes = {}


def add_node(addr):
    if addr in nodes:
        return

    nodes[addr] = {
        'last_seen': time.time(),
        'faults': [],
        'avg_rssi': None,
        'battery': None}


def touch(addr):
    nodes[addr]['last_seen'] = time.time()


def update_faults(addr, faults):
    nodes[addr]['faults'] = list(map(
        lambda h: int(h, 16),
        textwrap.wrap(faults[1:-1], 2)))
    touch(addr)


def update_rssi(addr, rssi):
    if nodes[addr]['avg_rssi'] is None:
        nodes[addr]['avg_rssi'] = rssi
    else:
        nodes[addr]['avg_rssi'] = RSSI_AVG_ALPHA * nodes[addr]['avg_rssi'] + \
            (1 - RSSI_AVG_ALPHA) * rssi
    touch(addr)


def update_battery(addr, battery):
    if nodes[addr]['battery'] is None:
        nodes[addr]['battery'] = battery
    else:
        nodes[addr]['battery'] = BATTERY_AVG_ALPHA * nodes[addr]['battery'] + \
            (1 - BATTERY_AVG_ALPHA) * battery
    touch(addr)


def process(line):
    op, *params = line.split(' ')

    if op == 'health':
        addr, rssi, faults = params

        addr = int(addr)
        rssi = float(rssi)

        add_node(addr)
        update_faults(addr, faults)
        update_rssi(addr, rssi)
    elif op == 'battery':
        addr, rssi, battery = params

        addr = int(addr)
        rssi = float(rssi)
        battery = (float(battery) * 6.0 * 0.6) / float(1 << 14)

        add_node(addr)
        update_rssi(addr, rssi)
        update_battery(addr, battery)
    else:
        raise RuntimeError("Unknown op: " + op)


def node_name(addr):
    if addr in NODE_MAP:
        return NODE_MAP[addr]
    else:
        return "Node 0x%04x" % addr


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


def format_rssi(data):
    if data['avg_rssi'] is None:
        return 'N/A'
    else:
        return "%3.1f dB" % data['avg_rssi']


def format_battery(data):
    if data['battery'] is None:
        return 'N/A'
    else:
        return "%.5f V" % data['battery']


async def display():
    while True:
        with open('/tmp/bhome_console', 'w') as f:
            for addr, data in nodes.items():
                f.write("%-15s | %-30s %-10s %-9s | %-20s\n" % (
                    node_name(addr),
                    format_faults(data),
                    format_rssi(data),
                    format_battery(data),
                    format_last_seen(data)))

        await asyncio.sleep(1.0)


class ConsoleSerial(asyncio.Protocol):
    def connection_made(self, transport):
        self.transport = transport
        self.buffer = b''

    def data_received(self, data):
        self.buffer += data

        while True:
            found = self.buffer.find(b'\r\n')
            if found < 0:
                break

            process(self.buffer[:found].decode())
            self.buffer = self.buffer[found + 2:]


async def interact():
    while True:
        sys.stdout.write('> ')
        sys.stdout.flush()

        command = await (asyncio.get_event_loop()
                                .run_in_executor(None, sys.stdin.readline))
        command = command[:-1]

        print('Command: ' + command)


async def main():
    await asyncio.gather(
        serial_asyncio.create_serial_connection(loop,
                                                ConsoleSerial,
                                                '/dev/cu.usbserial-A9M9DV3R',
                                                baudrate=115200),
        display(),
        interact())


loop = asyncio.get_event_loop()
loop.run_until_complete(main())
loop.close()
