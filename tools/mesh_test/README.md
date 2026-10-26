# Mesh_Test_v1 - measuring nodes talking and relaying (2 fire nodes + 1 command node)

Test builds of V5 for the report. The mesh behaves exactly like V5; the test layer only adds
measurement tools:

* **Trace** - every packet a node sends or hears is printed as one line (`TX` / `RX`), plus protocol events (`EV`).
* **Software topology** - `block <id>` makes a node ignore frames sent directly by another node, so three
  boards on one desk behave like a line **1 - 100 - 2** and every packet between the ends must be relayed.
* **Automatic tests** - `test ping`, `test alarm`, `test hb`, started from the serial terminal.
* **Logger + analyser** - `mesh_logger.py` records all three boards on one clock, `analyze_mesh.py` turns the
  log into tables, CSVs and plots.

| Folder | What |
|---|---|
| `Fire_Node_v5_Test/` | fire node sketch (open `Fire_Node_v5_Test.ino`) |
| `Fire_Command_v2_Test/` | command node sketch (open `Fire_Command_v2_Test.ino`) |
| `Mesh_Test_v1/` | this README, `mesh_logger.py`, `analyze_mesh.py` |

Your normal `Fire_Node_v5` / `Fire_Command_v2` folders are untouched.

## 1. One-time setup

1. **PC:** `pip install pyserial matplotlib` (matplotlib is only for the plots).
2. **Node ids.** In `Fire_Node_v5_Test/config.h` set `NODE_ADDR` to **1** and flash board A, then **2** and flash board B.
   The command node stays **100**. Ids must differ in their low 5 bits (`id & 31`): 1, 2 and 100 (=4) are fine;
   avoid pairs like 4 and 100 or 1 and 33 (`block` warns if the id collides with the node you type it on).
3. **Flash all three boards with the TEST sketches.** Same Arduino IDE settings as before (Heltec board package, WiFi LoRa 32(V4),
   LoRa FEM = KCT8103L, USB CDC On Boot enabled). The test builds do not talk to normal V5 nodes (the sender id is carried in
   the TTL byte), so never mix them.
4. Close the Arduino Serial Monitor before logging (the COM port can only be open once).
5. Note which COM port is which board (Device Manager). Below: A = COM5 (node 1), B = COM6 (node 2), command = COM7 (node 100).

## 2. Bench layout

* All three boards on USB to the laptop (a powered hub is fine), antennas fitted and vertical, **at least 2 m apart**, not touching.
* Keep smoke away from the fire nodes (they still calibrate and watch for smoke). The buzzers are **muted** during tests
  (`mute off` to hear them).
* TX power stays at the config value (10 dBm). On a desk every signal is strong, so relays use almost their longest back-off
  (about 0.4-0.7 s). That is real behaviour of the RSSI-based relay delay, but say so in the report; if you want shorter hops, repeat
  the relay run with the boards in different rooms.

## 3. The runs

Do each run as one log. Only run ONE test at a time (wait for its `RES` line), and start a test on **one** node only.

### Run A - direct (everyone hears everyone)

```
python mesh_logger.py run_direct n1=COM5 n2=COM6 n100=COM7
```
Type these into the logger window (`label: command`; `all:` sends to every board). Wait for each `RES` line before the next.
```
all: block none
n100: test ping 1 20 3          # command node -> fire node 1      (about 1.5 min)
n100: test ping 2 20 3
n1: test ping 2 20 3            # fire node 1 -> fire node 2       (direct: no block)
n1: test alarm 5 20 1           # 5 alarm cycles from node 1       (about 2.5 min)
n1: test hb 50 2000             # 50 heartbeats, 2 s apart         (about 1.7 min)
```
`Ctrl+C` when done (the logger asks every board for its final counters first).

### Run B - forced relay: line 1 - 100 - 2 (command node in the middle)

```
python mesh_logger.py run_relay n1=COM5 n2=COM6 n100=COM7
```
```
all: block none
n1: block 2
n2: block 1
all: cfg                        # check: n1 blocked=2, n2 blocked=1, n100 blocked=none
n1: test ping 2 20 3            # 2 nodes apart: CMD and ACK each relayed by the command node
n2: test ping 1 20 3
n100: test ping 1 20 3          # adjacent
n100: test ping 2 20 3
n1: test alarm 5 20 1           # FIRE / SILENCE / CLEAR must be relayed to node 2
n2: test alarm 5 20 1           # and the other way round
n1: test hb 50 2000
```
The block list is saved in flash. **Run `all: block none` at the start of the next session.**

### Optional extra runs (same commands, new log name)

* **Fire node in the middle:** line 100 - 1 - 2: `n100: block 2`, `n2: block 100`, `n1: block none`.
* **Load:** `n1: test hb 100 500` (a heartbeat every 0.5 s) vs `test hb 100 2000`; compare delivery.
* **Hidden terminal:** in the forced line, `all: lbt off` (turn listen-before-talk off) then repeat the heartbeat test.
  With `lbt on` the end nodes still sense each other's carrier on the bench, which a real line would not; `lbt off`
  lets them collide at the middle node like real hidden nodes. Run both and compare.
* **Real distance:** no blocks, move the end nodes until they only reach each other through the middle one.

## 4. Analyse

```
python analyze_mesh.py direct=run_direct.log relay=run_relay.log --out results --plots
```
Writes `results/comparison.md` (direct vs relay side by side), and per run `results/<label>/report.md`, CSVs
(`links.csv`, `packets.csv`, `commands.csv`, `alarm_latency.csv`, `relay_delay.csv`) and figures
(`fig_latency_by_hops.png`, `fig_ping_rtt.png`, `fig_links.png`, `fig_alarm_latency.png`, `fig_relay_delay.png`).
Good report content: the comparison tables (ping RTT, alarm propagation, delivery vs hops), latency-by-hops
figure, flood overhead (relay transmissions per packet), relay queue delay, link RSSI/SNR. The CSVs open in Excel.
Delivery percentages come with 95 % Wilson intervals; 20+ samples per cell is a sensible minimum.

To see what the output looks like before you have hardware results, run it on the two simulated logs in `sample/`:
`python analyze_mesh.py direct=sample/sim_direct.log relay=sample/sim_relay.log --out sample_results --plots` (simulated, not real measurements - do not put them in the report).

### What the numbers mean

| Metric | Definition |
|---|---|
| link delivery | frames node S put on air that node R decoded (own packets and relayed ones) |
| end-to-end delivery | packets a node originated that the other node(s) accepted (first copy) |
| latency | origin transmit start -> first copy decoded at the receiver (PC clock; a few ms USB jitter) |
| hops | relays between origin and receiver (0 = heard directly) |
| ping RTT | `test ping` = STATUS command, request to its ACK; the CMD is re-sent every ~1.5 s until the ACK arrives |
| copies | CMD copies sent before the ACK arrived (2+ at 2 hops is normal: the RTT is longer than the repeat interval) |
| relay queue delay | relay decision -> frame on air: RSSI back-off + jitter + listen-before-talk waits |
| flood overhead | relay transmissions and duplicate copies per originated packet |
| alarm propagation | FIRE created / SILENCE applied / CLEAR applied on each node after the origin issued it |

## 5. Serial commands (115200, newline; also in `testhelp`)

| Command | Effect |
|---|---|
| `test ping <node> [count=20] [gap_s=3]` | STATUS command to `<node>` one at a time; round-trip time and copies |
| `test alarm [count=5] [hold_s=20] [silence=1]` | this node raises a FIRE episode, silences it halfway (if 1), clears it after `hold_s`, waits 10 s, repeats |
| `test hb [count=50] [gap_ms=2000]` | this node sends heartbeats `gap_ms` apart (delivery and relay load) |
| `test stop` / `test status` | stop / show the running test |
| `block [id ...]` / `block none` / `unblock <id>` | ignore frames sent directly by these nodes (saved in flash); no argument lists them |
| `quiet on/off` | pause this node's own periodic heartbeats (tests do this automatically on the initiating node) |
| `mute on/off` | buzzer (default on = muted) |
| `lbt on/off` | listen-before-talk (default on) |
| `trace on/off` | per-packet lines (default on) |
| `cfg`, `stats` | print the CFG / STAT line now |

All normal V5 commands still work (`fire`, `clear`, `silence`, `mesh`, `cmd <node> ...`).

## 6. Log lines

Every line in the log is `<UTC time> <label> <text>`; the text is one of:

* `TX,ms,type,origin,counter,ttl,lane,relay,queue_ms,lbt_defers,len,info` - frame put on air (`relay=1`: forwarded, `lane` = who is transmitting)
* `RX,ms,type,origin,counter,ttl,lane,hops,rssi,snr,disp,info` - valid frame heard; `disp` = NEW first copy, DUP flood duplicate, OWN our own packet relayed back, OLD replay window
* `EV,ms,name,...` - FIRE_START, ALARM_NEW, SILENCE_START/APPLIED, CLEAR_START, ALARM_CLEARED, CMD_START/EXEC/DONE/NOREPLY/DUPCOPY/REJECTED, TEST_START/END
* `CFG` (settings and block list), `STAT` (counters), `RES` (on-node result of a test)

`ms` is the node's own clock; the analyser uses the PC timestamps to compare nodes.

## 7. Limits to mention in the report

* `block` emulates *reception* only. Listen-before-talk still hears the "hidden" node's carrier unless `lbt off`.
* The relay back-off depends on RSSI, so desk results show the long end of it.
* A test packet carries the sender id in the TTL byte (outside the HMAC tag), so the authentication is unchanged but test nodes
  cannot be mixed with normal V5 nodes.
* Latency across nodes has a few ms of USB/PC jitter; use the node-side numbers (RTT, queue delay) when a millisecond matters.
* The trace drops lines (and counts `trace_dropped`) rather than stall the radio if the USB buffer is full; the report warns if that happened.
* The alarm test is a real network-wide alarm (OLED, dashboards, other nodes' state), cleared automatically.

## 8. Verified / not verified

Verified on a PC: the real mesh / alarm / test code on three virtual nodes over a simulated radio (`Fire_Node_v5_Test/tests/sim_trace.cpp`:
line and full topologies, 25 % loss), the logger's output format, and the analyser on those logs; the original 64 V5 simulator checks and the
packet-format tests still pass with the test hooks. **Not done:** compiling with the Arduino/Heltec toolchain, running on the boards,
USB serial behaviour with three boards, real RSSI/timing. First check on hardware: `all: cfg` returns a CFG line from each board, a bench
`n100: test ping 1 5 2` shows RTT of roughly 0.5-0.8 s, and with the block commands set `n1: test ping 2 5 2` takes noticeably longer and reports 1 hop.
