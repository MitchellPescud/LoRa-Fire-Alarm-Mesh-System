# Power (firmware V6)

## Battery readout filtering
V5 read the ADC once per 250 ms through a 390k/100k divider; ADC noise plus cell sag during LoRa TX/WiFi bursts made the percentage jump about 5%. V6: 16 back-to-back samples with 3 low and 3 high dropped -> median of the last 9 readings (one per 500 ms) -> EMA (alpha 0.25) -> displayed percent only moves when it changes by 2 or more. Settles on a real step in about 6.5 s. Host test (simulated noise, assumed level): steady-battery display spread 8 points / ~800 changes in 5 min before, 0 / 0 after; 10-minute drain tracked within 2 mV.

## Low-power mode
CPU 240 -> 80 MHz (160 MHz while the WiFi AP is up), `delay(1)` per loop so the core idles, OLED off unless an alarm is active or the USER button / WiFi switch wakes it. Radio stays in continuous RX and mesh timing is unchanged, so detection and alert reception are as in V5. Setting: off / on / auto (auto = on when booted without external 5 V), stored in flash.

## Measured
Quick bench measurement on battery, low-power mode on, fitted 3800 mAh cell: **0.07 A WiFi off, 0.15 A WiFi on.** At those currents a constant draw would last about 54 h (WiFi off) and 25 h (WiFi on) - theoretical only, since the cell's capacity is not fully usable and current varies with transmissions. Normal-mode current, alarm-active current and a real discharge test were not done.

## Open levers
Biggest loads are the dust sensor + boost converter (continuous) and radio continuous RX. Planned: SX1262 RxDutyCycle (needs a longer preamble network-wide), runtime mains-loss detection, mains as primary power with battery backup (solar is possible since the board has a solar input).
