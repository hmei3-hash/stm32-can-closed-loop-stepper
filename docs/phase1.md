# Phase 1 — Bring-Up from First Principles

**Board:** NUCLEO-F446RE (STM32F446RET6, Cortex-M4F)
**Goal:** LD2 blinks at exactly 1 Hz, and you can *prove* the core is running
at 180 MHz.

The blink is not the point. The point is that by the end of this document you
understand every line of `src/main.c` well enough to write it again from a
blank file with only the reference manual open. If you can't, the phase isn't
done.

---

## 0. Success criteria

Phase 1 is complete when all four are true:

1. `pio run -e node0 -t upload` succeeds and LD2 toggles.
2. You measure the blink period and it is 1.000 s ± 1%.
3. You output SYSCLK/4 on MCO2 (PC9) and a scope/logic analyzer reads 45 MHz.
4. You can explain what happens between the rising edge of NRST and the first
   instruction of `main()`.

Criteria 2 and 3 exist because a wrong clock config **still blinks**. It just
blinks at the wrong rate, and every timing-dependent thing you build in
Phases 3–8 will be silently wrong. Verify the clock now.

---

## 1. What happens before `main()`

On an ESP32 this is hidden by a second-stage bootloader. On bare-metal STM32
it is your responsibility, so you need the model.

### 1.1 The reset sequence

```
NRST released
      |
      v
Core reads 32-bit word at address 0x0000_0000  -> initial Main Stack Pointer
Core reads 32-bit word at address 0x0000_0004  -> address of Reset_Handler
      |
      v
PC = Reset_Handler
```

That is the entire hardware boot protocol. The Cortex-M does not "run a
bootloader" — it loads SP and PC from the first two words of the vector table
and starts executing. Everything after that is software you (or the framework)
provided.

Address `0x0000_0000` is an alias. On this chip, with BOOT0 tied low (the
Nucleo default), flash at `0x0800_0000` is aliased to `0x0000_0000`. So the
vector table the core reads is the one at the start of your `.elf`'s flash
image.

### 1.2 The vector table

It is a plain array of function pointers, in a known order, placed at the
start of flash by the linker script (section `.isr_vector`). Entry 0 is not a
pointer — it is the initial stack pointer value. Entries 1..15 are core
exceptions (Reset, NMI, HardFault, SVCall, PendSV, SysTick, ...), and from
entry 16 onward are the peripheral IRQs in the order given by the reference
manual.

This is why a typo in an ISR name silently breaks things: if you write
`void TIM2_IRQhandler(void)` instead of `TIM2_IRQHandler`, your function is
never placed in the table, the weak default handler stays, and your interrupt
appears to "not fire". There is no error. Remember this for Phase 4.

### 1.3 `Reset_Handler`

Provided by `startup_stm32f446xx.s`, which the `stm32cube` framework compiles
in for you. It does, in order:

1. Set SP (already loaded by hardware, re-asserted here).
2. Call `SystemInit()` — a C function in `system_stm32f4xx.c`. On F4 this
   mostly enables the FPU and sets the vector table offset register (`VTOR`).
   **It does not configure the PLL.** That is a common misconception; on F4
   the chip is still running on HSI at 16 MHz when `SystemInit()` returns.
3. Copy `.data` from flash to SRAM. Initialized globals
   (`int x = 5;`) live in flash as initial values and must be copied to RAM
   before any C code reads them.
4. Zero `.bss`. Uninitialized globals (`int y;`) are guaranteed zero by the C
   standard — that guarantee is implemented right here, by a loop.
5. Call `__libc_init_array()` — runs static constructors (relevant for C++).
6. Call `main()`.

If `main()` ever returns, the startup code branches to an infinite loop. There
is no OS to return to.

**Why this matters practically:** if you put a global variable's
initialization in a place that runs before step 3, it gets overwritten. And if
your linker script is wrong, step 3 copies the wrong bytes and you get
inexplicable garbage in globals with no crash.

### 1.4 The memory map you're linking against

| Region | Start | Size (F446RE) |
|---|---|---|
| Flash | `0x0800_0000` | 512 KB |
| SRAM1 | `0x2000_0000` | 112 KB |
| SRAM2 | `0x2001_C000` | 16 KB |
| Peripherals | `0x4000_0000` | — |
| Cortex-M internals (NVIC, SysTick, SCB) | `0xE000_0000` | — |

Total SRAM is 128 KB. The stack grows *down* from the top of RAM; the heap (if
you use one) grows *up* after `.bss`. When they collide you get silent
corruption, not an error. This is the main reason this project uses static
allocation only — see `app_config.h`, where every buffer size is a compile-time
constant.

PlatformIO prints a RAM/Flash usage summary after every build. Watch it.

---

## 2. The clock tree

This is the single hardest part of Phase 1 and the part with the most
catastrophic failure mode (chip halts, debugger can't attach). Read this
section twice.

### 2.1 Sources

The F446 can run from:

- **HSI** — internal 16 MHz RC oscillator. Always available, starts
  automatically at reset, ±1% accuracy over temperature. Fine for blinking,
  useless for CAN bit timing.
- **HSE** — external. Either a crystal, or an external square wave in
  *bypass* mode.
- **PLL** — multiplies HSI or HSE up to the operating frequency.

**On the Nucleo-64 board there is no crystal populated for HSE.** Instead, the
on-board ST-LINK MCU outputs an 8 MHz square wave (MCO) into the F446's
`OSC_IN` pin. Because it is a driven signal rather than a crystal, you must use
`RCC_HSE_BYPASS`, not `RCC_HSE_ON`. Selecting `HSE_ON` tells the chip to drive
an oscillator circuit that isn't there; HSE never becomes ready,
`HAL_RCC_OscConfig()` times out, and you land in `Error_Handler()`.

This is the #1 Nucleo bring-up mistake. It is why `main.c` says:

```c
osc.HSEState = RCC_HSE_BYPASS;   /* ST-LINK MCO, not a crystal */
```

and why `platformio.ini` declares `-DHSE_VALUE=8000000U`. `HSE_VALUE` is not
sensed by hardware — it is a number you *promise* the HAL. Get it wrong and
every computed baud rate, timer period, and delay is wrong by that ratio, with
no error message.

### 2.2 The PLL math

```
                 /M            xN              /P
  HSE 8 MHz --------> 2 MHz -------> 360 MHz -------> 180 MHz  SYSCLK
              PLLM=4      PLLN=180       PLLP=2
                                     \
                                      \  /Q (=7) -> 51.4 MHz  (48 MHz clocks)
```

Three constraints from the datasheet, all of which must hold:

| Constraint | Requirement | Our value |
|---|---|---|
| PLL input (after /M) | 1–2 MHz, 2 MHz recommended | 8/4 = **2 MHz** ✅ |
| VCO output (after xN) | 100–432 MHz | 2×180 = **360 MHz** ✅ |
| PLL output (after /P) | ≤ 180 MHz | 360/2 = **180 MHz** ✅ |

M is chosen to hit 2 MHz because that minimizes PLL jitter. Then N and P are
whatever gets you to the target.

**Known compromise:** PLLQ = 7 gives 360/7 = 51.43 MHz, not the 48 MHz that
USB/SDIO/RNG require. This project uses none of them, so it is fine. If you
ever add USB, the whole tree has to be re-derived (typically HSE 8 → M=8,
N=336, P=2 → 168 MHz, Q=7 → 48 MHz exactly — which is why so many F4 examples
run at 168 MHz rather than 180).

### 2.3 Bus prescalers

SYSCLK feeds AHB, which feeds APB1 and APB2. Each bus has a hard maximum:

| Bus | Max (F446) | Our divider | Result |
|---|---|---|---|
| AHB (HCLK) | 180 MHz | /1 | 180 MHz |
| APB1 (PCLK1) | 45 MHz | /4 | 45 MHz |
| APB2 (PCLK2) | 90 MHz | /2 | 90 MHz |

Exceeding an APB maximum does not throw an error. Peripherals on that bus just
misbehave. Set the prescalers *before* raising SYSCLK, which is what
`HAL_RCC_ClockConfig()` does internally.

**The timer clock doubling rule.** If an APB prescaler is not 1, the timer
clock on that bus is **2× PCLK**. So:

- APB1 timers (TIM2–TIM5, TIM6, TIM7, TIM12–14): 2 × 45 = **90 MHz**
- APB2 timers (TIM1, TIM8, TIM9–11): 2 × 90 = **180 MHz**

This is why `app_config.h` defines `APB1_TIMER_HZ` as 90 MHz while PCLK1 is
45 MHz. Getting this wrong makes every step pulse in Phase 4 exactly 2× off —
a bug that looks like a mechanical problem and wastes an evening. Write the
number down now.

Which peripheral is on which bus is in the reference manual's RCC chapter
(and in the `RCC_APB1ENR` / `RCC_APB2ENR` register bit lists). CAN1 and CAN2
are on APB1. I2C1 is on APB1. Note this for Phases 3 and 6.

### 2.4 Flash wait states — why this bricks boards

Flash memory cannot deliver an instruction every cycle at 180 MHz. The flash
interface inserts wait states. The number required depends on HCLK **and**
supply voltage:

At 2.7–3.6 V (the Nucleo runs at 3.3 V):

| HCLK | Wait states |
|---|---|
| ≤ 30 MHz | 0 |
| ≤ 60 MHz | 1 |
| ≤ 90 MHz | 2 |
| ≤ 120 MHz | 3 |
| ≤ 150 MHz | 4 |
| ≤ 180 MHz | **5** |

**The ordering rule is absolute:**

- Raising the clock → increase latency **first**, then switch.
- Lowering the clock → switch **first**, then decrease latency.

Get it backwards and the core fetches garbage instructions the moment the
clock rises. It will HardFault or hang somewhere unrelated, and because the
core is dead the debugger often can't attach either. `HAL_RCC_ClockConfig()`
handles the ordering for you — that is why you pass `FLASH_LATENCY_5` *to* it
rather than setting the register yourself. In Phase 2, when you lower this to
register level, you must implement the ordering by hand.

### 2.5 Voltage scaling and over-drive

To reach 180 MHz you need two things, in this order:

1. **VOS = Scale 1** (`PWR_REGULATOR_VOLTAGE_SCALE1`) — the internal regulator
   supplies the core at its highest voltage. Requires the PWR peripheral clock
   to be enabled first, which is why `main.c` calls
   `__HAL_RCC_PWR_CLK_ENABLE()` before touching it.
2. **Over-drive mode** (`HAL_PWREx_EnableOverDrive()`) — required for anything
   above 168 MHz on F446. It must be enabled *after* the PLL is locked and
   *before* SYSCLK is switched to it.

Omit over-drive and 180 MHz is out of spec: it may appear to work at room
temperature and fail when the board warms up. That class of bug is worse than
a hard failure. This is precisely why success criterion 3 (measure the actual
clock) exists.

### 2.6 Reading `main.c` again

Now re-read `SystemClock_Config()` in `src/main.c`. Every line should map to
something above:

```c
__HAL_RCC_PWR_CLK_ENABLE();                          // §2.5 step 1 prerequisite
__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_..._SCALE1);     // §2.5 step 1
osc.HSEState = RCC_HSE_BYPASS;                       // §2.1 ST-LINK MCO
osc.PLL.PLLM = 4; PLLN = 180; PLLP = DIV2;           // §2.2
HAL_PWREx_EnableOverDrive();                         // §2.5 step 2
clk.APB1CLKDivider = RCC_HCLK_DIV4;                  // §2.3
HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);          // §2.4
```

If any line is still opaque, stop and look it up in RM0390 before continuing.

---

## 3. Clock gating — the silent killer

Every peripheral on an STM32 has its clock **disabled at reset**. A peripheral
with no clock is not just idle: its registers do not exist on the bus. Writes
are discarded. Reads return 0. There is no fault, no warning, no log line.

```c
__HAL_RCC_GPIOA_CLK_ENABLE();   // without this, every GPIOA write is a no-op
```

This has no ESP32 equivalent — the IDF enables peripheral clocks inside its
driver init functions, so you never see it. On STM32 it is the single most
common "my code does nothing and I don't know why" cause.

**Debugging rule:** any time a peripheral seems dead, check the corresponding
`RCC_AHBxENR` / `RCC_APBxENR` bit *first*, before reading any other register.
In a debugger, read the peripheral's own registers — if they're all zero when
they should have reset values, the clock is off.

There is also a hardware subtlety: after setting an enable bit, the peripheral
needs a couple of cycles before its registers respond. The HAL macros already
insert a dummy readback for this. In Phase 2 you must add it yourself.

---

## 4. GPIO

To make PA5 an output, four registers are configured (all in `GPIOA`):

| Register | Field per pin | Our setting for PA5 |
|---|---|---|
| `MODER` | 2 bits: input / output / alternate / analog | `01` = output |
| `OTYPER` | 1 bit: push-pull / open-drain | `0` = push-pull |
| `OSPEEDR` | 2 bits: slew rate | low (LED doesn't care) |
| `PUPDR` | 2 bits: none / pull-up / pull-down | none |

Then to drive it:

- `ODR` — read/write the output data register.
- `BSRR` — write-only. Bits 0–15 set a pin, bits 16–31 reset it.

**Always prefer `BSRR` over read-modify-write on `ODR`.** `ODR |= (1<<5)` is
three operations (load, or, store) and is not atomic: an interrupt landing in
the middle that touches another pin on the same port will have its change
clobbered. `BSRR = (1<<5)` is a single store and is inherently atomic. This
matters in Phase 4 when the step pulse ISR and the application both touch
GPIO.

`HAL_GPIO_TogglePin()` uses `BSRR` internally. In Phase 2 you'll write the
register version yourself.

**Why PA5?** On Nucleo-64 boards, LD2 (the user LED) is wired to PA5 through a
current-limiting resistor. Confirm on the board schematic (UM1724) rather than
trusting any single source — pin assignments differ across Nucleo variants.

---

## 5. SysTick and `HAL_Delay()`

`HAL_Init()` configures **SysTick**, a 24-bit down-counter inside the Cortex-M
core (not a peripheral on any bus), to fire an interrupt every 1 ms. Its
handler increments a `uint32_t` tick counter.

`HAL_Delay(500)` reads that counter and spins until 500 ticks have elapsed.
Understand the consequences:

- It is a **busy-wait**. The CPU burns 100% of its cycles doing nothing. Fine
  for Phase 1, unacceptable from Phase 4 onward.
- It depends on `SystemCoreClock` being correct. If your PLL config and your
  `HSE_VALUE` disagree with reality, delays are wrong by exactly that ratio —
  which is the basis of verification method A below.
- If called from an ISR with priority equal to or higher than SysTick, it
  **hangs forever**, because the tick counter can never advance. This is a
  classic HAL deadlock; remember it when you write ISRs.

---

## 6. Build and flash

```powershell
cd C:\Users\hongy\Desktop\ee474\ee474\stm32-can-stepper-controller
pio run -e node0
pio run -e node0 -t upload
```

First build downloads the toolchain and STM32Cube F4 framework — several
minutes, once. What the `stm32cube` framework contributes:

- `startup_stm32f446xx.s` (§1.3) and the linker script (§1.4)
- CMSIS headers: `stm32f446xx.h`, which defines every peripheral as a struct
  overlaid on its base address — this is what lets you write `GPIOA->MODER`
- The HAL driver sources

**Where `main.c` sits in the layers:** application → HAL → CMSIS register
definitions → hardware. Phase 2 removes the HAL layer for GPIO and RCC and has
the application talk to CMSIS definitions directly.

Debugging:

```powershell
pio debug -e node0
```

Breakpoints, single-step, register inspection. Use it — this is the capability
you did not have on ESP32, and it is the fastest way to understand what the
clock config actually did (inspect `RCC->CFGR` and `SystemCoreClock` after
`SystemClock_Config()` returns).

---

## 7. Verifying the clock — do not skip

### Method A — blink period (no equipment)

Change the delay to `HAL_Delay(500)` in a toggle loop (already the case), so
the full on-off period is 1.000 s. Time 60 blinks with a phone stopwatch.
Expect 60 s ± 0.5 s.

If you get ~30 s, your actual clock is 2× what the HAL thinks. If ~120 s, it's
half. Either way the PLL config or `HSE_VALUE` is wrong.

This catches gross errors but not a 180-vs-168 MHz mistake. It is necessary,
not sufficient.

### Method B — MCO2 output (needs scope or logic analyzer)

The MCU can route an internal clock to a pin. **MCO2 is on PC9.** Configure it
to output SYSCLK divided by 4 and measure:

```c
/* Add at the end of GPIO_Init(), temporarily, for verification only. */
__HAL_RCC_GPIOC_CLK_ENABLE();
HAL_RCC_MCOConfig(RCC_MCO2, RCC_MCO2SOURCE_SYSCLK, RCC_MCODIV_4);
```

`HAL_RCC_MCOConfig()` configures PC9's alternate function for you. Expected
reading: **45.0 MHz**. If you see 42 MHz, you're at 168 MHz (over-drive
probably failed). If you see 4 MHz, you're on HSI at 16 MHz — the PLL never
engaged and something quietly fell back.

A 45 MHz square wave needs a scope with ≥100 MHz bandwidth to look clean, but
a cheap logic analyzer will still report the frequency correctly, which is all
you need. Divide by 8 or 16 instead if your instrument is slow.

Remove the MCO code once verified — PC9 is needed later, and a 45 MHz square
wave on a jumper wire is a fine antenna.

### Record it

Put the measured numbers in `docs/measurements.md`. Before-data is
unrecoverable once you move on, and Phase 2 rewrites this exact code at
register level — you will want the Phase 1 numbers to compare against to prove
the lowering didn't change behavior.

---

## 8. Failure modes, ranked by likelihood

| Symptom | Most likely cause |
|---|---|
| Hangs in `Error_Handler()` | `RCC_HSE_ON` instead of `RCC_HSE_BYPASS` (§2.1) |
| Blink rate off by exactly 2× | APB timer doubling misunderstood, or wrong PLLP (§2.3) |
| Blink rate off by a weird ratio | `HSE_VALUE` doesn't match the actual 8 MHz (§2.1) |
| Peripheral registers read as 0 | Clock gate not enabled (§3) |
| Works cold, fails warm | Over-drive not enabled at 180 MHz (§2.5) |
| Hard fault immediately after clock switch | Flash latency set after raising clock (§2.4) |
| Upload fails, "no ST-LINK" | USB cable is charge-only, or driver missing |
| Nothing at all, board seems dead | Try holding NRST, start upload, release — recovers a board stuck in a bad clock state |

That last one is worth internalizing: if you write a clock config that halts
the core before the debugger can attach, connect-under-reset is how you get
the board back. You are not going to brick it permanently.

---

## 9. Self-check

If you can answer these from memory, Phase 1 is done:

1. What are the first two 32-bit words the CPU reads after reset, and what
   does it do with each?
2. Why must `.bss` be zeroed in software when the C standard says those
   variables are zero?
3. Why `RCC_HSE_BYPASS` on a Nucleo and not `RCC_HSE_ON`?
4. Compute PLLM/N/P for 168 MHz SYSCLK from an 8 MHz HSE, with PLLQ giving
   exactly 48 MHz. Verify all three datasheet constraints.
5. APB1 prescaler is /4 and HCLK is 180 MHz. What frequency does TIM2 count
   at? What about TIM1?
6. Why does `ODR |= (1<<5)` have a race condition that `BSRR = (1<<5)` doesn't?
7. You enable a peripheral's clock and immediately write a config register.
   Why might that write be lost?
8. Why does `HAL_Delay()` hang if called from a high-priority ISR?

Question 4 is the one that matters most — if you can derive a clock tree from
constraints rather than copying one, you can bring up any STM32.

---

## 10. Then what

Phase 2 rewrites §3, §4, and the clock config at register level: no
`__HAL_RCC_*` macros, no `HAL_GPIO_*`, only `RCC->AHB1ENR`, `GPIOA->MODER`,
`GPIOA->BSRR`. The blink behavior must be bit-identical, verified with the same
MCO2 measurement. Same output, half the abstraction, and afterward you'll
actually know what the HAL was doing.

---

## References

- **RM0390** — STM32F446 reference manual (RCC, GPIO, FLASH, PWR chapters)
- **DS10693** — STM32F446xx datasheet (electrical limits, wait-state table)
- **UM1724** — STM32 Nucleo-64 user manual (LD2 wiring, ST-LINK MCO, solder bridges)
- **PM0214** — Cortex-M4 programming manual (vector table, SysTick, NVIC)

Always confirm pin assignments and electrical limits against these documents
rather than any secondary source, including this one.
