#!/usr/bin/env python3
"""
Multi-node timestamped serial logger for the mesh test build.

  pip install pyserial
  python mesh_logger.py run1_relay n1=COM5 n100=COM6 n2=COM7

Opens every listed port (115200), stamps each line with a UTC time taken from ONE clock
on this PC, and writes a single merged log  run1_relay.log  (one line per node line):

  2026-10-26T08:00:23.567Z n100 RX,123456,HB,1,65537,2,1,1,-71,8,NEW,-

The name before '=' is only a label (n<node id> is a good habit). analyze_mesh.py reads the
real node id from each node's CFG line.

Type commands in this window to send them to the boards:
  n100: test ping 1 20 3       send to one node (label before the colon)
  all: block none              send to every node
  block none                   no prefix = send to every node (not allowed for 'test ...')
Ctrl+C stops. On start the logger sends 'cfg' to every node, and on stop 'stats', so the
log always contains each node's settings and final counters.

Timestamps use time.perf_counter() anchored to the wall clock at start, so they are
monotonic and millisecond-accurate (plain datetime.now() on Windows can be 15 ms coarse).
The USB serial delay (~1-5 ms) is the accuracy limit when comparing nodes.
"""
import sys
import time
import threading
from datetime import datetime, timezone

try:
    import serial
except ImportError:
    sys.exit('pyserial missing: pip install pyserial')

_wall0 = time.time()
_perf0 = time.perf_counter()


def now():
    t = _wall0 + (time.perf_counter() - _perf0)
    return datetime.fromtimestamp(t, timezone.utc).strftime('%Y-%m-%dT%H:%M:%S.%f')[:-3] + 'Z'


def main():
    if len(sys.argv) < 3 or any('=' not in a for a in sys.argv[2:]):
        sys.exit(__doc__)
    prefix = sys.argv[1]
    nodes = {}
    for a in sys.argv[2:]:
        label, port = a.split('=', 1)
        try:
            nodes[label] = serial.Serial(port, 115200, timeout=0.05)
        except Exception as e:  # noqa: BLE001
            sys.exit(f'cannot open {port} for {label}: {e}')

    lock = threading.Lock()
    stop = threading.Event()
    out = open(prefix + '.log', 'a', encoding='utf-8', buffering=1)

    def write(label, text):
        line = f'{now()} {label} {text}'
        with lock:
            out.write(line + '\n')
            if '[DUST]' not in text:   # still saved to the file, just not echoed
                print(line)

    def reader(label, ser):
        buf = b''
        while not stop.is_set():
            try:
                chunk = ser.read(512)
            except Exception as e:  # noqa: BLE001
                write(label, f'# serial error: {e}')
                return
            if not chunk:
                continue
            buf += chunk
            while b'\n' in buf:
                raw, buf = buf.split(b'\n', 1)
                write(label, raw.decode('utf-8', errors='replace').rstrip('\r'))

    def send(label, text):
        targets = list(nodes) if label in ('all', '*') else [label]
        for t in targets:
            if t not in nodes:
                print(f'unknown node label {t!r} (have: {", ".join(nodes)})')
                continue
            write(t, f'# > {text}')
            nodes[t].write((text + '\n').encode())

    write('logger', '# started: ' + ' '.join(f'{k}={v.port}' for k, v in nodes.items()))
    threads = [threading.Thread(target=reader, args=(k, v), daemon=True) for k, v in nodes.items()]
    for t in threads:
        t.start()
    time.sleep(0.5)
    send('all', 'cfg')

    def console():
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            if ':' in line.split(' ')[0]:
                label, cmd = line.split(':', 1)
                send(label.strip(), cmd.strip())
            elif line.startswith('test'):
                print('a test must be started on ONE node: n100: ' + line)
            else:
                send('all', line)
    threading.Thread(target=console, daemon=True).start()

    print(f'logging {len(nodes)} node(s) -> {prefix}.log   (Ctrl+C to stop)')
    try:
        while True:
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    send('all', 'stats')
    time.sleep(1.0)
    stop.set()
    write('logger', '# stopped')
    out.close()


if __name__ == '__main__':
    main()
