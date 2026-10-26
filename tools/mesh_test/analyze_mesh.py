#!/usr/bin/env python3
"""
Analyse a mesh test log (from mesh_logger.py) and write report tables, CSVs and plots.

  python analyze_mesh.py run1_relay.log --out results_relay --plots
  python analyze_mesh.py direct=run_direct.log relay=run_relay.log --out results --plots

Each log is analysed on its own (results/<label>/report.md + CSV files + PNGs) and, when
several logs are given, results/comparison.md puts the headline numbers side by side.

What it measures (all from the TX / RX / EV lines the test firmware prints):
  * links       - per directed pair S->R: frames S sent vs frames R heard from S (+ RSSI / SNR)
  * delivery    - every packet a node originated: which other nodes got it, how long it took,
                  how many hops, how many transmissions the flood needed
  * relay delay - how long relays held a packet before re-sending (relay back-off + LBT)
  * commands    - ping (STATUS) round-trip time and number of CMD copies needed
  * alarms      - FIRE / SILENCE / CLEAR propagation time to every node, per cycle
Timestamps come from the PC clock in the log, so cross-node latency has a few ms of USB jitter.
"""
import argparse
import csv
import math
import os
import sys
from collections import defaultdict
from datetime import datetime, timezone

TYPES = ['FIRE', 'HB', 'CLEAR', 'SILENCE', 'CMD', 'ACK']


# --------------------------------------------------------------------------- helpers
def parse_ts(s):
    return datetime.strptime(s, '%Y-%m-%dT%H:%M:%S.%fZ').replace(tzinfo=timezone.utc).timestamp()


def pct(vals, p):
    if not vals:
        return None
    v = sorted(vals)
    k = (len(v) - 1) * p / 100.0
    lo, hi = int(math.floor(k)), int(math.ceil(k))
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def mean(vals):
    return sum(vals) / len(vals) if vals else None


def fmt(x, nd=0, unit=''):
    if x is None:
        return '-'
    return f'{x:.{nd}f}{unit}'


def wilson(k, n, z=1.96):
    if n == 0:
        return None, None
    p = k / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    w = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return (c - w) / d, (c + w) / d


def pdr_text(k, n):
    if n == 0:
        return '-'
    lo, hi = wilson(k, n)
    return f'{100.0 * k / n:.1f} % ({lo * 100:.0f}-{hi * 100:.0f})'


def md_table(headers, rows):
    out = ['| ' + ' | '.join(headers) + ' |', '|' + '|'.join(['---'] * len(headers)) + '|']
    for r in rows:
        out.append('| ' + ' | '.join(str(c) for c in r) + ' |')
    return '\n'.join(out)


def kv(text):
    d = {}
    for part in text.split(','):
        if '=' in part:
            k, v = part.split('=', 1)
            d[k] = v
    return d


def to_int(x, default=0):
    try:
        return int(x)
    except (TypeError, ValueError):
        return default


# --------------------------------------------------------------------------- parsing
class Node:
    def __init__(self, label):
        self.label = label
        self.id = None
        self.cfg = {}
        self.cfg_blocked = set()
        self.stats = {}
        self.first = None
        self.last = None
        self.lines = 0


class Run:
    def __init__(self, label):
        self.label = label
        self.nodes = {}       # label -> Node
        self.tx = []
        self.rx = []
        self.ev = []
        self.res = []
        self.warnings = []
        self.t0 = None
        self.t1 = None

    def node(self, label):
        if label not in self.nodes:
            self.nodes[label] = Node(label)
        return self.nodes[label]


def parse_log(path, label):
    run = Run(label)
    bad = 0
    with open(path, encoding='utf-8', errors='replace') as f:
        for ln in f:
            ln = ln.rstrip('\r\n')
            parts = ln.split(' ', 2)
            if len(parts) < 3:
                continue
            try:
                t = parse_ts(parts[0])
            except ValueError:
                continue
            lab, text = parts[1], parts[2]
            if lab == 'logger':
                continue
            n = run.node(lab)
            n.lines += 1
            n.first = t if n.first is None else n.first
            n.last = t
            run.t0 = t if run.t0 is None else min(run.t0, t)
            run.t1 = t if run.t1 is None else max(run.t1, t)
            tag = text.split(',', 1)[0]
            try:
                if tag == 'TX':
                    f_ = text.split(',')
                    run.tx.append(dict(label=lab, t=t, ms=int(f_[1]), type=f_[2], origin=int(f_[3]), ctr=int(f_[4]),
                                       ttl=int(f_[5]), lane=int(f_[6]), relay=int(f_[7]), qms=int(f_[8]),
                                       defers=int(f_[9]), len=int(f_[10]), info=f_[11] if len(f_) > 11 else '-'))
                elif tag == 'RX':
                    f_ = text.split(',')
                    run.rx.append(dict(label=lab, t=t, ms=int(f_[1]), type=f_[2], origin=int(f_[3]), ctr=int(f_[4]),
                                       ttl=int(f_[5]), lane=int(f_[6]), hops=int(f_[7]), rssi=int(f_[8]),
                                       snr=int(f_[9]), disp=f_[10], info=f_[11] if len(f_) > 11 else '-'))
                elif tag == 'EV':
                    f_ = text.split(',')
                    run.ev.append(dict(label=lab, t=t, ms=int(f_[1]), name=f_[2], args=f_[3:]))
                elif tag == 'CFG':
                    d = kv(text)
                    n.cfg = d
                    n.id = to_int(d.get('node'), n.id)
                    bl = d.get('blocked', 'none')
                    n.cfg_blocked = set() if bl == 'none' else {int(x) for x in bl.split('|') if x}
                elif tag == 'STAT':
                    n.stats = kv(text)
                elif tag == 'RES':
                    run.res.append(dict(label=lab, t=t, text=text))
            except (ValueError, IndexError):
                bad += 1
    if bad:
        run.warnings.append(f'{bad} malformed trace line(s) ignored (usually a line cut by a reset or an unplugged cable).')
    # node ids: CFG, else the origin of packets this node transmitted
    for n in run.nodes.values():
        if n.id is None:
            ids = [r['origin'] for r in run.tx if r['label'] == n.label and r['relay'] == 0]
            if ids:
                n.id = max(set(ids), key=ids.count)
    run.nodes = {k: v for k, v in run.nodes.items() if v.id is not None}
    run.by_id = {n.id: n for n in run.nodes.values()}
    run.id_of = {n.label: n.id for n in run.nodes.values()}
    for lst in (run.tx, run.rx, run.ev, run.res):
        lst[:] = [r for r in lst if r['label'] in run.id_of]
        for r in lst:
            r['node'] = run.id_of[r['label']]
    return run


# --------------------------------------------------------------------------- analysis
def info_target(rec):
    try:
        return int(rec['info'].split(':')[0])
    except (ValueError, IndexError):
        return None


def analyse(run, tail_guard=6.0):
    A = {}
    end_t = run.t1 or 0
    ids = sorted(run.by_id)
    lane = lambda i: i & 0x1F  # noqa: E731

    # ---- test windows ----
    windows = []
    open_w = {}
    for e in sorted(run.ev, key=lambda e: e['t']):
        if e['name'] == 'TEST_START':
            open_w[e['node']] = dict(node=e['node'], kind=e['args'][0], args=e['args'][1:], t0=e['t'], t1=None)
        elif e['name'] == 'TEST_END' and e['node'] in open_w:
            w = open_w.pop(e['node'])
            w['t1'] = e['t']
            windows.append(w)
    for w in open_w.values():
        w['t1'] = end_t
        windows.append(w)
    A['windows'] = windows

    # ---- link layer ----
    rx_idx = defaultdict(dict)   # receiver id -> {(lane, origin, ctr, ttl): rx}
    for r in run.rx:
        rx_idx[r['node']].setdefault((r['lane'], r['origin'], r['ctr'], r['ttl']), r)
    links = {}
    for s in ids:
        for rcv in ids:
            if s == rcv:
                continue
            rn = run.by_id[rcv]
            blocked = s in rn.cfg_blocked
            txs = [x for x in run.tx if x['node'] == s and rn.first is not None and rn.first <= x['t'] <= rn.last - 1.0
                   and x['t'] <= end_t - tail_guard]
            hit = [rx_idx[rcv].get((lane(s), x['origin'], x['ctr'], x['ttl'])) for x in txs]
            got = [h for h in hit if h is not None]
            links[(s, rcv)] = dict(sent=len(txs), heard=len(got), blocked=blocked,
                                   rssi=mean([g['rssi'] for g in got]), snr=mean([g['snr'] for g in got]))
    A['links'] = links

    # ---- end to end ----
    first_new = {}                      # (receiver, origin, ctr) -> rx
    for r in run.rx:
        if r['disp'] == 'NEW':
            first_new.setdefault((r['node'], r['origin'], r['ctr']), r)
    relays_of = defaultdict(list)       # (origin, ctr) -> relay TX records
    for x in run.tx:
        if x['relay']:
            relays_of[(x['origin'], x['ctr'])].append(x)
    dups_of = defaultdict(int)
    for r in run.rx:
        if r['disp'] in ('DUP', 'OWN'):
            dups_of[(r['origin'], r['ctr'])] += 1

    def test_of(node, t):
        for w in windows:
            if w['node'] == node and w['t0'] - 0.5 <= t <= w['t1'] + 4.0:
                return w['kind']
        return 'background'

    pkts = []                           # one row per (originated packet, intended receiver)
    orig = [x for x in run.tx if not x['relay'] and x['t'] <= end_t - tail_guard]
    for x in orig:
        if x['type'] in ('CMD', 'ACK'):
            tgt = info_target(x)
            recv = [tgt] if tgt in run.by_id and tgt != x['node'] else []
        else:
            recv = [i for i in ids if i != x['node']]
        rel = relays_of.get((x['origin'], x['ctr']), [])
        test = test_of(x['node'], x['t'])
        for rcv in recv:
            fn = first_new.get((rcv, x['origin'], x['ctr']))
            pkts.append(dict(type=x['type'], origin=x['node'], rcv=rcv, ctr=x['ctr'], t=x['t'], test=test,
                             delivered=fn is not None, lat_ms=(fn['t'] - x['t']) * 1000 if fn else None,
                             hops=fn['hops'] if fn else None, rssi=fn['rssi'] if fn else None,
                             relays=len(rel), dups=dups_of.get((x['origin'], x['ctr']), 0)))
    A['pkts'] = pkts
    A['orig'] = orig

    # ---- relay delay ----
    A['relay_tx'] = [x for x in run.tx if x['relay']]

    # ---- commands ----
    cmd_start = {}
    cmds = []
    for e in sorted(run.ev, key=lambda e: e['t']):
        a = e['args']
        if e['name'] == 'CMD_START':
            cmd_start[(e['node'], int(a[0]), int(a[1]), int(a[2]))] = e
        elif e['name'] in ('CMD_DONE', 'CMD_NOREPLY'):
            k = (e['node'], int(a[0]), int(a[1]), int(a[2]))
            st = cmd_start.pop(k, None)
            row = dict(src=e['node'], dst=int(a[0]), cmd=int(a[1]), req=int(a[2]), t=st['t'] if st else e['t'])
            if e['name'] == 'CMD_DONE':
                row.update(ok=True, result=int(a[3]), copies=int(a[4]), rtt_ms=int(a[5]), late=int(a[6]) == 1)
            else:
                row.update(ok=False, result=None, copies=int(a[3]), rtt_ms=None, late=False)
            row['test'] = test_of(e['node'], row['t'])
            cmds.append(row)
    # hop count of the CMD at its target (from the target's own RX lines)
    cmd_hops = defaultdict(list)
    for r in run.rx:
        if r['type'] == 'CMD' and r['disp'] == 'NEW' and info_target(r) == r['node']:
            cmd_hops[(r['origin'], r['node'])].append(r['hops'])
    A['cmds'] = cmds
    A['cmd_hops'] = cmd_hops

    # ---- alarms ----
    ev_first = {}
    for e in sorted(run.ev, key=lambda e: e['t']):
        a = e['args']
        if e['name'] in ('ALARM_NEW', 'ALARM_CLEARED', 'SILENCE_APPLIED'):
            ev_first.setdefault((e['name'], e['node'], int(a[0]), int(a[1])), e)
    alarms = []
    for e in sorted(run.ev, key=lambda e: e['t']):
        a = e['args']
        if e['name'] == 'FIRE_START':
            origin, ep, phase, got_name = e['node'], int(a[0]), 'FIRE', 'ALARM_NEW'
        elif e['name'] == 'SILENCE_START':
            origin, ep, phase, got_name = int(a[0]), int(a[1]), 'SILENCE', 'SILENCE_APPLIED'
        elif e['name'] == 'CLEAR_START':
            origin, ep, phase, got_name = int(a[0]), int(a[1]), 'CLEAR', 'ALARM_CLEARED'
        else:
            continue
        if e['t'] > end_t - tail_guard:
            continue
        for rcv in ids:
            if rcv == e['node']:
                continue
            g = ev_first.get((got_name, rcv, origin, ep))
            alarms.append(dict(phase=phase, alarm_origin=origin, episode=ep, issuer=e['node'], rcv=rcv, t=e['t'],
                               ok=g is not None, lat_ms=(g['t'] - e['t']) * 1000 if g else None,
                               hops=to_int(g['args'][2]) if (g and phase == 'FIRE') else None))
    A['alarms'] = alarms
    return A


# --------------------------------------------------------------------------- report
def stat_row(vals, nd=0):
    if not vals:
        return ['-'] * 5
    return [fmt(mean(vals), nd), fmt(pct(vals, 50), nd), fmt(pct(vals, 95), nd), fmt(min(vals), nd), fmt(max(vals), nd)]


def report(run, A, outdir):
    L = []
    ids = sorted(run.by_id)
    name = lambda i: f'{i}' + (' (cmd)' if run.by_id[i].cfg.get('role') == 'command' else '')  # noqa: E731
    L.append(f'# Mesh test report - {run.label}\n')
    dur = (run.t1 - run.t0) if run.t0 else 0
    L.append(f'Log span {datetime.fromtimestamp(run.t0, timezone.utc):%Y-%m-%d %H:%M:%S} UTC, {dur / 60:.1f} min, '
             f'{len(run.tx)} TX and {len(run.rx)} RX trace lines from {len(ids)} node(s).\n')
    for w in run.warnings:
        L.append(f'> WARNING: {w}\n')
    dropped = sum(to_int(n.stats.get('trace_dropped')) for n in run.nodes.values())
    if dropped:
        L.append(f'> WARNING: {dropped} trace line(s) were dropped on the nodes (USB buffer full) - counts below are slightly low.\n')

    # ---- setup ----
    L.append('## 1. Setup\n')
    rows = []
    for i in ids:
        c = run.by_id[i].cfg
        rows.append([name(i), run.by_id[i].label, c.get('sf', '?'), c.get('pwr_dbm', '?'), c.get('freq_mhz', '?'),
                     c.get('airtime18_ms', '?'), f"{c.get('ttl_fire', '?')}/{c.get('ttl_ctrl', '?')}/{c.get('ttl_hb', '?')}",
                     f"{c.get('relay_max_ms', '?')}+{c.get('relay_jit_ms', '?')}", c.get('lbt', '?'),
                     c.get('blocked', '?')])
    L.append(md_table(['node', 'log label', 'SF', 'TX dBm', 'MHz', 'airtime 18 B (ms)', 'TTL fire/ctrl/hb',
                       'relay delay max+jitter (ms)', 'LBT', 'ignores frames from'], rows))
    pairs = sorted({tuple(sorted((a, b))) for a in ids for b in ids
                    if a != b and b in run.by_id[a].cfg_blocked and a in run.by_id[b].cfg_blocked})
    one_way = sorted((a, b) for a in ids for b in run.by_id[a].cfg_blocked if b in run.by_id and a not in run.by_id[b].cfg_blocked)
    if pairs:
        L.append('\nEmulated topology: ' + ', '.join(f'{a} and {b} cannot hear each other (blocked at both ends)' for a, b in pairs) + '.')
    elif not one_way:
        L.append('\nTopology: nothing blocked - every node can hear every other node directly.')
    if one_way:
        L.append('\nOne-way blocks: ' + ', '.join(f'{a} ignores {b}' for a, b in one_way) + '.')
    L.append('')

    # ---- links ----
    L.append('## 2. Links (every frame a node transmitted, as heard by each other node)\n')
    rows, csvrows = [], []
    for (s, r), d in sorted(A['links'].items()):
        if d['blocked']:
            rows.append([f'{s} -> {r}', d['sent'], 'blocked (emulated)', '-', '-', '-'])
        else:
            rows.append([f'{s} -> {r}', d['sent'], d['heard'], pdr_text(d['heard'], d['sent']),
                         fmt(d['rssi'], 1), fmt(d['snr'], 1)])
        csvrows.append([s, r, d['sent'], d['heard'], int(d['blocked']), d['rssi'], d['snr']])
    L.append(md_table(['link S -> R', 'frames S sent', 'heard by R', 'link delivery % (95 % CI)', 'mean RSSI (dBm)', 'mean SNR (dB)'], rows))
    L.append('\nA frame is anything the node put on air: its own packets and the ones it relayed. "Heard" means the receiving node decoded it and the HMAC was valid.\n')
    write_csv(outdir, 'links.csv', ['sender', 'receiver', 'frames_sent', 'frames_heard', 'blocked', 'mean_rssi', 'mean_snr'], csvrows)

    # ---- end-to-end ----
    pk = A['pkts']
    L.append('## 3. End-to-end delivery of originated packets\n')
    L.append('Latency = origin transmit start to the receiver\'s first copy being decoded (PC clock). '
             'CMD and ACK count only their intended receiver; the other types count every other node.\n')
    rows = []
    for t in TYPES:
        for o in ids:
            for r in ids:
                sel = [p for p in pk if p['type'] == t and p['origin'] == o and p['rcv'] == r]
                if not sel:
                    continue
                lat = [p['lat_ms'] for p in sel if p['delivered']]
                hops = [p['hops'] for p in sel if p['delivered']]
                rows.append([t, f'{o} -> {r}', len(sel), sum(p['delivered'] for p in sel), pdr_text(sum(p['delivered'] for p in sel), len(sel)),
                             fmt(mean(hops), 1)] + stat_row(lat))
    L.append(md_table(['type', 'origin -> receiver', 'sent', 'delivered', 'delivery % (95 % CI)', 'mean hops',
                       'lat mean (ms)', 'median', 'p95', 'min', 'max'], rows))
    L.append('')
    L.append('### Latency by hop count (all types)\n')
    rows = []
    for h in sorted({p['hops'] for p in pk if p['delivered']}):
        lat = [p['lat_ms'] for p in pk if p['delivered'] and p['hops'] == h]
        rows.append([h, len(lat)] + stat_row(lat))
    L.append(md_table(['hops', 'packets', 'mean (ms)', 'median', 'p95', 'min', 'max'], rows))
    ap = [mean([to_int(n.cfg.get('airtime18_ms')) for n in run.nodes.values() if n.cfg.get('airtime18_ms')])]
    if ap[0]:
        L.append(f'\nSanity check: 0 hops should be about one airtime ({ap[0]:.0f} ms for 18 bytes) plus a few ms of USB timing; each further hop adds the relay back-off, one more airtime, and LBT waits.\n')

    # flood overhead
    L.append('### Flood overhead (transmissions needed per originated packet)\n')
    rows = []
    for t in TYPES:
        sel = [x for x in A['orig'] if x['type'] == t]
        if not sel:
            continue
        keys = {(p['origin'], p['ctr']): p for p in pk if p['type'] == t}
        rel = [p['relays'] for p in keys.values()]
        dup = [p['dups'] for p in keys.values()]
        rows.append([t, len(sel), fmt(mean(rel), 2), fmt(1 + mean(rel), 2) if rel else '-', fmt(mean(dup), 2)])
    L.append(md_table(['type', 'originated', 'relay transmissions / packet', 'total transmissions / packet', 'duplicate copies heard / packet'], rows))
    L.append('')
    write_csv(outdir, 'packets.csv', ['type', 'origin', 'receiver', 'counter', 'time_utc', 'test', 'delivered', 'latency_ms', 'hops', 'rssi', 'relay_tx', 'dup_rx'],
              [[p['type'], p['origin'], p['rcv'], p['ctr'], datetime.fromtimestamp(p['t'], timezone.utc).isoformat(), p['test'],
                int(p['delivered']), '' if p['lat_ms'] is None else round(p['lat_ms'], 1), '' if p['hops'] is None else p['hops'],
                '' if p['rssi'] is None else p['rssi'], p['relays'], p['dups']] for p in pk])

    # ---- relay delay ----
    L.append('## 4. Relay behaviour\n')
    rows, csvrows = [], []
    for i in ids:
        rt = [x for x in A['relay_tx'] if x['node'] == i]
        q = [x['qms'] for x in rt]
        orig_q = [x['qms'] for x in run.tx if x['node'] == i and not x['relay']]
        rows.append([name(i), len(rt)] + stat_row(q) + [sum(x['defers'] for x in rt), fmt(mean(orig_q), 0) if orig_q else '-'])
        csvrows += [[i, x['type'], x['qms'], x['defers']] for x in rt]
    L.append(md_table(['node', 'relayed frames', 'queue delay mean (ms)', 'median', 'p95', 'min', 'max', 'LBT backoffs', 'own-packet queue (ms)'], rows))
    L.append('\nQueue delay = time from the relay decision to the frame going on air: the RSSI-based back-off '
             '(stronger signal = longer wait, up to relay_max) + jitter + any listen-before-talk backoffs. '
             'On a desk every signal is strong, so delays sit near the maximum; at range they shrink.\n')
    write_csv(outdir, 'relay_delay.csv', ['node', 'type', 'queue_ms', 'lbt_defers'], csvrows)

    # ---- commands ----
    cm = A['cmds']
    if cm:
        L.append('## 5. Remote commands (ping = STATUS request and ACK)\n')
        rows = []
        for (s, d) in sorted({(c['src'], c['dst']) for c in cm}):
            sel = [c for c in cm if c['src'] == s and c['dst'] == d]
            ok = [c for c in sel if c['ok']]
            rtt = [c['rtt_ms'] for c in ok]
            hops = A['cmd_hops'].get((s, d), [])
            cop = {k: sum(1 for c in ok if c['copies'] == k) for k in (1, 2, 3)}
            rows.append([f'{s} -> {d}', len(sel), len(ok), pdr_text(len(ok), len(sel)), fmt(mean(hops), 1)] + stat_row(rtt) +
                        [f"{cop[1]} / {cop[2]} / {cop[3]}"])
        L.append(md_table(['asker -> target', 'requests', 'answered', 'answered % (95 % CI)', 'mean CMD hops', 'RTT mean (ms)', 'median', 'p95', 'min', 'max',
                           'ACK after 1 / 2 / 3 copies'], rows))
        L.append('\nRTT runs from the request to its ACK arriving. A CMD is re-sent every ~1.5 s until the ACK arrives, so "2 copies" means the first '
                 'copy or its ACK was lost or simply took longer than the 1.5 s repeat interval (typical for 2+ hops). A request with no ACK 8 s after the last copy counts as not answered.\n')
        write_csv(outdir, 'commands.csv', ['asker', 'target', 'cmd', 'req', 'time_utc', 'test', 'answered', 'rtt_ms', 'copies'],
                  [[c['src'], c['dst'], c['cmd'], c['req'], datetime.fromtimestamp(c['t'], timezone.utc).isoformat(), c['test'],
                    int(c['ok']), '' if c['rtt_ms'] is None else c['rtt_ms'], c['copies']] for c in cm])

    # ---- alarms ----
    al = A['alarms']
    if al:
        L.append('## 6. Alarm propagation (FIRE, SILENCE, CLEAR)\n')
        rows = []
        for ph in ('FIRE', 'SILENCE', 'CLEAR'):
            for issuer in sorted({a['issuer'] for a in al if a['phase'] == ph}):
                for r in ids:
                    sel = [a for a in al if a['phase'] == ph and a['issuer'] == issuer and a['rcv'] == r]
                    if not sel:
                        continue
                    ok = [a for a in sel if a['ok']]
                    lat = [a['lat_ms'] for a in ok]
                    hops = [a['hops'] for a in ok if a['hops'] is not None]
                    rows.append([ph, f'{issuer} -> {r}', len(sel), len(ok), pdr_text(len(ok), len(sel)),
                                 fmt(mean(hops), 1) if hops else '-'] + stat_row(lat))
        L.append(md_table(['step', 'issued by -> node', 'cycles', 'reached', 'reached % (95 % CI)', 'hops (FIRE)', 'mean (ms)', 'median', 'p95', 'min', 'max'], rows))
        L.append('\nFIRE = alarm created on the node (first FIRE packet decoded); SILENCE = buzzer-mute applied; CLEAR = alarm removed. '
                 'FIRE is re-announced every ~5 s and SILENCE / CLEAR are sent 3 times, so a node that misses the first packet still gets there - the latency then jumps by a repeat interval.\n')
        write_csv(outdir, 'alarm_latency.csv', ['step', 'alarm_origin', 'episode', 'issuer', 'node', 'time_utc', 'reached', 'latency_ms', 'hops'],
                  [[a['phase'], a['alarm_origin'], a['episode'], a['issuer'], a['rcv'], datetime.fromtimestamp(a['t'], timezone.utc).isoformat(),
                    int(a['ok']), '' if a['lat_ms'] is None else round(a['lat_ms'], 1), '' if a['hops'] is None else a['hops']] for a in al])

    # ---- heartbeat load tests ----
    hbw = [w for w in A['windows'] if w['kind'] == 'hb']
    if hbw:
        L.append('## 7. Heartbeat load tests (`test hb`)\n')
        rows = []
        for w in hbw:
            sel = [p for p in pk if p['type'] == 'HB' and p['origin'] == w['node'] and w['t0'] - 0.5 <= p['t'] <= w['t1'] + 0.5]
            for r in ids:
                s2 = [p for p in sel if p['rcv'] == r]
                if not s2:
                    continue
                lat = [p['lat_ms'] for p in s2 if p['delivered']]
                hops = [p['hops'] for p in s2 if p['delivered']]
                rows.append([f"{w['node']} ({datetime.fromtimestamp(w['t0'], timezone.utc):%H:%M:%S})", w['args'][1] if len(w['args']) > 1 else '?',
                             r, len(s2), pdr_text(sum(p['delivered'] for p in s2), len(s2)), fmt(mean(hops), 1)] + stat_row(lat)[:3])
        L.append(md_table(['sender (start)', 'gap (ms)', 'receiver', 'sent', 'delivery % (95 % CI)', 'mean hops', 'lat mean (ms)', 'median', 'p95'], rows))
        L.append('')

    # ---- counters ----
    L.append('## 8. Node counters at the end of the log (STAT lines)\n')
    rows = []
    keys = ['rx_total', 'rx_blocked', 'rx_dup', 'rx_own_echo', 'rx_auth_fail', 'rx_foreign', 'rx_crc_err', 'tx_orig', 'tx_relayed', 'tx_dropped', 'tx_deferred', 'cmd_rejected', 'trace_dropped']
    for i in ids:
        s = run.by_id[i].stats
        rows.append([name(i)] + [s.get(k, '-') for k in keys])
    L.append(md_table(['node'] + keys, rows))
    L.append('\nrx_blocked counts frames ignored by the emulated topology (they were really received by the radio). rx_crc_err = frames the radio saw but could not decode (collisions, weak signal). tx_deferred = listen-before-talk backoffs.\n')

    # ---- tests run ----
    if A['windows']:
        L.append('## 9. Tests found in the log\n')
        rows = [[w['node'], w['kind'], ' '.join(w['args']), datetime.fromtimestamp(w['t0'], timezone.utc).strftime('%H:%M:%S'), f"{w['t1'] - w['t0']:.0f}"] for w in A['windows']]
        L.append(md_table(['node', 'test', 'parameters', 'start (UTC)', 'duration (s)'], rows))
        resl = [r for r in run.res]
        if resl:
            L.append('\nOn-node result lines:\n')
            L.append('```\n' + '\n'.join(f"{r['label']} {r['text']}" for r in resl) + '\n```')
        L.append('')

    with open(os.path.join(outdir, 'report.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')


def write_csv(outdir, name, header, rows):
    with open(os.path.join(outdir, name), 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


# --------------------------------------------------------------------------- plots
def plots(run, A, outdir):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print('matplotlib missing - skipping plots (pip install matplotlib)')
        return
    ids = sorted(run.by_id)

    pk = [p for p in A['pkts'] if p['delivered']]
    types = [t for t in ('FIRE', 'HB', 'CLEAR', 'SILENCE', 'CMD', 'ACK') if any(p['type'] == t for p in pk)]
    if pk:
        fig, ax = plt.subplots(figsize=(7, 4.2))
        hops_all = sorted({p['hops'] for p in pk})
        data, labels = [], []
        for h in hops_all:
            v = [p['lat_ms'] for p in pk if p['hops'] == h]
            data.append(v)
            labels.append(f'{h} hop{"s" if h != 1 else ""}\n(n={len(v)})')
        ax.boxplot(data, showfliers=True)
        ax.set_xticklabels(labels)     # (the labels= keyword was renamed between matplotlib versions)
        ax.set_ylabel('end-to-end latency (ms)')
        ax.set_title('Packet latency by hop count')
        ax.grid(axis='y', alpha=.3)
        fig.tight_layout()
        fig.savefig(os.path.join(outdir, 'fig_latency_by_hops.png'), dpi=150)
        plt.close(fig)

    links = [(k, d) for k, d in sorted(A['links'].items()) if not d['blocked'] and d['sent']]
    if links:
        fig, (a1, a2) = plt.subplots(1, 2, figsize=(9, 3.8))
        lab = [f'{s}->{r}' for (s, r), _ in links]
        a1.bar(lab, [100.0 * d['heard'] / d['sent'] for _, d in links], color='#2a78b5')
        a1.set_ylim(0, 105)
        a1.set_ylabel('frames heard (%)')
        a1.set_title('Link delivery')
        a2.bar(lab, [d['rssi'] if d['rssi'] is not None else 0 for _, d in links], color='#c26a1b')
        a2.set_ylabel('mean RSSI (dBm)')
        a2.set_title('Link signal strength')
        for a in (a1, a2):
            a.grid(axis='y', alpha=.3)
        fig.tight_layout()
        fig.savefig(os.path.join(outdir, 'fig_links.png'), dpi=150)
        plt.close(fig)

    rel = [x['qms'] for x in A['relay_tx']]
    if rel:
        fig, ax = plt.subplots(figsize=(6.5, 3.8))
        ax.hist(rel, bins=30, color='#4a8f4a')
        ax.set_xlabel('relay queue delay (ms)')
        ax.set_ylabel('frames')
        ax.set_title('How long relays held a frame before re-sending')
        ax.grid(axis='y', alpha=.3)
        fig.tight_layout()
        fig.savefig(os.path.join(outdir, 'fig_relay_delay.png'), dpi=150)
        plt.close(fig)

    ok = [c for c in A['cmds'] if c['ok']]
    if ok:
        fig, ax = plt.subplots(figsize=(7, 4))
        for (s, d) in sorted({(c['src'], c['dst']) for c in ok}):
            v = [c['rtt_ms'] for c in ok if c['src'] == s and c['dst'] == d]
            ax.plot(range(1, len(v) + 1), v, marker='o', ms=3, label=f'{s} -> {d}')
        ax.set_xlabel('request #')
        ax.set_ylabel('round-trip time (ms)')
        ax.set_title('Ping (STATUS command + ACK) round-trip time')
        ax.legend()
        ax.grid(alpha=.3)
        fig.tight_layout()
        fig.savefig(os.path.join(outdir, 'fig_ping_rtt.png'), dpi=150)
        plt.close(fig)

    al = [a for a in A['alarms'] if a['ok']]
    if al:
        fig, ax = plt.subplots(figsize=(7.5, 4))
        colors = {'FIRE': '#c0392b', 'SILENCE': '#d68910', 'CLEAR': '#2471a3'}
        xs = {}
        for i, (ph, r) in enumerate(sorted({(a['phase'], a['rcv']) for a in al}, key=lambda z: (['FIRE', 'SILENCE', 'CLEAR'].index(z[0]), z[1]))):
            v = [a['lat_ms'] for a in al if a['phase'] == ph and a['rcv'] == r]
            ax.scatter([i] * len(v), v, color=colors[ph], alpha=.7, s=22)
            xs[i] = f'{ph}\nnode {r}'
            ax.plot([i - .25, i + .25], [mean(v)] * 2, color='k', lw=1.5)
        ax.set_xticks(list(xs))
        ax.set_xticklabels(list(xs.values()), fontsize=8)
        ax.set_ylabel('time to reach node (ms)')
        ax.set_title('Alarm propagation (each dot = one cycle, bar = mean)')
        ax.grid(axis='y', alpha=.3)
        fig.tight_layout()
        fig.savefig(os.path.join(outdir, 'fig_alarm_latency.png'), dpi=150)
        plt.close(fig)


# --------------------------------------------------------------------------- comparison
def comparison(runs, analyses, outdir):
    L = ['# Comparison of runs\n']
    rows = []
    for run, A in zip(runs, analyses):
        for (s, d) in sorted({(c['src'], c['dst']) for c in A['cmds']}):
            sel = [c for c in A['cmds'] if c['src'] == s and c['dst'] == d]
            ok = [c for c in sel if c['ok']]
            rtt = [c['rtt_ms'] for c in ok]
            hops = A['cmd_hops'].get((s, d), [])
            rows.append([run.label, f'{s} -> {d}', fmt(mean(hops), 1), len(sel), pdr_text(len(ok), len(sel))] + stat_row(rtt)[:3] + [fmt(mean([c['copies'] for c in ok]), 2)])
    if rows:
        L.append('## Ping round-trip\n')
        L.append(md_table(['run', 'asker -> target', 'CMD hops', 'requests', 'answered', 'RTT mean (ms)', 'median', 'p95', 'mean copies'], rows))
        L.append('')
    rows = []
    for run, A in zip(runs, analyses):
        for ph in ('FIRE', 'SILENCE', 'CLEAR'):
            for r in sorted(run.by_id):
                sel = [a for a in A['alarms'] if a['phase'] == ph and a['rcv'] == r]
                if not sel:
                    continue
                ok = [a for a in sel if a['ok']]
                rows.append([run.label, ph, f'node {r}', len(sel), pdr_text(len(ok), len(sel))] + stat_row([a['lat_ms'] for a in ok])[:3])
    if rows:
        L.append('## Alarm propagation\n')
        L.append(md_table(['run', 'step', 'at', 'cycles', 'reached', 'mean (ms)', 'median', 'p95'], rows))
        L.append('')
    rows = []
    for run, A in zip(runs, analyses):
        for t in TYPES:
            sel = [p for p in A['pkts'] if p['type'] == t]
            if not sel:
                continue
            ok = [p for p in sel if p['delivered']]
            rows.append([run.label, t, len(sel), pdr_text(len(ok), len(sel)), fmt(mean([p['hops'] for p in ok]), 2),
                         fmt(mean([p['lat_ms'] for p in ok]), 0), fmt(mean([p['relays'] for p in sel]), 2)])
    if rows:
        L.append('## Packet delivery\n')
        L.append(md_table(['run', 'type', 'packet x receiver', 'delivered', 'mean hops', 'latency mean (ms)', 'relay tx / packet'], rows))
        L.append('')
    with open(os.path.join(outdir, 'comparison.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+', help='log file, or label=log file')
    ap.add_argument('--out', default='results', help='output folder (default: results)')
    ap.add_argument('--plots', action='store_true', help='write PNG figures (needs matplotlib)')
    ap.add_argument('--tail-guard', type=float, default=6.0,
                    help='ignore packets sent in the last N seconds of the log - they had no time to arrive (default 6)')
    args = ap.parse_args()

    runs, analyses = [], []
    for item in args.logs:
        if '=' in item and not os.path.exists(item):
            label, path = item.split('=', 1)
        else:
            label, path = os.path.splitext(os.path.basename(item))[0], item
        if not os.path.exists(path):
            sys.exit(f'no such file: {path}')
        run = parse_log(path, label)
        if not run.nodes:
            sys.exit(f'{path}: no node data found (is it a mesh_logger.py log?)')
        sub = os.path.join(args.out, label) if len(args.logs) > 1 else args.out
        os.makedirs(sub, exist_ok=True)
        A = analyse(run, args.tail_guard)
        report(run, A, sub)
        if args.plots:
            plots(run, A, sub)
        print(f'{label}: {len(run.nodes)} nodes, {len(run.tx)} TX, {len(run.rx)} RX -> {sub}{os.sep}report.md')
        runs.append(run)
        analyses.append(A)
    if len(runs) > 1:
        comparison(runs, analyses, args.out)
        print(f'comparison -> {os.path.join(args.out, "comparison.md")}')


if __name__ == '__main__':
    main()
