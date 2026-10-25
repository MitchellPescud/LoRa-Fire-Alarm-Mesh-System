#!/usr/bin/env python3
"""
Timestamped serial logger for Range_Test_v1.

  pip install pyserial
  python rt_logger.py COM5 log_sf9_neighbourhood.txt

Reads the fixed node's USB serial port (115200) and writes every line to the file
prefixed with a UTC timestamp, e.g.
  2026-10-24T08:00:23.567Z RX,123456,40123,17,100,3,2,-104,-2,9,125,10,18
so analyze_range.py can match bursts to the phone's GPS track. Lines are flushed
immediately, so a crash or unplugged cable loses nothing. Ctrl+C to stop.
Keep the laptop clock network-synced (and the phone's too); a few seconds of offset is fine.
Type a line + Enter to send it to the board (e.g. "noise", "go 100", "point 1").
"""
import sys
import threading
from datetime import datetime, timezone

try:
    import serial
except ImportError:
    sys.exit('pyserial missing: pip install pyserial')


def now():
    return datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3] + 'Z'


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    port, outpath = sys.argv[1], sys.argv[2]
    ser = serial.Serial(port, 115200, timeout=0.2)

    def sender():
        for line in sys.stdin:
            ser.write((line.strip() + '\n').encode())
    threading.Thread(target=sender, daemon=True).start()

    print(f'logging {port} -> {outpath}  (Ctrl+C to stop)')
    with open(outpath, 'a', encoding='utf-8', buffering=1) as out:
        out.write(f'{now()} # logger started\n')
        buf = b''
        try:
            while True:
                chunk = ser.read(256)
                if not chunk:
                    continue
                buf += chunk
                while b'\n' in buf:
                    raw, buf = buf.split(b'\n', 1)
                    text = raw.decode('utf-8', errors='replace').rstrip('\r')
                    line = f'{now()} {text}'
                    out.write(line + '\n')
                    print(line)
        except KeyboardInterrupt:
            pass
        out.write(f'{now()} # logger stopped\n')


if __name__ == '__main__':
    main()
