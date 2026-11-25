# Test results

## 1. Host tests (PC, no hardware)
- `test_proto`: all six packet types round-trip, tamper on every byte rejected, wrong key rejected, replay window, a CMD cannot be retyped as an ACK; wire format cross-checked against Python `hmac`.
- `sim_mesh`: real mesh/alarm code on virtual nodes - FIRE across a relay, silence, network-wide clear, re-arm hold-off, replayed CLEAR, forged packets, hidden-node collisions, 30% loss, CMD/ACK across relays, no-reply timeout, replayed CMD refused after reboot. 64 checks, 0 failures. Mutation check: removing de-duplication or the reboot guard makes 4 checks fail each.

## 2. Mesh relay test (3 boards, bench)
Setup: 2 fire nodes (ID 1, 2) and a command node (ID 100); 921 MHz, SF9, BW125, 10 dBm, TTL 5/3. Run A: all in range. Run B: nodes 1 and 2 software-block each other, forcing line topology 1 - 100 - 2. Raw logs: `tools/mesh_test/`.

| Metric | Direct | Forced relay |
|---|---|---|
| Ping 1 -> 2 answered | 100% (20/20) | 85% (17/20) |
| Ping 1 -> 2 round trip (mean) | 1.72 s | 5.17 s |
| FIRE latency at far fire node | 0.76 s | 2.1 s |
| Heartbeat delivery (50 sent) | 96% | 86-88% |

Alarm propagation over 5 cycles: all FIRE/SILENCE/CLEAR delivered in the direct run; in the relay run all reached node 2 through node 100 except one CLEAR (4/5). Each extra hop costs roughly 0.7-0.9 s, mostly the deliberate RSSI-based relay back-off. Alarm delivery stays reliable despite 66-86% per-frame delivery because FIRE is re-announced every ~5 s and SILENCE/CLEAR are sent 3 times.

Caveats: low per-frame delivery was mostly collisions from three nodes on one desk (over 1300 listen-before-talk deferrals), not range loss; blocking is emulated in software; some trace lines were dropped when the USB buffer filled; the test firmware does not interoperate with normal V5 nodes.

## 3. Single-hop range test (Heltec V4.3 pair, urban Perth, 2 sessions + line of sight)
18-byte probes, 100 per burst, SF9 and SF11, no retries or relaying. Delivery counted from unique sequence numbers (Wilson 95% intervals); signal estimated from RSSI corrected for the noise floor. Tools: `tools/range_test/`; charts and sanitised data tables (GPS columns removed): [range-test/](range-test/).

| SF | Distance (m) | Delivered /100 | Est. signal (dBm) |
|---|---|---|---|
| 9 | 37 | 100 | -79 |
| 9 | 159-162 (4 bursts) | 95 / 90 / 58 / 48 | -108 to -117 |
| 9 | 219 | 7 | -118 |
| 9 | 236 / 256 / 273 | 54 / 66 / 46 | -116 to -118 |
| 11 | 38 | 100 | -85 |
| 11 | 159 / 161 | 97 / 70 | -116 / -118 |
| 11 | 226 / 233 / 257 / 272 | 1 / 87 / 70 / 25 | -114 to -122 |

- Obstruction dominates range: four SF9 bursts at about the same 160 m delivered 48% to 95%.
- 50% delivery point at about 130 dB path loss (SF9) vs 132.5 dB (SF11): SF11 gained about 2.3 dB (theory about 5) for ~3.5x the airtime, so the mesh stays at SF9.
- 699 m line of sight: SF9 100% and 98% on the line, SF11 100%; standing about 1 m off the line dropped SF9 to 72% and SF11 to 90%. Margin on the line was only about 10-12 dB, and a free-space estimate predicted 25-30 dB spare, so the real path was 15-20 dB lossier than ideal.
- Practical sensitivity is set by the measured noise floor (about -103 dBm) plus the SF limit, roughly -115 dBm at SF9.
- Design takeaway: plan 10-20 dB of margin per hop; expect hops of about 100-250 m through built-up streets, with clear paths reaching further.

**Test conditions:** every range test, including the 699 m line of sight, used a 10 dBm chip setting (well below the radio's maximum) and a small spring antenna of unknown gain lying inside the closed case next to other electronics, close to the ground. The results are therefore a conservative lower bound; a real deployment with a higher power setting, an external antenna and raised mounting should have more margin, but longer hops were not tested.

Limitations: one session per SF, one hardware pair, SF9 and SF11 at different positions 30 minutes apart, no repeats, antenna height/gain not measured, GPS accuracy about 5-10 m, whole-dB RSSI/SNR.

## 4. Power
See [power.md](power.md).
