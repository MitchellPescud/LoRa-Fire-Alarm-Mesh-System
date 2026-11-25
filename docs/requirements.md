# Requirements and objectives vs results

## Objectives
| # | Objective | Status |
|---|---|---|
| 1 | Gateway-free mesh where every node transmits, receives and relays | Met - frames from node 1 reached node 2 only through node 100, duplicates suppressed |
| 2 | Smoke detection with an optical sensor confirmed by temperature trend | Met qualitatively - detected real smoke; no detection time or false-alarm rate measured |
| 3 | Authenticated packets, replay-protected silence/clear | Met in design and simulation - 4-byte tag and a single shared key limit strength |
| 4 | Local and remote operation | Met - dashboards on every node and the command node; acknowledged node-to-node commands worked on hardware |
| 5 | Power reduction for battery backup | Partly met - quick current reading only; no discharge test |
| 6 | Measure single-hop range and multi-hop relay on real hardware | Partly met - single-hop and relay measured; multi-hop range over distance not |
| 7 | Assess against requirements and identify limitations | Done (this page and the README) |

## Requirements
| ID | Requirement | Result | Met? |
|---|---|---|---|
| R1 | Self-calibrating detection | Baseline and hysteresis work on real smoke; no quantitative data | Yes, qualitatively |
| R2 | No gateway; every node relays | Relay verified on hardware with 3 boards | Yes |
| R3 | Under 10 s over three hops | 2.1 s at one relay hop; 0.7-0.9 s per extra hop suggests about 3-4 s over three hops; three hops tested in simulation only | Partly |
| R4 | At least 95% of alarm cycles delivered | 15 of 15 direct; 14 of 15 in the relay run (CLEAR 4 of 5 cycles) | Mostly; small sample |
| R5 | Authenticated, replay-resistant | HMAC on all types; replay and forgery tests pass in simulation | Yes; 4-byte tag limit |
| R6 | Safe silence and clear | Episode logic verified in simulation and on 3 boards | Yes |
| R7 | Fail safe | Missing heartbeat shown as UNKNOWN, never safe | Yes |
| R8 | Remote management | Commands and ACKs worked, including via a relay | Yes |
| R9 | 24 h on battery | About 54 h (WiFi off) / 25 h (WiFi on) at constant draw; no discharge test | Indicative only |
| R10 | EIRP at or below 30 dBm | Chip setting capped at 20, default 10; antenna and front-end gain not measured | Not verified |
| R11 | Local interfaces | Buzzer, LEDs, OLED, WiFi dashboard working; alarm LED pin pending | Mostly |

The weakest evidence is for R3, R9 and R10, and for any use over distance.

## Future work (from the project report)
Range: repeat walks at identical points, coast run, TX-power steps, antenna height/orientation, multi-hop farm trial. Power: normal and alarm current, full discharge, solar, RxDutyCycle. Security: per-node keys or rotation, longer tag, dashboard login, non-default WiFi passwords, close the post-reboot replay window. Hardware: alarm LED on GPIO38-42, measure antenna and front-end gain. Sensing: compare with a certified detector. Scale: retune beyond 6-8 nodes at SF9.
