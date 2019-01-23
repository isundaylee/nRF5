PRUNE_TIMEOUT = 30


COMMAND_LIST = (
    'name',
    'prune',
    'session_reset',
)


class CommandProcessor:
    def __init__(self, nodes):
        self.nodes = nodes

    def process_command(self, timestamp, command):
        if command.startswith("name "):
            self.process_name(timestamp, command[5:])
        elif command.startswith("prune"):
            self.process_prune(timestamp, command[5:])
        elif command.startswith("session_reset"):
            self.process_session_reset(timestamp, command[13:])

    def process_name(self, timestamp, command):
        addr, *rest = command.split()

        addr = int(addr, 16)
        name = ' '.join(rest)

        if addr not in self.nodes:
            print('Error: unknown address 0x{:04x}.'.format(addr))
            return

        self.nodes[addr]['name'] = name

        print('Set the name of node 0x{:04X} to "{}"'.format(addr, name))

    def process_prune(self, timestamp, command):
        for addr in list(self.nodes.keys()):
            if self.nodes[addr]['last_seen'] <= timestamp - PRUNE_TIMEOUT:
                del(self.nodes[addr])
                print('Pruned node 0x{:04X}.'.format(addr))

    def process_session_reset(self, timestamp, command):
        for addr in self.nodes:
            self.nodes[addr]['last_health_status_seen'] = None
        print('Session state reset.')
