import asyncio
import time
import aiofiles
import textwrap


LEFT_MARGIN = 38

FAULT_MAP = {
    0x0001: "Friendless"
}

NODE_MAP = {
    0x000D: "LPN PCB",
    0x0005: "LPN Ant",
    0x0010: "Friend",
}

RSSI_AVG_ALPHA = 0.95


nodes = {}


def add_node(addr):
    if addr in nodes:
        return

    nodes[addr] = {
        'last_seen': time.time(),
        'faults': [],
        'avg_rssi': None}


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


async def update():
    async with aiofiles.open('/tmp/rtt.log', 'r') as f:
        while True:
            line = await f.readline()

            if len(line) == 0:
                await asyncio.sleep(0.1)
                continue

            found = line.find("Protocol: ")
            if found == -1:
                continue

            line = line[found+len("Protocol: "):-1]

            op, *params = line.split(' ')

            if op == 'health':
                addr, rssi, faults = params

                addr = int(addr)
                rssi = float(rssi)

                add_node(addr)
                update_faults(addr, faults)
                update_rssi(addr, rssi)
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


async def display():
    while True:
        for addr, data in nodes.items():
            print("%-15s | %-30s Avg %.1f dB | %-20s" % (
                node_name(addr),
                format_faults(data),
                data['avg_rssi'],
                format_last_seen(data)))

        print('=' * 80)

        await asyncio.sleep(1.0)


async def main():
    await asyncio.gather(
        update(),
        display())


loop = asyncio.get_event_loop()
loop.run_until_complete(main())
loop.close()
