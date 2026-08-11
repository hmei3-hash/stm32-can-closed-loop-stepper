# Measurements

Record before-data here *before* any refactor that makes it unrecoverable.

| Date | Phase | Metric | Value | Method |
|---|---|---|---|---|
| | 1 | Blink period (target 1.000 s) | | 60-blink stopwatch |
| | 1 | MCO2 = SYSCLK/4 (target 45.0 MHz) | | scope / logic analyzer on PC9 |
| | 1 | Flash / RAM usage | | `pio run` size report |

Planned metrics: control loop jitter (GPIO toggle + scope), I2C transaction
time, max step rate before stall, steps lost per 10 revolutions open- vs
closed-loop, CAN bus utilization at 50 Hz telemetry, worst-case ISR latency.
