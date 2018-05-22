import sys
import time
import serial
import struct

SLIP_START = 0xAB
SLIP_END = 0xBC
SLIP_ESC = 0xCD
SLIP_ESC_START = 0xAC
SLIP_ESC_END = 0xBD
SLIP_ESC_ESC = 0xCE

PACKET_TYPE_EVENT_PACKET = 0x06
PACKET_TYPE_PING_RESP = 0x0E

class PacketBody:
    def __init__(self, data, packet_type):
        self.raw_payload = data

        if packet_type == PACKET_TYPE_EVENT_PACKET:
            self.header_len = data[0]

            if self.header_len != 10:
                raise ValueError("Unexpected packet header length: %d" % self.header_len)

            self.flags = data[1]
            self.channel = data[2]
            self.rssi = data[3]
            self.event_counter = data[4] + data[5] * 256
            self.time_diff = data[6] + data[7] * 256 + data[8] * 256 * 256 + data[9] * 256 * 256 * 256

            print(self.channel)

            self.access_address = data[10:14]
            self.payload_header = data[14]
            self.payload_length = data[15]

            if 10 + 4 + 1 + 1 + 1 + 3 + self.payload_length != len(data):
                raise ValueError("Unmatching payload length: %d" % self.payload_length)

            # self.payload = data[17:17+self.payload_length]
            # self.crc = data[-3:]
            self.payload = data[10:16] + data[17:]
        elif packet_type == PACKET_TYPE_PING_RESP:
            pass
        else:
            raise ValueError("Unsupported packet type: %d" % packet_type)

class PacketHeader:
    def __init__(self, data):
        self.proto_ver = data[2]
        self.packet_counter = data[3] + data[4] * 256
        self.packet_type = data[5]

        if self.proto_ver != 1:
            raise ValueError("Unexpected protocol version: %d" % self.proto_ver)

class Packet:
    def __init__(self, data):
        header_len = data[0]
        packet_len = data[1]

        if header_len != 6:
            raise ValueError("Unexpected packet header length: %d" % header_len)

        if header_len + packet_len != len(data):
            raise ValueError("Header/packet lengths do not match.")

        self.header = PacketHeader(data[:header_len])
        self.body = PacketBody(data[header_len:], self.header.packet_type)


def slip_decode(packet):
    assert(packet[0] == SLIP_START)
    assert(packet[-1] == SLIP_END)

    result = bytearray()
    cur = 1
    while cur < len(packet) - 1:
        if packet[cur] == SLIP_START or packet[cur] == SLIP_END:
            return None

        if packet[cur] == SLIP_ESC:
            if packet[cur + 1] == SLIP_ESC_START:
                result.append(SLIP_START)
            elif packet[cur + 1] == SLIP_ESC_END:
                result.append(SLIP_END)
            elif packet[cur + 1] == SLIP_ESC_ESC:
                result.append(SLIP_ESC)
            else:
                assert(False)

            cur += 2
        else:
            result.append(packet[cur])
            cur += 1

    return result

class PCAPWriter:
    def __init__(self, f):
        self.f = f
        self.seq = 0

        self.f.write(bytes([
            0xd4, 0xc3, 0xb2, 0xa1, 0x02, 0x00, 0x04, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x04, 0x00, 0xfb, 0x00, 0x00, 0x00,
        ]))

    def write_packet(self, packet):
        t = time.time()
        sec = int(t)
        usec = int((t - sec) * 1000000)

        self.f.write(struct.pack('<I', sec))
        self.f.write(struct.pack('<I', usec))
        self.f.write(struct.pack('<I', len(packet)))
        self.f.write(struct.pack('<I', len(packet)))

        self.f.write(packet)

def print_hex(arr):
    print(' '.join(map(lambda c: "%02x" % c, arr)))

def print_wireshark_hex_dump(arr):
    print('0000 ' + ' '.join(map(lambda c: "%02x" % c, arr)))

def process_packet(raw_packet):
    packet = slip_decode(raw_packet)

    if packet is None:
        sys.stderr.write('Discarding an invalid packet with length %d' % len(raw_packet))
        sys.stderr.write("\n")
        sys.stderr.flush()
        return None

    try:
        packet = Packet(packet)
    except ValueError as e:
        sys.stderr.write('Discarding an invalid packet with length %d: %s' % (len(raw_packet), str(e)))
        sys.stderr.write("\n")
        sys.stderr.flush()
        return None

    if packet.header.packet_type == PACKET_TYPE_EVENT_PACKET:
        return packet.body.payload
        # print_wireshark_hex_dump(packet.body.payload)

def process_buf(buf):
    payloads = []

    while True:
        try:
            start = buf.index(SLIP_START)
            end = buf[start:].index(SLIP_END) + start
        except ValueError:
            return buf, payloads

        payload = process_packet(buf[start:end+1])
        if payload is not None:
            payloads.append(payload)
        buf = buf[end+1:]

def main():
    buf = bytes()

    with serial.Serial('/dev/tty.usbserial-A9M9DV3R', 460800, timeout=1) as f:
        with open('/tmp/ble-out', 'wb') as of:
            pcap_writer = PCAPWriter(of)

            while True:
                block = f.read(256)
                # block = sys.stdin.buffer.read(1024)
                buf += block

                buf, payloads = process_buf(buf)
                for payload in payloads:
                    pcap_writer.write_packet(payload)
                    print('Captured 1 packet with length %d' % len(payload))
    #

    with open('/tmp/ble-out', 'wb') as f:
        pcap_writer.write_packet(bytes(10))

main()
