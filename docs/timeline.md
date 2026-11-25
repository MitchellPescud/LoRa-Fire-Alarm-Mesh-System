# Project timeline

Reconstructed from the last-saved dates of the author's version folders and photos (AWST). Dates show when each snapshot was finished, not hours spent. The project ran over two semesters; early prototyping in Semester 1 (basic broadcasts between Heltec boards, characterising the dust sensor on an Arduino Nano) came before V1 and is not in these snapshots.

| Date | Tag / commit | What changed |
|---|---|---|
| 2026-05-22 | photo | Original Arduino Nano version: basic smoke sensor only |
| 2026-08-11 | photos | First LoRa node on a breadboard |
| 2026-09-05 | v1 | Breadboard prototype: smoke sensing, alarm, OLED, WiFi dashboard |
| 2026-09-16 | v2 (branch `board-bringup`) | Stripped build for the first designed board; 5V-line sense; new pin map |
| 2026-09-17 | photo | Assembled board photo |
| 2026-10-21 | v3 | V2 hardware support merged with the WiFi dashboard |
| 2026-10-21 | v4 | LoRa mesh with HMAC-authenticated packets, alarm episodes, simulator tests; command node V1 |
| 2026-10-23 | v5 | Node-to-node commands; command node V2 |
| 2026-10-25 | `feature/range-test` | Single-hop range test tooling, field run and charts |
| 2026-10-26 | `feature/mesh-test` | Mesh relay test builds, logs and results |
| 2026-10-29 | v6 | Battery filtering and low-power mode (demo version) |
| 2026-10-30 | - | Fire node enclosure STL parts and dimensioned drawings (STL files are undated exports, dated with the drawings) |

Final oral presentation with the physical prototypes: 30 Oct 2026. The 16 Sep to 21 Oct gap between V2 and V3 is real; the snapshots do not show what happened in between.
