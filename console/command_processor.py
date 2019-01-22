PRUNE_TIMEOUT = 30


class CommandProcessor:
    def __init__(self, nodes):
        self.nodes = nodes

    def process_command(self, command):
        if command.startswith("name "):
            self.process_name(command[5:])
        elif command.startswith("prune"):
            self.process_prune(command[5:])

    def process_name(self, command):
        addr, *rest = command.split()

        addr = int(addr, 16)
        name = ' '.join(rest)

        if addr not in self.nodes:
            print('Error: unknown address 0x{:04x}.'.format(addr))
            return

        self.nodes[addr]['name'] = name

        print('Set the name of node 0x{:04X} to "{}"\n'.format(addr, name))

    def process_prune(self, command):
        for addr in list(self.nodes.keys()):
            if self.nodes[addr]['last_seen'] <= time.time() - PRUNE_TIMEOUT:
                del(self.nodes[addr])
                print('Pruned node 0x{:04X}.'.format(addr))

        print()
