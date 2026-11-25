# Design notes

## Goals
A reliable, authenticated, self-healing fire-alert mesh with no gateway: detect fire, propagate to every node, support remote silence/clear with replay protection, and keep working on battery. Target deployment: buildings spread over a farm (hundreds of metres to a few km), Perth, AU915 (30 dBm EIRP limit including antenna gain).

## Why managed flooding
The problem is one-to-all broadcast, not point-to-point routing, so full mesh routing tables add complexity with no benefit. Managed flooding (TTL + duplicate suppression + back-off) is simple and self-healing. Relay order is RSSI-based: the node that heard the packet weakest relays first, pushing the flood outward with fewer collisions. Listen-before-talk and jittered timers reduce hidden-node collisions.

## Radio
921.0 MHz, SF9, BW125, CR4/5, custom sync word. RadioLib was rejected: it does not know about the KCT8103L PA/LNA front-end and caused erratic behaviour, so the firmware uses Heltec's native framework. TX power defaults to 10 dBm (hard cap in config) so total EIRP stays under the 30 dBm limit with antenna gain.

## Packet format (17-18 bytes)
Network magic (2) | version/type | TTL | origin | flags | counter (32-bit: boot epoch << 16 | seq) | payload | HMAC-SHA256 tag (4 bytes, TTL excluded so relays can decrement it).

Types: FIRE, HEARTBEAT, SILENCE, CLEAR, CMD, ACK.

Handling order: magic -> HMAC -> own-echo drop -> replay/duplicate window (32-packet sliding window per origin) -> cleared-episode check -> act -> relay.

## Network isolation (three layers)
1. Custom SX1262 sync word and non-default channel (PHY filter, not security).
2. Network magic in the header, checked before dedup.
3. HMAC on every packet with a pre-shared key; origin and counter are in the MAC input. Authentication is mandatory; encryption is not used.

## Alarm state machine
- `alarmId = (originNode, episode)`; episode counter is persisted in flash and increments per sensor trip.
- FIRE is a state: re-announced about every 5 s while the origin's sensor is tripped.
- SILENCE mutes buzzers but the episode stays active (the dashboard still shows fire). CLEAR ends the episode; both are sent 3 times.
- After CLEAR the origin re-arms only when the sensor is below the clear threshold and a 60 s hold-off has passed; if it is still tripped it starts a *new* episode.
- A node never accepts a FIRE for an episode older than the latest CLEAR it holds for that origin.
- Missing heartbeats mean "unknown", never "clear".

## Node-to-node commands (V5+)
CMD (test fire, recalibrate dust baseline, reboot, status) and ACK use the same HMAC and flooding. The sender repeats a CMD 3 times and stops when the ACK arrives; the target executes once per (sender, request ID) but ACKs every copy so a lost ACK is recovered. A CMD is refused if the target has not yet heard an authentic packet from the sender since its own boot, which blocks replay of a recorded REBOOT right after a restart.

## Smoke sensing
Dynamic baseline instead of hardcoded thresholds: average 50 samples at boot, trigger at +150 ADC counts over baseline, clear at +80, over a 10-sample window. Calibrated empirically with real smoke rather than datasheet conversions. BMP280 temperature (not pressure) confirms; pressure is too noisy to be useful for fire.

## Known security limits
Shared key means any node or stolen flash can command everything; 4-byte tag is adequate for this project but weak against a determined attacker; replay windows are RAM-only; erasing flash resets the boot epoch.
