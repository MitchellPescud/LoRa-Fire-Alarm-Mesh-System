# Range_Test_v1

Standalone single-hop range-test firmware for two Heltec V4.3 boards (no mesh, no retries).

## Setup
1. Open `Range_Test_v1.ino` in Arduino IDE (same board settings as the fire nodes: WiFi LoRa 32(V4), LoRa FEM = KCT8103L, USB CDC On Boot enabled).
2. Set `NODE_ADDR` (top of the sketch) to 1 on one board and 2 on the other, flash both. Antennas on first.
3. Defaults: 921 MHz, SF9, BW125, 10 dBm, 100 probes at 500 ms, sync word 0x5B (the mesh uses 0x5A, so they ignore each other). `RT_SF_ALT` (default 11) is the second SF.

## In the field
- Fixed node: USB to the laptop, logged with `python rt_logger.py COM5 log_sf9_neighbourhood.txt` (needs `pip install pyserial`; adds a UTC timestamp to every line; one file per session). Antenna vertical, fixed height.
- Phone: start the GPS tracker at the fixed node and carry it with the mobile node. Laptop and phone clocks should both be network-synced.
- Mobile node: on a pole, vertical, same height. At each dwell point do a short press of PRG. The OLED shows `OUT pN got/total rssi` when the reply arrives; buzzer: 2 short beeps >=90 %, 1 short 50-89 %, 1 long below 50 % / no reply.
- Hold PRG 1.5 s to switch BOTH nodes between SF9 and SF11 (do it where the link is still good). Resetting a board returns it to the compile-time defaults.
- Write down point number -> distance/location as you go. Point numbers count up from 1 on the mobile node each burst (`point N` over serial to reset).
- Press `noise` on the fixed node (serial) before each session for an idle RSSI reading.

## After
```
python analyze_range.py log_sf9_neighbourhood.txt log_sf11_neighbourhood.txt --gps track_sf9.csv track_sf11.csv --base-lat <fixed-node-lat> --base-lon <fixed-node-lon> --points points.csv --tx-power 10 --gain-tx 2 --gain-rx 2 --plots
```
Each burst's distance is the fixed node to the mean phone position during that burst (fixes worse than 20 m accuracy are ignored; bursts where the phone moved >15 m are flagged `~`). Use `--base-first-fix` if the track starts at the fixed node, or leave out `--gps` and give `distance_m` in `points.csv` instead. For a burst the logger never heard, add a row to `points.csv` with `time_utc` (ISO, UTC) so it still gets a GPS distance and counts as 0 %.
`points.csv`: `session,point,distance_m,environment,note` (session = log file name without extension). A point listed there with no packets in the log is counted as 0 %. Output: `summary.csv`, a table with Wilson 95 % intervals, range at the PDR threshold, a log-distance path-loss fit per environment/SF, and two PNG plots.

## Log tags
`RX` per packet, `SUM` per burst (receiver), `TX`/`TXDONE`/`REP`/`REPNONE` (sender), `NOISE`, `CFG`. Column lists are printed at boot (`help`).

## Verified / not verified
Syntax-checked and run in a PC simulation of two nodes (bursts, reports, loss, SF switching, serial commands). NOT yet compiled with the Heltec toolchain or run on hardware: first check that two boards hear each other on the bench, and that the OLED, button and buzzer behave.
