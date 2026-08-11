# STM32 CAN-Based Closed-Loop Stepper Motor Controller

A two-node distributed motor control system built on STM32F446RE. One node
drives a stepper through a TMC2209 and closes the position loop with an AS5600
magnetic encoder; the second node issues setpoints and consumes telemetry over
a 500 kbit/s CAN bus.

> **Status:** Phase 1 (bring-up) in progress. This README documents intended
> scope; sections describing unbuilt phases are marked *planned*. Known issues
> stay visible rather than get papered over.

---

## Why this project

Closed-loop stepper control is the honest middle ground between open-loop
step/dir toys and full field-oriented control. The TMC2209 owns current
chopping, so the interesting engineering lives where it should for a firmware
portfolio piece: deterministic timing, a layered driver stack, a real fieldbus
with priority arbitration, and fault handling that has to survive a physically
stalled motor.

**This is not FOC.** There is no Clarke/Park transform and no dq-axis current
loop — the TMC2209 is a step/dir driver and phase currents are not directly
controlled. The AS5600 closes a *position* loop that corrects lost steps.

---

## Hardware

| Part | Role | Interface |
|---|---|---|
| NUCLEO-F446RE ×2 | Motor node + commander node | — |
| TMC2209 | Stepper driver (StealthChop defaults) | STEP/DIR/EN + UART |
| AS5600 | 12-bit absolute magnetic encoder | I2C @ 400 kHz |
| SN65HVD230 ×2 | CAN transceivers | CAN1 TX/RX |
| NEMA-17 (1.8°) | Test motor | — |

Bus needs a 120 Ω terminator at each physical end. Both boards share a ground.

---

## Architecture (planned)

```
        commander node                          motor node
   +---------------------+                +---------------------+
   |  setpoint generator |                |  position PID 1 kHz |
   |  telemetry logger   |                |  step pulse gen     |
   +----------+----------+                +----+-----------+----+
              |                                |           |
        [ CAN driver ]  <== 500 kbit/s ==> [ CAN driver ]  |
                                                           |
                                        [ TMC2209 ]   [ AS5600 ]
                                          TIM PWM       I2C DMA
```

Three layers, strictly separated: **HAL/register access** → **device drivers**
(`lib/`) → **application logic** (`src/`). Drivers take function pointers for
their transport so they can be unit-tested on the host without hardware.

---

## Phases

| # | Phase | Status |
|---|---|---|
| 1 | Toolchain + clock tree + LED blink (CubeMX baseline) | in progress |
| 2 | Register-level lowering of Phase 1; GPIO/RCC without HAL | planned |
| 3 | AS5600 over I2C; angle read, magnet health check | planned |
| 4 | TMC2209 step/dir via timer PWM; open-loop moves | planned |
| 5 | Position PID; closed-loop correction of lost steps | planned |
| 6 | CAN bring-up; two-node setpoint/telemetry link | planned |
| 7 | Fault handling: stall, encoder loss, commander timeout | planned |
| 8 | Characterization: step response, stall recovery, bus load | planned |

Out of scope (explicitly cut): W25Q64 external flash, TMC2209 StealthChop
tuning via UART.

---

## Build

```bash
pio run -e node0          # motor node
pio run -e node1          # commander node
pio run -e node0 -t upload
pio device monitor -b 115200
```

### Flashing two boards

Both Nucleos enumerate as ST-LINK. Pass the probe serial explicitly:

```bash
pio run -e node0 -t upload --upload-port <STLINK_SERIAL_A>
pio run -e node1 -t upload --upload-port <STLINK_SERIAL_B>
```

`NODE_ID` and the role macros are set per environment in `platformio.ini`, so
both boards run one source tree.

---

## Layout

```
├── platformio.ini        build environments, one per node
├── include/app_config.h  every tunable constant; no magic numbers elsewhere
├── src/main.c            entry point, clock config
├── lib/
│   ├── as5600/           encoder driver (transport-injected)
│   ├── tmc2209/          stepper driver
│   └── can_proto/        wire format + static size assertions
├── test/                 host-side unit tests
└── docs/                 measurements, scope captures, notes
```

---

## Known issues

Nothing measured yet — Phase 1 is not complete. This section gets populated
with real numbers, not omissions.

---

**Author:** Hongyi Mei · [github.com/hmei3-hash](https://github.com/hmei3-hash)
