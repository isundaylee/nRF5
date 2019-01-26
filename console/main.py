import asyncio
import time
import serial_asyncio
import sys
import os


from display import render
from processor import Processor
from command_processor import COMMAND_LIST
from checker import create_messenger_open_close_checks


TCP_DASHBOARD_PORT = 9798

OUTPUT_DIR = 'output'
OUTPUT_PROTOCOL_TRANSCRIPT_PATH = os.path.join(
    OUTPUT_DIR, 'protocol_transcript')
OUTPUT_CONSOLE_TRANSCRIPT_PATH = os.path.join(
    OUTPUT_DIR, 'console_transcript')

CHECKS = \
    create_messenger_open_close_checks(
        'Main', 'Main door', os.environ['FB_OWNER_ID']) + \
    create_messenger_open_close_checks(
        'Bathroom', 'Bathroom door', os.environ['FB_OWNER_ID'])


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

            timestamp = time.time()
            with open(OUTPUT_PROTOCOL_TRANSCRIPT_PATH, 'a') as f:
                f.write('{} {}\n'.format(str(timestamp), message))

            self.rx_queue.put_nowait((timestamp, False, message))

            self.buffer = self.buffer[found + 2:]

    async def _process_outgoing(self):
        while True:
            request = await self.tx_queue.get()
            self.transport.write((request + '\n').encode())


async def transcribe_and_process_console_message(processor, message):
    timestamp = time.time()
    with open(OUTPUT_CONSOLE_TRANSCRIPT_PATH, 'a') as f:
        f.write('{} {}\n'.format(str(timestamp), message))

    await processor.process_console_message(timestamp, message)


async def interact(processor):
    await transcribe_and_process_console_message(processor, 'session_reset')

    while True:
        sys.stdout.write('\n')
        sys.stdout.write('> ')
        sys.stdout.flush()

        message = await (asyncio.get_event_loop()
                                .run_in_executor(None, sys.stdin.readline))
        message = message[:-1]

        await transcribe_and_process_console_message(processor, message)


async def display(processor, dashboard_queues):
    while True:
        dashboard = render(processor.nodes, processor.gateway)

        for queue in dashboard_queues:
            queue.put_nowait(dashboard)

        await asyncio.sleep(1.0)


async def replay(processor):
    if not os.path.exists(OUTPUT_PROTOCOL_TRANSCRIPT_PATH):
        return

    print("Replaying transcript...")

    time_begin = time.time()
    entries = []

    with open(OUTPUT_PROTOCOL_TRANSCRIPT_PATH, 'r') as f:
        for line in f:
            str_timestamp, *rest = line.split()
            timestamp = float(str_timestamp)
            entries.append((timestamp, 'protocol',
                            line[len(str_timestamp)+1:-1]))

    with open(OUTPUT_CONSOLE_TRANSCRIPT_PATH, 'r') as f:
        for line in f:
            str_timestamp, *rest = line.split()
            timestamp = float(str_timestamp)
            entries.append((timestamp, 'console',
                            line[len(str_timestamp)+1:-1]))

    for timestamp, source, message in sorted(entries):
        if source == 'protocol':
            if message.startswith("sta "):
                await processor.protocol_rx_queue.put(
                    (timestamp, True, message))
                await processor.protocol_rx_queue.join()
        elif source == 'console':
            op = message.split()[0]
            if op in COMMAND_LIST:
                await processor.process_console_message(timestamp, message)

    print('Replayed {} entries in {:.1f} seconds.'.format(
        len(entries),
        time.time() - time_begin))


async def handle_tcp_dashboard(reader, writer, dashboard_queue):
    while True:
        dashboard = await dashboard_queue.get()
        writer.write(b"\x1b[2J\x1b[H")
        writer.write(dashboard.replace('\n', '\r\n').encode())

        try:
            await writer.drain()
        except (ConnectionResetError, BrokenPipeError):
            return


async def main():
    tx_queue = asyncio.Queue()
    rx_queue = asyncio.Queue()

    dashboard_queues = []

    processor = Processor(tx_queue, rx_queue, CHECKS)
    processor.start()

    await replay(processor)

    os.makedirs(OUTPUT_DIR, 0o777, True)

    async def tcp_dashboard_handler(reader, writer):
        dashboard_queue = asyncio.Queue()
        dashboard_queues.append(dashboard_queue)
        await handle_tcp_dashboard(reader, writer, dashboard_queue)
        dashboard_queues.remove(dashboard_queue)

    await asyncio.gather(
        asyncio.start_server(
            tcp_dashboard_handler,
            '0.0.0.0',
            TCP_DASHBOARD_PORT,
            loop=asyncio.get_event_loop()),
        serial_asyncio.create_serial_connection(
            loop,
            lambda: ConsoleSerial(tx_queue, rx_queue, processor),
            '/dev/cu.usbmodem401111',
            baudrate=115200),
        display(processor, dashboard_queues),
        interact(processor))


loop = asyncio.get_event_loop()
loop.run_until_complete(main())
loop.close()
