import asyncio

from status_processor import StatusProcessor
from command_processor import CommandProcessor, COMMAND_LIST
from checker import Checker
from request_transformers.basic import BasicRequestTransformer


class Processor:
    def __init__(self, protocol_tx_queue, protocol_rx_queue, checks):
        self.nodes = {}
        self.gateway = {
            'logs': [],
            'address_book_free_slots': None,
            'address_book_total_slots': None,
        }
        self.status_processor = StatusProcessor(self.nodes, self.gateway)
        self.command_processor = CommandProcessor(self.nodes)
        self.checker = Checker(checks)

        self.protocol_tx_queue = protocol_tx_queue
        self.protocol_rx_queue = protocol_rx_queue

        self.pending_reply = None

        self.request_transformers = {
            'ping': BasicRequestTransformer(),
            'id': BasicRequestTransformer(),
            'reset': BasicRequestTransformer(),
        }

    def start(self):
        asyncio.get_event_loop().create_task(self.process_protocol_rx())

    async def process_protocol_rx(self):
        while True:
            timestamp, is_replay, message = await self.protocol_rx_queue.get()

            if message.startswith('sta '):
                self.status_processor.process_status(timestamp, message[4:])

                if not is_replay:
                    self.checker.check(self.nodes)
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
            op, *rest = message.split()

            if op not in self.request_transformers:
                print('Unknown request: {}'.format(op))
                return

            transformer = self.request_transformers[op]

            await self.protocol_tx_queue.put(
                'req ' + transformer.transform_request(message))

            reply_queue = asyncio.Queue()
            self.pending_reply = reply_queue
            reply = await self.pending_reply.get()

            print(transformer.transform_reply(reply))
