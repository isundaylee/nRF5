import asyncio

from status_processor import StatusProcessor
from command_processor import CommandProcessor, COMMAND_LIST


class Processor:
    def __init__(self, protocol_tx_queue, protocol_rx_queue):
        self.nodes = {}
        self.status_processor = StatusProcessor(self.nodes)
        self.command_processor = CommandProcessor(self.nodes)
        self.protocol_tx_queue = protocol_tx_queue
        self.protocol_rx_queue = protocol_rx_queue

        self.pending_reply = None

    def start(self):
        asyncio.get_event_loop().create_task(self.process_protocol_rx())

    async def process_protocol_rx(self):
        while True:
            timestamp, message = await self.protocol_rx_queue.get()

            if message.startswith('sta '):
                self.status_processor.process_status(timestamp, message[4:])
            elif message.startswith('rep '):
                reply = message[4:]

                if self.pending_reply is not None:
                    await self.pending_reply.put(reply)
                    self.pending_reply = None
                else:
                    print('Unexpected reply from protocol: {}'.format(reply))
            else:
                print('Unexpected message from protocol: {}'.format(message))

            self.protocol_rx_queue.task_done()

    async def process_console_message(self, timestamp, message):
        op, *rest = message.split()

        if op in COMMAND_LIST:
            self.command_processor.process_command(timestamp, message)
        else:
            await self.protocol_tx_queue.put('req ' + message)

            reply_queue = asyncio.Queue()
            self.pending_reply = reply_queue
            reply = await self.pending_reply.get()

            err, *rest = reply.split()
            err = int(err)

            if err == 0:
                print('Success: {}\n'.format(' '.join(rest)))
            else:
                print('Error {}: {}\n'.format(err, ' '.join(rest)))
