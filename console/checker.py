import aiohttp
import asyncio
import os


class MessengerSendMessageAction:
    def __init__(self, recipient_id, message):
        self.recipient_id = recipient_id
        self.message = message
        self.access_token = os.environ['FB_ACCESS_TOKEN']

    async def run(self):
        request_body = {
            'recipient': {
                'id': self.recipient_id,
            },
            'message': {
                'text': self.message,
            },
        }

        url = 'https://graph.facebook.com/v2.6/me/messages?access_token={}'. \
            format(self.access_token)

        async with aiohttp.ClientSession() as session:
            await session.post(url, json=request_body)


class OnOffServerStatusChangeDetector:
    def __init__(self, node_name, target_status):
        self.node_name = node_name
        self.target_status = target_status
        self.last_status = None

    def check(self, nodes):
        for _, data in nodes.items():
            if data['name'] != self.node_name:
                continue

            if data['onoff_status'] is None:
                return False

            if data['onoff_status'] == self.last_status:
                return False

            self.last_status = data['onoff_status']

            return (data['onoff_status'] == self.target_status)


def create_messenger_open_close_checks(node_name, thing_name, recipient_id):
    return [
        (
            OnOffServerStatusChangeDetector(node_name, True),
            MessengerSendMessageAction(
                recipient_id,
                '{} is opened.'.format(thing_name)
            ),
        ),
        (
            OnOffServerStatusChangeDetector(node_name, False),
            MessengerSendMessageAction(
                recipient_id,
                '{} is closed.'.format(thing_name)
            ),
        ),
    ]


class Checker:
    def __init__(self, checks):
        self.checks = checks

    def check(self, nodes):
        for check in self.checks:
            if check[0].check(nodes):
                asyncio.get_event_loop().create_task(check[1].run())
