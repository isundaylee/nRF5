import asyncio
import time
import textwrap
import serial_asyncio
import sys
import os
import pickle


from display import render
from status_processor import StatusProcessor
from command_processor import CommandProcessor


OUTPUT_DIR = 'output'
OUTPUT_DASHBOARD_PATH = os.path.join(OUTPUT_DIR, 'dashboard')
OUTPUT_NODES_PATH = os.path.join(OUTPUT_DIR, 'nodes')
OUTPUT_TRANSCRIPT_PATH = os.path.join(OUTPUT_DIR, 'transcript')

LEFT_MARGIN = 38

REPLAY = True

status_processor = StatusProcessor()
command_processor = CommandProcessor(status_processor.nodes)


async def display():
    while True:
        with open(OUTPUT_DASHBOARD_PATH, 'w') as f:
            f.write(render(status_processor.nodes))

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

            if message.startswith('sta '):
                status_processor.process_status(message[4:])
            elif message.startswith('rep '):
                if self.reply_pending:
                    self.reply_pending = False
                    self.rx_queue.put_nowait(message[4:])
                else:
                    print('Received unexpected reply: "{}"'.format(
                        message[4:]))
            else:
                print('Received unexpected message: "{}"'.format(message))

            self.buffer = self.buffer[found + 2:]

    async def _process_outgoing(self):
        while True:
            request = await self.tx_queue.get()

            self.reply_pending = True
            self.transport.write((request + '\n').encode())


def save_nodes():
    with open(OUTPUT_NODES_PATH, 'wb') as f:
        pickle.dump(nodes, f)


async def interact(tx_queue, rx_queue):
    while True:
        sys.stdout.write('> ')
        sys.stdout.flush()

        request = await (asyncio.get_event_loop()
                                .run_in_executor(None, sys.stdin.readline))
        request = request[:-1]

        op = request.split()[0]
        if op in ('name', 'prune'):
            command_processor.process_command(request)
            continue

        await tx_queue.put('req ' + request)
        reply = await rx_queue.get()

        err, *rest = reply.split()
        err = int(err)

        if err == 0:
            print('Success: {}\n'.format(' '.join(rest)))
        else:
            print('Error {}: {}\n'.format(err, ' '.join(rest)))


def replay():
    if not os.path.exists(OUTPUT_TRANSCRIPT_PATH):
        return

    print("Replaying transcript...")

    with open(OUTPUT_TRANSCRIPT_PATH, 'r') as f:
        for line in f:
            timestamp, *rest = line.split()
            timestamp = float(timestamp)
            command = ' '.join(rest)

            if not command.startswith("sta "):
                continue

            status_processor.process_status(command[4:])

    print()


async def main():
    global nodes

    if REPLAY:
        nodes = {}
        replay()
    else:
        try:
            with open(OUTPUT_NODES_PATH, 'rb') as f:
                nodes = pickle.load(f)
        except FileNotFoundError:
            nodes = {}

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
