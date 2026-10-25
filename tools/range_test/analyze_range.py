#!/usr/bin/env python3
"""
Analyse Range_Test_v1 serial logs.

Usage
  python analyze_range.py log_sf9_neighbourhood.txt log_sf11_neighbourhood.txt \
         --points points.csv --tx-power 10 --gain-tx 2 --gain-rx 2

  Each LOG FILE is one logger session (the serial output of the fixed node, any
  timestamp prefix is fine). Lines starting RX, SUM, TX, REP, REPNONE, NOISE are
  picked out wherever they appear on a line.

GPS (optional): --gps track.csv --base-lat <fixed-node-lat> --base-lon <fixed-node-lon>
  The phone track (UTC timestamps) is matched to each burst by time. Burst distance =
  distance from the fixed node to the mean GPS position during the burst. Serial lines
  must carry a UTC timestamp prefix (rt_logger.py adds it). Phone and laptop clocks must
  both be network-synced. Use --base-first-fix if the track starts at the fixed node.

points.csv (optional, header required, extra columns ignored)
  session,point,distance_m,environment,note[,time_utc]
  time_utc (ISO, optional) lets a point with no logged packets get its distance from GPS.
  With GPS you still list points only to give them an environment / note, or to
  record bursts the logger never heard.
  log_sf9_neighbourhood,1,50,neighbourhood,corner at the park
  log_sf9_neighbourhood,2,100,neighbourhood,
  session = log file name without extension. A point that is listed here but has
  NO packets in the log is counted as 0 % delivery (the logger heard nothing,
  so it never printed a SUM line). Use --default-count for the burst size
  assumed in that case (default 100).

Output
  summary.csv   one row per (session, point)
  console table + a log-distance path-loss fit per (environment, SF) when
  distances are supplied. With matplotlib installed: pdr_vs_distance.png and
  pdr_vs_pathloss.png.

Notes
  * PDR confidence interval is the Wilson 95 % interval.
  * Path loss = tx_power + gain_tx + gain_rx - mean RSSI. Antenna gains are
    whatever you pass in; they are not measured.
  * The fit uses only points that received at least one packet, so it is biased
    optimistic near the edge of range (censoring) - say so in the write-up.
"""
import argparse
import csv
import math
import os
import re
import sys
from collections import defaultdict
from datetime import datetime, timedelta, timezone

TS = re.compile(r'^\s*\[?(\d{4}-\d{2}-\d{2}[T ][0-9:.]+)(Z|[+-]\d\d:?\d\d)?')


def parse_ts(text):
    """ISO-8601 UTC timestamp -> aware datetime (None if unparseable)."""
    t = text.strip().replace(' ', 'T')
    if t.endswith('Z'):
        t = t[:-1] + '+00:00'
    try:
        d = datetime.fromisoformat(t)
    except ValueError:
        # fractional seconds with 7+ digits, which older Pythons reject
        m = re.match(r'(.*\.\d{6})\d*(.*)$', t)
        if not m:
            return None
        try:
            d = datetime.fromisoformat(m.group(1) + m.group(2))
        except ValueError:
            return None
    if d.tzinfo is None:
        d = d.replace(tzinfo=timezone.utc)
    return d


def haversine_m(lat1, lon1, lat2, lon2):
    r = 6371000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def load_gps(paths, max_acc):
    fixes = []
    for path in paths:
        with open(path, newline='', errors='replace') as f:
            for row in csv.DictReader(f):
                try:
                    t = parse_ts(row['timestamp'])
                    lat, lon = float(row['latitude']), float(row['longitude'])
                    acc = float(row.get('horizontal accuracy') or 0)
                except (KeyError, ValueError, TypeError):
                    continue
                if t is None or (acc > max_acc) or acc < 0:
                    continue
                fixes.append((t, lat, lon))
    fixes.sort()
    return fixes


def position_between(fixes, t0, t1):
    """Mean lat/lon of fixes in [t0, t1] and the largest distance of any fix from that mean (m)."""
    pts = [(la, lo) for (t, la, lo) in fixes if t0 <= t <= t1]
    if not pts:
        return None
    la = sum(p[0] for p in pts) / len(pts)
    lo = sum(p[1] for p in pts) / len(pts)
    spread = max(haversine_m(la, lo, p[0], p[1]) for p in pts)
    return la, lo, spread, len(pts)


def est_signal(rssi, snr):
    """Estimated signal power (dBm) of one packet.

    The radio's RSSI is the total in-band power (signal + noise). When SNR is low the
    reading just sits on the noise floor, so the real signal is lower: with N = noise power,
    signal = N * 10^(SNR/10) and total = N + signal, hence
    signal_dBm = RSSI - 10*log10(1 + 10^(-SNR/10)).  SNR is only reported in whole dB.
    """
    return rssi - 10 * math.log10(1 + 10 ** (-snr / 10.0))


TAG = re.compile(r'(?:^|\s|\])(RX|SUM|TX|TXDONE|REP|REPNONE|NOISE|CFG),([-0-9.,]+)')


def wilson(k, n, z=1.96):
    if n == 0:
        return (0.0, 1.0)
    p = k / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return (max(0.0, c - h), min(1.0, c + h))


def parse_log(path):
    """Returns dict run -> info built from SUM lines (preferred) or RX lines."""
    runs = {}
    rx = defaultdict(list)
    noise = []
    with open(path, errors='replace') as f:
        for line in f:
            m = TAG.search(line)
            if not m:
                continue
            tag, rest = m.group(1), m.group(2).strip().split(',')
            tm = TS.match(line)
            ts = parse_ts(tm.group(1) + (tm.group(2) or '')) if tm else None
            try:
                if tag == 'SUM':
                    ms, run, point, src, total, got, pdr, avg_rssi, min_rssi, avg_snr, min_snr, sf, bw, pwr = rest[:14]
                    runs[int(run)] = dict(point=int(point), src=int(src), total=int(total), got=int(got),
                                          avg_rssi=float(avg_rssi), min_rssi=float(min_rssi),
                                          avg_snr=float(avg_snr), sf=int(sf), bw=int(bw), pwr=int(pwr), t_sum=ts)
                elif tag == 'RX':
                    ms, run, seq, total, point, src, rssi, snr, sf, bw, pwr = rest[:11]
                    rx[int(run)].append(dict(ts=ts, seq=int(seq), total=int(total), point=int(point), src=int(src),
                                             rssi=float(rssi), snr=float(snr), sf=int(sf), bw=int(bw), pwr=int(pwr)))
                elif tag == 'NOISE':
                    noise.append(float(rest[2]))
            except (ValueError, IndexError):
                continue
    # RX lines are authoritative. A run can appear in several SUM lines (older firmware split a
    # burst at every long gap), so count unique sequence numbers instead of trusting SUM.
    out = {}
    for run, rows in rx.items():
        uniq = {}
        for r in rows:
            uniq.setdefault(r['seq'], r)
        uniq = sorted(uniq.values(), key=lambda r: r['seq'])
        r0 = uniq[0]
        sigs = [est_signal(r['rssi'], r['snr']) for r in uniq]
        dts = [(b['ts'] - a['ts']).total_seconds() for a, b in zip(uniq, uniq[1:])
               if b['seq'] == a['seq'] + 1 and a['ts'] and b['ts']]
        dts.sort()
        interval = dts[len(dts) // 2] if dts else (0.5 if r0['sf'] <= 9 else 0.86)
        t0 = t1 = None
        if r0['ts'] is not None:
            t0 = r0['ts'] - timedelta(seconds=r0['seq'] * interval + 1)
            t1 = t0 + timedelta(seconds=r0['total'] * interval + 3)
        out[run] = dict(point=r0['point'], src=r0['src'], total=r0['total'], got=len(uniq),
                        avg_rssi=sum(r['rssi'] for r in uniq) / len(uniq),
                        min_rssi=min(r['rssi'] for r in uniq),
                        avg_snr=sum(r['snr'] for r in uniq) / len(uniq),
                        min_snr=min(r['snr'] for r in uniq),
                        avg_sig=sum(sigs) / len(sigs),
                        sf=r0['sf'], bw=r0['bw'], pwr=r0['pwr'], interval=interval,
                        t_first=t0, t_last=t1, t_sum=None)
    for run, d in runs.items():          # SUM-only runs (no per-packet lines in the log)
        if run not in out:
            d.setdefault('t_first', None)
            d.setdefault('t_last', None)
            d['min_snr'] = d['avg_snr']
            d['avg_sig'] = est_signal(d['avg_rssi'], d['avg_snr'])
            d['interval'] = 0.5
            out[run] = d
    return out, noise


def linfit(xs, ys):
    n = len(xs)
    if n < 2:
        return None
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return None
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    icpt = my - slope * mx
    res = [y - (icpt + slope * x) for x, y in zip(xs, ys)]
    sigma = math.sqrt(sum(r * r for r in res) / (n - 2)) if n > 2 else float('nan')
    return slope, icpt, sigma


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--points', help='points.csv (session,point,distance_m,environment,note)')
    ap.add_argument('--tx-power', type=float, default=10.0, help='TX power setting in dBm (default 10)')
    ap.add_argument('--gain-tx', type=float, default=0.0, help='TX antenna gain dBi (+ front-end gain if you want it counted)')
    ap.add_argument('--gain-rx', type=float, default=0.0, help='RX antenna gain dBi')
    ap.add_argument('--default-count', type=int, default=100, help='burst size assumed for points with no packets logged')
    ap.add_argument('--threshold', type=float, default=0.9, help='PDR threshold used for the "range" figure')
    ap.add_argument('--gps', nargs='+', help='phone GPS track CSV file(s) (timestamp,longitude,latitude,... in UTC)')
    ap.add_argument('--base-lat', type=float, help='fixed node latitude (decimal degrees)')
    ap.add_argument('--base-lon', type=float, help='fixed node longitude (decimal degrees)')
    ap.add_argument('--base-first-fix', action='store_true',
                    help='use the first GPS fix of the track as the fixed node position (walk starts at the fixed node)')
    ap.add_argument('--max-acc', type=float, default=20.0, help='ignore GPS fixes with horizontal accuracy worse than this (m)')
    ap.add_argument('--out', default='summary.csv')
    ap.add_argument('--plots', action='store_true', help='write PNG plots (needs matplotlib)')
    args = ap.parse_args()

    meta = {}
    if args.points:
        with open(args.points, newline='') as f:
            for row in csv.DictReader(f):
                try:
                    meta[(row['session'].strip(), int(row['point']))] = dict(
                        dist=float(row['distance_m']) if (row.get('distance_m') or '').strip() else None,
                        time=parse_ts(row['time_utc']) if (row.get('time_utc') or '').strip() else None,
                        env=(row.get('environment') or '').strip(), note=(row.get('note') or '').strip())
                except (KeyError, ValueError):
                    print('bad points row skipped:', row, file=sys.stderr)

    fixes = load_gps(args.gps, args.max_acc) if args.gps else []
    base = None
    if args.base_lat is not None and args.base_lon is not None:
        base = (args.base_lat, args.base_lon)
    elif args.base_first_fix and fixes:
        base = (fixes[0][1], fixes[0][2])
    if args.gps:
        print(f'GPS: {len(fixes)} usable fixes (accuracy <= {args.max_acc:.0f} m); base = '
              + (f'{base[0]:.6f},{base[1]:.6f}' if base else 'NOT SET (use --base-lat/--base-lon or --base-first-fix)'))

    def gps_distance(t0, t1):
        """(distance_m, spread_m, n_fixes) of the mobile node during [t0, t1], or None."""
        if not fixes or not base or t0 is None or t1 is None:
            return None
        pos = position_between(fixes, t0, t1)
        if not pos:
            return None
        return haversine_m(base[0], base[1], pos[0], pos[1]), pos[2], pos[3]

    rows = []
    for path in args.logs:
        session = os.path.splitext(os.path.basename(path))[0]
        runs, noise = parse_log(path)
        if noise:
            print(f'[{session}] idle noise samples (mean RSSI): ' + ', '.join(f'{n:.1f}' for n in noise))
        # one row per point; if a point was repeated, keep every run as its own row
        seen_points = set()
        sf_of_session = None
        for run, r in sorted(runs.items(), key=lambda kv: (kv[1]['point'], kv[0])):
            m = meta.get((session, r['point']), {})
            seen_points.add(r['point'])
            sf_of_session = r['sf']
            gd = None
            if r.get('t_first') is not None:
                gd = gps_distance(r['t_first'], r['t_last'])
            elif r.get('t_sum') is not None:
                gd = gps_distance(r['t_sum'] - timedelta(seconds=90), r['t_sum'])
            dist = gd[0] if gd else m.get('dist')
            rows.append(dict(session=session, point=r['point'], run=run, sf=r['sf'], bw=r['bw'], pwr=r['pwr'],
                             total=r['total'], got=r['got'], avg_rssi=r['avg_rssi'], min_rssi=r['min_rssi'],
                             avg_snr=r['avg_snr'], min_snr=r.get('min_snr', float('nan')),
                             sig=r['avg_sig'], dist=dist, env=m.get('env', ''), note=m.get('note', ''),
                             inferred=False, dist_src='gps' if gd else ('points' if m.get('dist') is not None else ''),
                             gps_spread=gd[1] if gd else float('nan')))
        for (s, p), m in meta.items():
            if s == session and p not in seen_points:
                gd = gps_distance(m['time'] - timedelta(seconds=20), m['time'] + timedelta(seconds=20)) if m.get('time') else None
                dist = gd[0] if gd else m.get('dist')
                rows.append(dict(session=session, point=p, run='', sf=sf_of_session or '', bw='', pwr=int(args.tx_power),
                                 total=args.default_count, got=0, avg_rssi=float('nan'), min_rssi=float('nan'),
                                 avg_snr=float('nan'), min_snr=float('nan'), sig=float('nan'), dist=dist, env=m.get('env', ''), note=m.get('note', ''),
                                 inferred=True, dist_src='gps' if gd else ('points' if m.get('dist') is not None else ''),
                                 gps_spread=gd[1] if gd else float('nan')))

    if not rows:
        print('No SUM/RX lines found.')
        return 1

    for r in rows:
        pdr = r['got'] / r['total'] if r['total'] else 0.0
        r['pdr'] = pdr
        r['ci_lo'], r['ci_hi'] = wilson(r['got'], r['total'])
        r['path_loss'] = (args.tx_power + args.gain_tx + args.gain_rx - r['sig']) if r['got'] else float('nan')

    rows.sort(key=lambda r: (r['session'], r['dist'] if r['dist'] is not None else 1e9, r['point']))

    fields = ['session', 'point', 'run', 'env', 'dist', 'sf', 'bw', 'pwr', 'total', 'got', 'pdr', 'ci_lo', 'ci_hi',
              'avg_rssi', 'sig', 'min_rssi', 'avg_snr', 'min_snr', 'path_loss', 'inferred', 'dist_src', 'gps_spread', 'note']
    with open(args.out, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction='ignore')
        w.writeheader()
        for r in rows:
            w.writerow({k: (round(v, 3) if isinstance(v, float) else v) for k, v in r.items()})

    print(f"\n{'session':24} {'pt':>3} {'dist':>6} {'SF':>3} {'got/tot':>8} {'PDR%':>5} {'95% CI':>12} {'RSSI':>7} {'sig~':>7} {'SNR':>5} {'PL dB':>6}")
    for r in rows:
        dist = f"{r['dist']:.0f}" if r['dist'] is not None else '-'
        ci = f"{r['ci_lo']*100:.0f}-{r['ci_hi']*100:.0f}"
        rssi = f"{r['avg_rssi']:.1f}" if r['got'] else '-'
        snr = f"{r['avg_snr']:.1f}" if r['got'] else '-'
        sg = f"{r['sig']:.1f}" if r['got'] else '-'
        pl = f"{r['path_loss']:.1f}" if r['got'] else '-'
        flag = '*' if r['inferred'] else ('~' if r.get('gps_spread', 0) == r.get('gps_spread', 0) and r.get('gps_spread', 0) > 15 else ' ')
        print(f"{r['session'][:24]:24} {r['point']:>3} {dist:>6} {str(r['sf']):>3} {str(r['got'])+'/'+str(r['total']):>8}{flag}{r['pdr']*100:>5.0f} {ci:>12} {rssi:>7} {sg:>7} {snr:>5} {pl:>6}")
    if any(r['inferred'] for r in rows):
        print('* no packets were logged for this point: counted as 0 % of --default-count')
    if any(r.get('gps_spread', 0) == r.get('gps_spread', 0) and r.get('gps_spread', 0) > 15 for r in rows):
        print('~ the phone moved more than 15 m during this burst (GPS spread): distance is approximate')

    # Range at threshold + path-loss fit per (env, sf)
    groups = defaultdict(list)
    for r in rows:
        if r['dist'] is not None:
            groups[(r['env'] or '-', r['sf'])].append(r)
    if groups:
        print('\nPer environment / SF:')
    for (env, sf), g in sorted(groups.items(), key=lambda kv: (str(kv[0][0]), str(kv[0][1]))):
        g.sort(key=lambda r: r['dist'])
        good = [r['dist'] for r in g if r['pdr'] >= args.threshold]
        # "range": furthest distance reached before the first point that fails the threshold
        reach = 0
        for r in g:
            if r['pdr'] >= args.threshold:
                reach = r['dist']
            else:
                break
        print(f"  env={env} SF{sf}: points={len(g)}  furthest PDR>={args.threshold*100:.0f}% point: "
              f"{(str(round(max(good))) if good else 'none')} m  continuous-from-start range: {reach:.0f} m")
        pts = [(math.log10(r['dist']), r['path_loss']) for r in g if r['got'] and r['dist'] > 0]
        fit = linfit([p[0] for p in pts], [p[1] for p in pts])
        if fit:
            slope, icpt, sigma = fit
            print(f"     log-distance fit (points with data, n={len(pts)}): path-loss exponent n={slope/10:.2f}, "
                  f"PL(1 m)={icpt:.1f} dB, shadowing sigma={sigma:.1f} dB")

    if args.plots:
        try:
            import matplotlib
            matplotlib.use('Agg')
            import matplotlib.pyplot as plt
        except ImportError:
            print('matplotlib not installed: skipping plots')
            return 0
        series = defaultdict(list)
        for r in rows:
            series[(r['env'] or r['session'], r['sf'])].append(r)
        fig, ax = plt.subplots(figsize=(7, 4))
        for (env, sf), g in sorted(series.items(), key=lambda kv: str(kv[0])):
            g = [r for r in g if r['dist'] is not None]
            g.sort(key=lambda r: r['dist'])
            if not g:
                continue
            ax.errorbar([r['dist'] for r in g], [r['pdr'] * 100 for r in g],
                        yerr=[[max(0.0, (r['pdr'] - r['ci_lo']) * 100) for r in g], [max(0.0, (r['ci_hi'] - r['pdr']) * 100) for r in g]],
                        marker='o', linestyle='none', capsize=3, label=f'{env} SF{sf}')
        ax.axhline(args.threshold * 100, ls='--', lw=0.8, color='grey')
        ax.set_xlabel('distance (m)'); ax.set_ylabel('packet delivery ratio (%)'); ax.set_ylim(-2, 102)
        ax.legend(); ax.grid(alpha=0.3); fig.tight_layout(); fig.savefig('pdr_vs_distance.png', dpi=150)
        fig, ax = plt.subplots(figsize=(7, 4))
        for (env, sf), g in sorted(series.items(), key=lambda kv: str(kv[0])):
            g = [r for r in g if r['got']]
            g.sort(key=lambda r: r['path_loss'])
            if g:
                ax.plot([r['path_loss'] for r in g], [r['pdr'] * 100 for r in g], marker='o', linestyle='none', label=f'{env} SF{sf}')
        ax.set_xlabel('path loss (dB)'); ax.set_ylabel('packet delivery ratio (%)'); ax.set_ylim(-2, 102)
        ax.legend(); ax.grid(alpha=0.3); fig.tight_layout(); fig.savefig('pdr_vs_pathloss.png', dpi=150)
        print('wrote pdr_vs_distance.png, pdr_vs_pathloss.png')
    return 0


if __name__ == '__main__':
    sys.exit(main())
