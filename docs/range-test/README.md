# Range test results

Single-hop range on a Heltec V4.3 pair, 24 Oct 2026 (SF9 and SF11 neighbourhood walks plus a 699 m line-of-sight test). Method and conclusions: [../test-results.md](../test-results.md). All tests used a 10 dBm chip setting and a small spring antenna inside the closed case, so results are a conservative lower bound.

## Charts (`charts/`)
`pdr_vs_distance`, `pdr_vs_pathloss`, `delivery_vs_snr`, `sf9_vs_sf11_pairs`, `signal_margin`, `noise_floor`, `los_bursts`, `los_snr_within_burst`.

## Data (`data/`)
| File | Contents |
|---|---|
| `summary.csv` | One row per walk burst: distance, SF, delivery with 95% interval, RSSI, SNR, estimated signal, path loss |
| `line_of_sight_summary.csv` | The five 699 m line-of-sight bursts |
| `master_table.csv` | All 23 bursts with margin to the 50% delivery point |
| `packets.csv` | Per-packet RSSI/SNR (1,649 packets) |
| `los_snr_drift.csv` | SNR drift within each line-of-sight burst |

GPS latitude/longitude columns and the raw GPS tracks were removed before publishing; distances are as computed from them.
