# STM32 + ESP32 CAN-Based Closed-Loop Stepper Motor Controller

A heterogeneous two-node motor control system. An **STM32F4** node closes a
1 kHz position loop around a TMC2209 stepper driver and an AS5600 magnetic
encoder. An **ESP32** node supplies setpoints, drives the operator interface,
and bridges telemetry to Wi-Fi. The two nodes are coupled only by a
500 kbit/s CAN bus and an 8-byte frame contract.

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

## Node partitioning

|  | STM32F4 — motor node | ESP32 — supervisory node |
|---|---|---|
| **Role** | Hard real-time control | Connectivity and operator interface |
| **Owns** | TMC2209 step/dir pulse generation (timer hardware)<br>AS5600 sampling @ 1 kHz<br>Position PID<br>Stall / encoder-loss / timeout fault handling<br>CAN slave | Setpoint generation from potentiometer (ADC)<br>1602 LCD status and error display<br>MQTT telemetry uplink<br>Logging, OTA<br>CAN master |
| **Timing budget** | Microsecond jitter, non-negotiable | Millisecond, jitter tolerated |
| **Network** | None, by design | Wi-Fi |

### Why the split is drawn here

**Determinism.** The ESP32 Wi-Fi stack is an interrupt-hungry black box — RF
calibration, beacon handling, and TCP retransmission all preempt the CPU and
inject hundreds of microseconds to milliseconds of unpredictable latency.
Stretch the interval between two step pulses and the motor loses steps. A
bare-metal STM32 has nothing competing with its timer interrupt, so worst-case
latency can be both computed and measured.

**Fault isolation.** If Wi-Fi drops, the broker dies, or the ESP32 reboots, the
motor node must keep holding its last valid setpoint and then fall back to a
HOLD state after a 200 ms timeout. Putting a safety-relevant loop on the
networked chip would make a network failure a motor failure. Keeping
safety-critical function off the connected processor is standard practice in
automotive and robotics architectures.

**Peripheral fit over familiarity.** The F4 advanced timers offer hardware dead
time, encoder interface mode, and DMA-driven pulse trains — silicon designed
for motor control. The ESP32 offers a Wi-Fi/BLE stack the STM32F4 does not
have at all. Each part does what its hardware is best at.

**A single narrow interface.** The nodes share nothing but an 8-byte CAN frame
whose layout is frozen in `lib/can_proto/include/can_proto.h`, with static size
assertions so a silent padding change cannot corrupt the bus format. Either
side can be replaced or tested in isolation.

> This justification is a claim until it is measured. Phase 8 records
> GPIO-toggle interrupt-latency distributions for both processors — ESP32 with
> Wi-Fi active versus STM32 bare-metal — in `docs/measurements.md`.

---

## Hardware

| Part | Role | Interface |
|---|---|---|
| NUCLEO-F4 ×1 | Motor node (F446RE assumed — see `platformio.ini`) | — |
| ESP32 ×1 | Supervisory node (built-in TWAI = CAN 2.0B) | — |
| TMC2209 | Stepper driver (StealthChop defaults) | STEP/DIR/EN + UART |
| AS5600 | 12-bit absolute magnetic encoder | I2C @ 400 kHz |
| SN65HVD230 ×2 | CAN transceivers, one per node | CAN1 / TWAI TX/RX |
| 1602 LCD (I2C) | Operator display, ESP32 side | I2C |
| Potentiometer | Setpoint input, ESP32 side | ADC |
| NEMA-17 (1.8°) | Test motor | — |

Bus needs a 120 Ω terminator at each physical end. Both nodes share a ground.

---

## Architecture

```
      ESP32 supervisory node                  STM32F4 motor node
   +--------------------------+          +--------------------------+
   |  ADC knob -> setpoint    |          |  position PID @ 1 kHz    |
   |  1602 LCD status/error   |          |  step pulse gen (TIM)    |
   |  MQTT telemetry uplink   |          |  fault state machine     |
   +------------+-------------+          +----+----------------+----+
                |                             |                |
          [ TWAI + xcvr ] <== CAN 500k ==> [ CAN1 + xcvr ]     |
                |                                              |
             Wi-Fi                            [ TMC2209 ]  [ AS5600 ]
                                               TIM PWM      I2C DMA
```

Three layers, strictly separated on the STM32 side: **HAL/register access** →
**device drivers** (`lib/`) → **application logic** (`src/`). Drivers take
function pointers for their transport so they can be unit-tested on the host
without hardware.

---

## Phases

| # | Phase | Status |
|---|---|---|
| 1 | Toolchain + clock tree + LED blink (CubeMX baseline) | in progress |
| 2 | Register-level lowering of Phase 1; GPIO/RCC without HAL | planned |
| 3 | AS5600 over I2C; angle read, magnet health check | planned |
| 4 | TMC2209 step/dir via timer PWM; open-loop moves | planned |
| 5 | Position PID; closed-loop correction of lost steps | planned |
| 6a | CAN peripheral in loopback/silent mode — bit timing, filters, FIFO, ISRs validated with no second node and no transceiver | planned |
| 6b | Real bus: ESP32 TWAI node, setpoint/telemetry link | planned |
| 7 | Fault handling: stall, encoder loss, commander timeout | planned |
| 8 | Characterization: step response, stall recovery, bus load, interrupt-latency comparison | planned |

Phases 1–5 need only the STM32 board. Phase 6a validates the CAN stack in
isolation before a second node is introduced — one variable at a time.

Out of scope (explicitly cut): W25Q64 external flash, TMC2209 StealthChop
tuning via UART.

---

## Build

```bash
pio run -e node0          # motor node, normal operation
pio run -e canloop        # motor node, CAN loopback self-test (Phase 6a)
pio run -e node0 -t upload
pio device monitor -b 115200
```

ESP32 supervisory firmware lives in a separate tree (added at Phase 6b).

---

## Layout

```
├── platformio.ini        build environments
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
