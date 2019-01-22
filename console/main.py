import asyncio
import time
import textwrap
import serial_asyncio
import sys
import os
import pickle


OUTPUT_DIR = 'output'
OUTPUT_DASHBOARD_PATH = os.path.join(OUTPUT_DIR, 'dashboard')
OUTPUT_NODES_PATH = os.path.join(OUTPUT_DIR, 'nodes')
OUTPUT_TRANSCRIPT_PATH = os.path.join(OUTPUT_DIR, 'transcript')

PRUNE_TIMEOUT = 30

LEFT_MARGIN = 38

FAULT_MAP = {
    0x0001: "Friendless"
}

try:
    with open(OUTPUT_NODES_PATH, 'rb') as f:
        nodes = pickle.load(f)
except FileNotFoundError:
    nodes = {}

RSSI_AVG_ALPHA = 0.95
TTL_AVG_ALPHA = 0.95
BATTERY_AVG_ALPHA = 0.95


def add_node(addr):
    if addr in nodes:
        return

    nodes[addr] = {
        'last_seen': time.time(),
        'faults': [],
        'avg_rssi': None,
        'avg_ttl': None,
        'battery': None,
        'name': 'Node 0x{:04X}'.format(addr),
        'onoff_status': None}


def touch(addr):
    nodes[addr]['last_seen'] = time.time()


def update_faults(addr, faults):
    nodes[addr]['faults'] = list(map(
        lambda h: int(h, 16),
        textwrap.wrap(faults[1:-1], 2)))
    touch(addr)


def update_ttl(addr, ttl):
    if nodes[addr]['avg_ttl'] is None:
        nodes[addr]['avg_ttl'] = ttl
    else:
        nodes[addr]['avg_ttl'] = TTL_AVG_ALPHA * nodes[addr]['avg_ttl'] + \
            (1 - TTL_AVG_ALPHA) * ttl
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


def update_onoff(addr, onoff):
    nodes[addr]['onoff_status'] = onoff
    touch(addr)


def process(line):
    op, *params = line.split(' ')

    if op == 'health':
        addr, ttl, rssi, faults = params

        addr = int(addr)
        ttl = int(ttl)
        rssi = float(rssi)

        add_node(addr)
        update_faults(addr, faults)
        update_ttl(addr, ttl)
        update_rssi(addr, rssi)
    elif op == 'battery':
        addr, ttl, rssi, battery = params

        addr = int(addr)
        ttl = int(ttl)
        rssi = float(rssi)
        battery = (float(battery) * 6.0 * 0.6) / float(1 << 14)

        add_node(addr)
        update_ttl(addr, ttl)
        update_rssi(addr, rssi)
        update_battery(addr, battery)
    elif op == 'onoff':
        addr, ttl, rssi, onoff = params

        addr = int(addr)
        ttl = int(ttl)
        rssi = float(rssi)
        onoff = bool(int(onoff))

        add_node(addr)
        update_ttl(addr, ttl)
        update_rssi(addr, rssi)
        update_onoff(addr, onoff)
    else:
        raise RuntimeError("Unknown op: " + op)

    save_nodes()


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


def format_ttl(data):
    if data['avg_ttl'] is None:
        return 'N/A'
    else:
        return "%.1f hops" % data['avg_ttl']


def format_rssi(data):
    if data['avg_rssi'] is None:
        return 'N/A'
    else:
        return "%3.1f dB" % data['avg_rssi']


def format_onoff_status(data):
    if data['onoff_status'] is None:
        return 'N/A'
    else:
        return ('On' if data['onoff_status'] else '')


def format_battery(data):
    if data['battery'] is None:
        return 'N/A'
    else:
        return "%.5f V" % data['battery']


async def display():
    while True:
        with open(OUTPUT_DASHBOARD_PATH, 'w') as f:
            for addr, data in nodes.items():
                f.write("%-15s | %-30s %-10s %-10s %-9s | %-3s | %-20s\n" % (
                    data['name'],
                    format_faults(data),
                    format_ttl(data),
                    format_rssi(data),
                    format_battery(data),
                    format_onoff_status(data),
                    format_last_seen(data)))

        await asyncio.sleep(1.0)


class ConsoleSerial(asyncio.Protocol):
    def __init__(self, tx_queue, rx_queue):
        self.tx_queue = tx_queue
        self.rx_queue = rx_queue
        self.reply_pending = False

    def connection_made(self, transport):
        self.transport = transport
        self.buffer = b''

        asyncio.get_event_loop().create_task(self._process_outgoing())

    def data_received(self, data):
        self.buffer += data

        while True:
            found = self.buffer.find(b'\r\n')
            if found < 0:
                break

            message = self.buffer[:found].decode()

            with open(OUTPUT_TRANSCRIPT_PATH, 'a') as f:
                f.write('{} {}\n'.format(str(time.time()), message))

            if message[:4] == 'rep ':
                if self.reply_pending:
                    self.reply_pending = False
                    self.rx_queue.put_nowait(message[4:])
                else:
                    print('Received unexpected reply: "{}"'.format(
                        message[4:]))
            else:
                process(message)

            self.buffer = self.buffer[found + 2:]

    async def _process_outgoing(self):
        while True:
            request = await self.tx_queue.get()

            self.reply_pending = True
            self.transport.write((request + '\n').encode())


def save_nodes():
    with open(OUTPUT_NODES_PATH, 'wb') as f:
        pickle.dump(nodes, f)


def handle_name(request):
    addr, *rest = request.split()

    addr = int(addr, 16)
    name = ' '.join(rest)

    if addr not in nodes:
        print('Error: unknown address 0x{:04x}.'.format(addr))
        return

    nodes[addr]['name'] = name
    save_nodes()

    print('Set the name of node 0x{:04X} to "{}"\n'.format(addr, name))


def handle_prune(request):
    for addr in list(nodes.keys()):
        if nodes[addr]['last_seen'] <= time.time() - PRUNE_TIMEOUT:
            del(nodes[addr])
            print('Pruned node 0x{:04X}.'.format(addr))

    save_nodes()

    print()


async def interact(tx_queue, rx_queue):
    while True:
        sys.stdout.write('> ')
        sys.stdout.flush()

        request = await (asyncio.get_event_loop()
                                .run_in_executor(None, sys.stdin.readline))
        request = request[:-1]

        if request.startswith("name "):
            handle_name(request[5:])
            continue
        elif request.startswith("prune"):
            handle_prune(request[5:])
            continue

        await tx_queue.put('req ' + request)
        reply = await rx_queue.get()

        err, *rest = reply.split()
        err = int(err)

        if err == 0:
            print('Success: {}\n'.format(' '.join(rest)))
        else:
            print('Error {}: {}\n'.format(err, ' '.join(rest)))


async def main():
    os.makedirs(OUTPUT_DIR, 0o777, True)

    tx_queue = asyncio.Queue()
    rx_queue = asyncio.Queue()

    await asyncio.gather(
        serial_asyncio.create_serial_connection(
            loop,
            lambda: ConsoleSerial(tx_queue, rx_queue),
            '/dev/cu.usbserial-A9M9DV3R',
            baudrate=115200),
        display(),
        interact(tx_queue, rx_queue))


loop = asyncio.get_event_loop()
loop.run_until_complete(main())
loop.close()
