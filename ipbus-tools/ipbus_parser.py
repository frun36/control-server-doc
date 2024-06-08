class Packet:
    def __init__(self, header) -> None:
        self.header = header

    def __str__(self) -> str:
        return str(self.header)

    @classmethod
    def from_le_bytes(cls, bytes):
        header = PacketHeader.from_le_bytes(bytes[0:4])
        return cls(header)

class PacketHeader:
    def __init__(self, protocol_version, rsvd, packet_id, byte_order_qualifier, packet_type) -> None:
        self.protocol_version = protocol_version
        self.rsvd = rsvd
        self.packet_id = packet_id
        self.byte_order_qualifier = byte_order_qualifier
        self.packet_type = packet_type
    
    def __str__(self) -> str:
        return f"Protocol Version: 0x{self.protocol_version:02X} | " \
               f"RSVD: 0x{self.rsvd:02X} | " \
               f"Packet ID: 0x{self.packet_id:04X} | " \
               f"Byte Order Qualifier: 0x{self.byte_order_qualifier:02X} | " \
               f"Packet Type: 0x{self.packet_type:02X}"

    @classmethod
    def from_le_bytes(cls, bytes):
        packet_type = bytes[0] & 0x0f
        byte_order_qualifier = bytes[0] >> 4
        packet_id = bytes[1] + (bytes[2] << 8)
        rsvd = bytes[3] & 0x0f
        protocol_version = bytes[3] >> 4
        return cls(protocol_version, rsvd, packet_id, byte_order_qualifier, packet_type)