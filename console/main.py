import asyncio
import time
import textwrap
import serial_asyncio
import sys
import os
import pickle


from display import render
from processor import Processor
from command_processor import COMMAND_LIST


OUTPUT_DIR = 'output'
OUTPUT_DASHBOARD_PATH = os.path.join(OUTPUT_DIR, 'dashboard')
OUTPUT_NODES_PATH = os.path.join(OUTPUT_DIR, 'nodes')
OUTPUT_PROTOCOL_TRANSCRIPT_PATH = os.path.join(
    OUTPUT_DIR, 'protocol_transcript')
OUTPUT_CONSOLE_TRANSCRIPT_PATH = os.path.join(
    OUTPUT_DIR, 'console_transcript')

LEFT_MARGIN = 38

REPLAY = True


class ConsoleSerial(asyncio.Protocol):
    def __init__(self, tx_queue, rx_queue, processor):
        self.tx_queue = tx_queue
        self.rx_queue = rx_queue
        self.reply_pending = False
        self.processor = processor

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

            with open(OUTPUT_PROTOCOL_TRANSCRIPT_PATH, 'a') as f:
                f.write('{} {}\n'.format(str(time.time()), message))

            self.rx_queue.put_nowait(message)

            self.buffer = self.buffer[found + 2:]

    async def _process_outgoing(self):
        while True:
            request = await self.tx_queue.get()
            self.transport.write((request + '\n').encode())


async def interact(processor):
    while True:
        sys.stdout.write('> ')
        sys.stdout.flush()

        message = await (asyncio.get_event_loop()
                                .run_in_executor(None, sys.stdin.readline))
        message = message[:-1]

        with open(OUTPUT_CONSOLE_TRANSCRIPT_PATH, 'a') as f:
            f.write('{} {}\n'.format(str(time.time()), message))

        await processor.process_console_message(message)

        print()


async def display(processor):
    while True:
        with open(OUTPUT_DASHBOARD_PATH, 'w') as f:
            f.write(render(processor.nodes))

        await asyncio.sleep(1.0)


async def replay(processor):
    if not os.path.exists(OUTPUT_PROTOCOL_TRANSCRIPT_PATH):
        return

    print("Replaying transcript...")

    time_begin = time.time()
    entries = []

    with open(OUTPUT_PROTOCOL_TRANSCRIPT_PATH, 'r') as f:
        for line in f:
            timestamp, *rest = line.split()
            timestamp = float(timestamp)
            message = ' '.join(rest)
            entries.append((timestamp, 'protocol', message))

    with open(OUTPUT_CONSOLE_TRANSCRIPT_PATH, 'r') as f:
        for line in f:
            timestamp, *rest = line.split()
            timestamp = float(timestamp)
            message = ' '.join(rest)
            entries.append((timestamp, 'console', message))

    for timestamp, source, message in sorted(entries):
        if source == 'protocol':
            if message.startswith("sta "):
                await processor.protocol_rx_queue.put(message)
                await processor.protocol_rx_queue.join()
        elif source == 'console':
            await processor.process_console_message(message)

    print('Replayed {} entries in {:.1f} seconds.\n'.format(
        len(entries),
        time.time() - time_begin))


async def main():
    tx_queue = asyncio.Queue()
    rx_queue = asyncio.Queue()

    processor = Processor(tx_queue, rx_queue)
    processor.start()

    if REPLAY:
        await replay(processor)

    os.makedirs(OUTPUT_DIR, 0o777, True)

    await asyncio.gather(
        serial_asyncio.create_serial_connection(
            loop,
            lambda: ConsoleSerial(tx_queue, rx_queue, processor),
            '/dev/cu.usbserial-A9M9DV3R',
            baudrate=115200),
        display(processor),
        interact(processor))


loop = asyncio.get_event_loop()
loop.run_until_complete(main())
loop.close()
