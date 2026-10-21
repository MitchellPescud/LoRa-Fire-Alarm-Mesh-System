# Independent check of the wire format using Python's hmac. Run: python3 golden.py <hex from test_proto FIRE line>
import hmac, hashlib, struct, sys
key = bytes(0xA0 + i for i in range(16))
body = bytes([0xF1, 0xA7, (1 << 4) | 1, 0, 1, 0]) + struct.pack('<I', 0x00010002) + struct.pack('<Hh', 7, -3)
tag = hmac.new(key, body, hashlib.sha256).digest()[:4]
pkt = bytearray(body + tag); pkt[3] = 5            # TTL is set after tagging (excluded from the MAC)
print(pkt.hex())
if len(sys.argv) > 1:
    print("MATCH" if sys.argv[1] == pkt.hex() else "MISMATCH")
