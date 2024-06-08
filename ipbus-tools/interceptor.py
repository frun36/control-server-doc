import socket

import ipbus_parser

UDP_IP = "127.0.0.1"
UDP_PORT = 50001

sock = socket.socket(socket.AF_INET, # Internet
                     socket.SOCK_DGRAM) # UDP
sock.bind((UDP_IP, UDP_PORT))

while True:
    data, addr = sock.recvfrom(1024) # buffer size is 1024 bytes
    print("Received packet")
    for i in range(0, len(data), 4):
        print(' '.join(format(x, '02x') for x in data[i:i+4]))
    sent = sock.sendto(data, addr)
    print("Header: " + str(ipbus_parser.Packet.from_le_bytes(data[0:4])))
    print("Sent %d bytes" % sent)

# First (status packet) is sent as big endian. Consecutive packets are little endian