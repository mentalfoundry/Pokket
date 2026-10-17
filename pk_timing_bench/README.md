# pk_timing_bench

pk_timing_bench is a PocketStation app. It measures real hardware behavior that no available documentation records: memory-access cost, timer and interrupt timing, the CPU stop bit, and the two interrupt-line rates of the RTC. It operates on real hardware, and in [this emulator](../core). It uses Timer0 of the device as a stopwatch. It shows raw results on the 32 by 32 LCD.

This app supports the hardware model of this emulator with real-hardware evidence. A guess from documentation is not sufficient evidence. Use this app to compare the timing of a different PocketStation emulator against the same real-hardware behavior.

This app started with two questions about the memory-timing table in `core/src/memory.c`. It now covers nine. **Real retail hardware answers each one:**

| Question | The answer that real hardware gives | Screen |
|---|---|---|
| Does a BIOS ROM opcode fetch divide into 2 cycles for ARM and 1 cycle for Thumb, the same way as the documented behavior of FLASH? | Yes. | 1, 3, 4 |
| Does the `FLASH_CTRL` register window (`0x06000000` to `0x063FF`) get the fast 1-cycle data-access rate of WRAM, or the slow 2-cycle rate that each other region gets? | The fast rate. | 2 |
| Does a timer that is armed with period P take P ticks? | No. It takes P+1 ticks. | 6 |
| What does one interrupt cost? | Approximately 98 raw cycles. | 7 |
| How long does a timer expiry take to reach the arm operation in its handler? | 0 ticks, over IRQ, over FIQ, and with a full dispatch chain. | 8, 10, 11 |
| Does an MMIO write to `IRDA_DATA` cost more than a plain WRAM store? | No. The cost is identical. | 9 |
| Does bit 0 of `CLK control` (`0x0B000004`) stop the CPU? | Yes, and the timers stop with it. | 13 |
| Are the documented RTC interrupt-line rates waveform rates or transition rates? | Waveform rates, at 1Hz and 4096Hz. | 14 |
| Is `CLK_MODE 7` really 3,997,696Hz? | Yes. | 14 |

Those answers corrected `core/src/memory.c`, `timer.c`, `rtc.c`, and `clk.c` in this emulator. Each correction is a real behavior difference, and not a tuning value.

**While this app runs, the device does not answer the PS1.** The first instruction of the app masks each interrupt source, and the kernel answers the PS1 only from the interrupt of the COM block. Thus a PS1 sees no memory card in the slot while this app runs, on each screen. A launch from the PS1 side, with the BU command `59h`, starts the app, but the device stops its answers when the app starts. To give the link back to the kernel, leave the app.

[VERIFICATION.md](VERIFICATION.md) records the confirmed real-hardware value of each screen, next to the value that this emulator gives. Use it to compare a different unit, or a different emulator, against a known-good result. See the "Memory access timing" section in [docs/hardware-notes.md](../docs/hardware-notes.md), and [docs/app-notes.md](../docs/app-notes.md), for the full writeup of the memory-timing result. "Other real-hardware findings" below gives the results that are not screen values.

## Building

This build needs the [GNU Arm Embedded Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads). That toolchain supplies `arm-none-eabi-as`, `arm-none-eabi-ld`, and `arm-none-eabi-objcopy`. The zip package needs no installer, and it needs no administrator rights. Extract it and use it directly. This build also needs any C compiler, for the small packer program:

```
make
```

`make` writes `pk_timing_bench.mcs` into this directory. The file is 16512 bytes. It holds a real PS1 single-save directory frame, and a PSX Title Sector app of two 8KB blocks. The first block holds screens 1 to 18. The second block holds only the code of screens 19 to 29 (`src/cpu.s`). Thus each measurement routine of the first block has the same address as in the build of one block. Load it with any frontend of this project, through `psemu_load_content`. The memory-card-loading pipeline of a real PocketStation also loads it.

**Link the objects in the exact `$(OBJS)` order of the Makefile** (`header icon mirrors font thumb_loop start helpers experiments ui`). Do not pass `src/*.o` to the toolchain by hand. The shell expands that pattern alphabetically. It then links `experiments.o` first, and it pushes `_start` several hundred bytes further into `.text`. The fixed-offset sections in [`link.ld`](link.ld) still land correctly, and the entry-point word at body `0x5C` still points at the real `_start`. Thus the build is successful, and the app operates. But the layout of that image is very different from the layout that `make` gives. Thus a byte comparison against a known-good build is not possible. `_start` must come immediately after `.thumb_loop`.

This build needs no Python, and it needs no assembler-specific scripting. The real ARM source and Thumb source are in [`src/`](src/), as editable GNU-syntax assembly. [`link.ld`](link.ld) puts each piece at the exact body offset that the runtime logic needs. [`pack.c`](pack.c) is the only code that is not assembly. It puts the assembled and linked binary into the PS1 directory-frame header. If the toolchain binaries are not on `PATH`, give `AS`, `LD`, `OBJCOPY`, or `CC` on the `make` command line.

`make BLOCKS=3` makes an app of three blocks. The third block holds only the target of probe 10 on screen 29. Probes 10 to 13 then show if the abort of an access in FLASH1 occurs at the end of the app, or at a fixed address. Do `make clean` before a change of `BLOCKS`. The prebuilt file is the default build of two blocks.

This directory already holds a prebuilt `pk_timing_bench.mcs`. The toolchain is necessary only for a change to the source.

## Two separate icons

This app has two independent icons. Two different things read them:

- **The browse-screen icon of the PocketStation.** This is a direct 32 by 32 1bpp bitmap, at Title Sector body offset `0x100`. The LCD of the device shows this icon at a browse operation to this file, before the launch. See [`src/icon.s`](src/icon.s). See the "Browse-screen icon/graphic" section in `docs/app-notes.md` for the method that gave this format.
- **The standard PS1 memory-card icon.** This icon has a 16-color BGR555 palette at body offset `0x60`, and then a 16 by 16 4bpp bitmap at offset `0x80`. The memory-card manager of a real PS1 draws this icon. A PC-side memory-card management tool also draws it. The PocketStation never reads this icon.

  A byte-for-byte comparison gave this format. That comparison put the output of a real icon-embedding tool against the build of this project. Only 27 bytes were different, and each one is in the range `0x60` to `0xFF`. That result confirmed the format. The icon-frame-count byte of this project, at body offset `0x02`, was already `0x11`, which means "1 frame, no animation". That byte is also the standard field that this icon format uses as a gate.

[`icon_convert.c`](icon_convert.c) builds the standard PS1 icon automatically, from [`assets/card_icon.bmp`](assets/card_icon.bmp). That file is a 16 by 16, 24-bit, uncompressed BMP. `make` runs `icon_convert.c` before it assembles `header.s`.

To change the icon, edit or replace `assets/card_icon.bmp`. Then build again. The file must stay 16 by 16, 24-bit, and uncompressed. It must use 16 distinct colors or fewer. The converter gives a clear error message if the file does not obey these rules.

`icon_convert` takes the top-left pixel as the background color, at palette index 0. This agrees with the convention in a real reference icon.

## Controls

This app runs its startup measurements one time, at power-on. A person then pages through the result screens by hand. Screens 13, 15, and 29 also use UP and DOWN. Each of those screens gives its own buttons below.

- **RIGHT**: the next screen
- **LEFT**: the previous screen
- The screens wrap around, from 1 to 29 and then 1 again. Screen 12 is not in that cycle, on purpose: it is the CONTINUE/EXIT prompt, and only a hold on ACTION opens it. Thus LEFT from screen 1 goes directly to screen 29. No screen stops the device. See "Screens 19 to 28" below.
- **A hold on ACTION** opens a CONTINUE/EXIT prompt. That prompt returns to the system without a hardware reset. UP selects CONTINUE, DOWN selects EXIT, and a new ACTION press confirms the selection. See "Screen index used for the continue/exit prompt" in `src/constants.inc`, and `pb_prompt_confirm_exit` in `src/ui.s`, for the full behavior. Screens 13 and 15 each have one condition in which the hold does not operate. See those screens below.
- **The physical reset button** starts the device again if nothing else operates. This app writes no flash, thus a reset loses only the results on the screens.

## What each screen shows

Screens 1 to 4 use the same layout:

- Top-left: one digit, from 1 to 5. This digit gives the screen number.
- The middle row of 8 hex digits: the "test" raw tick count.
- The bottom row of 8 hex digits: the "control" raw tick count.

Each value is the number of Timer0 ticks across 30000 (`0x7530`) iterations of a tight loop. This is a raw elapsed-time count. It is not a delta with a subtraction already applied. A larger number shows a slower operation.

**Screen 1, the sanity check. Read this screen first.** The test value (top) is the ARM-mode loop delta. The control value (bottom) is the Thumb-mode loop delta.

Both loops do exactly the same operation. They read a fixed WRAM address, in a tight branch-back loop, with N=30000. The only difference is the encoding of the instructions of the loop, which is ARM or Thumb.

This screen derives a documented fact again. A FLASH opcode fetch costs 2 cycles for each instruction in the ARM mode, and 1 cycle for each instruction in the Thumb mode.

The top value is clearly larger than the bottom value if the measurement method is correct. The loop overhead and the call overhead dilute the ratio well below a clean 2:1. Real hardware gives approximately 1.7:1. See [VERIFICATION.md](VERIFICATION.md) for the raw values.

Do not trust screens 2 to 4 if screen 1 gives a ratio near 1:1, or a different unexpected ratio. Something in the measurement technique does not behave as expected on that unit. Examine the Timer configuration, `CLK_MODE`, and interrupt interference.

**Screen 2, the data-access cost of `FLASH_CTRL` against WRAM.** The test (top) reads `FLASH_CTRL+0x100` (`F_BANK_VAL[0]`) 30000 times. The control (bottom) reads a WRAM scratch address 30000 times. The two numbers come back close or equal if `FLASH_CTRL` gets the fast rate of WRAM.

**Screen 3, a real BIOS ARM helper against a WRAM copy of the same code.** The test (top) calls the "get selected app slot" helper of the real BIOS, at `0x04001BC8` in BIOS ROM, 30000 times. The control (bottom) calls an identical instruction sequence in WRAM 30000 times. The top value comes back clearly larger than the bottom value if a BIOS opcode fetch agrees with the documented 2-cycle ARM rate of FLASH.

**Screen 4, a real BIOS Thumb helper against a WRAM copy.** This screen uses the same method as screen 3. It uses a BIOS routine in the Thumb mode in place of the ARM mode: the directory-frame-marker check, at `0x04001320`.

**Screen 5, the raw Timer0 diagnostic.** This project added screen 5 after screens 3 and 4 returned `0xFFFFxxxx` values on real hardware. Those values were a Timer0 wraparound artifact, and a 16-bit mask corrected them. Screen 5 shows four rows of raw Timer0 snapshots, and not deltas. Those rows are the values before and after one isolated real BIOS call, and then the values before and after the full 30000-iteration measurement that screen 3 runs.

The sanity check for this screen is `(full_before − full_after) mod 65536`. That value must equal the test value of screen 3 exactly. Real hardware and this emulator both give `0x802D`. Thus the raw counter data is internally consistent on both sides.

**Screen 6: does a timer that is armed with period P take P ticks?** Both rows here are raw Timer0 stopwatch totals. They are not a test and control pair, which is the layout of screens 1 to 4. The top row is Timer2 armed with **period 1016**, measured across **256** reloads. The bottom row is **period 2032** across **128** reloads. Both rows use the `/2` divisor of Timer2, and both take **no interrupts at all**. A poll of the count register of Timer2 finds each reload. See `measure_timer_periods` in [`src/helpers.s`](src/helpers.s).

The reason for 1016: that is the exact period that the serial-carrying app arms Timer2 with while it transmits. Its nominal IR pulse unit is 1200, and it subtracts a fixed 184 before the arm operation. This screen was built to find the source of those approximately 184 ticks, at a time when the transmitted pulse of this emulator measured only approximately 1041, which is approximately 13% short.

Both rows measure the same total number of Timer2 ticks, because 1016 x 256 = 2032 x 128. Thus a correct timer makes **both rows read the same value**. That property makes the two rows a discriminator:

| Screen 6 result | Meaning |
|---|---|
| Both rows `0x3F80` | The timer is exact. A period of P takes P ticks. Thus the missing approximately 184 ticks occur *after* the expiry, in the interrupt path. That path is the exception entry, the BIOS and kernel dispatch, and the prologue of the handler of the app. |
| Top `0x4B00`, bottom `0x4540` | A fixed **additive** cost for each period. The timer block takes P plus approximately 184 ticks. That result is a timer-model fault in this emulator, and it is a much easier correction. |
| Both rows `0x4B00` | A **proportional** rate error. The `/2` divisor, or the clock that feeds it, is incorrect. It is not a fixed cost. |

Real hardware gave `0x3F90` and `0x3F88`, which is the second condition at a much smaller scale: exactly 1 extra tick for each period. `core/src/timer.c` now has that correction, and this emulator returns `0x3F91` and `0x3F88`. See [VERIFICATION.md](VERIFICATION.md) for the confirmed values.

**Screen 7: what does an interrupt cost?** Screen 6 gives only 1 tick of the approximately 184 ticks that the serial-carrying app compensates for. This screen tests the next candidate for the remainder, which is the interrupt entry, the kernel dispatch, and the prologue of the handler.

**The method.** Time exactly the same measurement loop two times. The first run masks each interrupt, and it gives the top row. The second run keeps one timer interrupt live at a known fixed rate, and it gives the bottom row. Each interrupt takes its full cost from the loop. Thus the two totals together give the cost of one interrupt. Timer1 is armed at period 99 with the `/32` divisor. Thus it fires every `(99+1)*32 = 3200` raw cycles.

**How to read it.** `B` is the top row, and `W` is the bottom row. Both are in Timer0 ticks:

```
per-interrupt cost (raw cycles) = 3200 * (1 - B / W)
```

The interrupt count is not a fixed number. The loop runs longer while interrupts interrupt it, thus more interrupts fit inside it. The formula above already includes that effect.

| | B (masked) | W (one IRQ live) | cost of one interrupt |
|---|---|---|---|
| This emulator | `0x2BF2` | `0x2D4E` | 96 raw cycles |
| Real hardware (measured 2026-07-31, see [VERIFICATION.md](VERIFICATION.md)) | `0x2BF2` | `0x2D56` | **98 raw cycles** |
| The prediction, if the 184-tick compensation of the app were entry cost | `0x2BF2` | approximately `0x31A8` | 368 raw cycles |

**Real hardware removes this hypothesis.** The interrupt entry, dispatch, and return cost of this emulator is 2.2% under hardware, and not approximately 4 times too cheap. Thus this path is essentially correct, and it is **not** the place where the missing IR time goes.

**This project met two faults during the build of this screen. This emulator caught both faults before any run on hardware.** Either fault stops a real unit, and it gives no measurement:

1. A timer interrupt that is enabled with no app IRQ callback registered corrupts the app visibly. The kernel needs a callback first. Thus experiment 7 registers a handler before it unmasks anything. It registers through `SWI 1`, which is the call that a real app uses. A disassembly of the interrupt setup of that app found the call.
2. The registration helper returned with `pop {..., pc}` at first. On an ARMv4T core, a Thumb POP into the PC does not change the instruction set. Thus the code returned into ARM caller code while it was still in the Thumb state. It then caused an `unrecognized thumb opcode 0xEBFF` fault. `0xEB` is the highest byte of the ARM `BL` instruction at that address. The helper now returns with `BX`, the same way as `measure_loop_ptr_thumb`.

The contract of `SWI 1` comes from that disassembly. It has no documentation. `r0` is the callback slot, `r1` is the handler address, and a value of 0 removes the callback. That contract is useful if screen 7 behaves incorrectly on a unit. Experiment 7 masks each interrupt source again, and it restores Timer1, before it returns. Thus nothing after it operates with a live interrupt.

An earlier version put this experiment behind a hold on UP at power-on, as protection against a stop on a unit. This project removed that gate. The gate read a live button *level* from `INTC_STATUS` at startup. Real hardware gives that level, but this emulator only approximates it. Thus the gate behaved differently in the two. A hold on a direction button through the launch also disturbs the menu navigation of the BIOS. Both failure modes above now have a real correction, thus the protection gave nothing.

**Screen 8: how long does a timer expiry take to reach the arm operation in its handler?** The top row is the Timer0 stopwatch total across **64** Timer1 periods. The bottom row is the period that each one was armed with, which is `0x3F8` = 1016.

Screens 6 and 7 each removed a candidate for the approximately 184 ticks that the serial-carrying app compensates for, and neither one explained it. A trace of that app shows the part that the earlier screens missed. Its transmit handler **arms the timer again at each interrupt**. It does not let the timer run free.

That difference is the reason for this screen. A free-running timer keeps its period however late the handler runs. Thus the interrupt latency cancels, and it never appears in the pulse. Under the theory that this screen tests, a re-armed timer does not start its next period until the handler reaches the arm operation. Thus the latency adds to *each* period. That theory explains why the app arms 1016 and expects 1200: it budgets 184 ticks for the path from the expiry to the arm operation.

Timer1 is armed here with the same 1016 and the same `/2` divisor that the real app uses. Its handler arms it again with the same value.

**How to read it.** `D` is the top row:

```
effective period (Timer1 ticks) = D * 32 / 64 / 2
expiry-to-arm latency           = effective period - 1017
```

The value is 1017, and not 1016, because a timer that is armed with P runs P+1 ticks. See screen 6.

| | top row | effective period | latency |
|---|---|---|---|
| This emulator | `0x0FE4` | 1017 | 0 ticks |
| Real hardware (measured 2026-07-31, see [VERIFICATION.md](VERIFICATION.md)) | `0x0FE4` | 1017 | **0 ticks** |

**Real hardware removes this hypothesis.** The latency from the expiry to the arm operation measures 0 ticks on real hardware. It is not the approximately 184 ticks that the hypothesis needs. The handler of this screen arms the timer again with a write of `period` and `count`, and `count` ignores writes (screen 29, probe 22). Thus the timer continues from its reload at the expiry. This screen does not test the re-arm method of the real app, which disables the timer and enables it again. The enable edge loads `count` from `period`, thus that method starts the period again. See "Timing measurements on real hardware" in [docs/hardware-notes.md](../docs/hardware-notes.md) for the full table of screen results.

**Screen 9: does an MMIO write to `IRDA_DATA` cost more than a plain WRAM store?** The top row is a loop of **30000** stores to `IRDA_DATA` (`0x0C800004`). The bottom row is the same loop with the store to a WRAM scratch address. This screen uses the same two-row test and control layout as screens 1 to 4. It uses the same method that screen 2 uses for `FLASH_CTRL` against WRAM, but it times a store and not a load.

Screens 6, 7, and 8 each removed a general interrupt-path or timer-path candidate for the approximately 184-tick IR pulse-width shortfall. See screen 8 above. None of them cover the work of the real transmit handler. The one MMIO write on the frequent path of that handler is `IRDA_DATA`, at the LED bit, which the handler changes at each pulse edge. This screen measures the cost of that write directly.

This emulator gives `IRDA_DATA` the same general 2-cycle "I/O" data-access rate as each region outside WRAM and `FLASH_CTRL`. See "Memory access timing" in [docs/hardware-notes.md](../docs/hardware-notes.md). BIOS, FLASH, and VRAM get that same rate.

| | test (`IRDA_DATA`) | control (WRAM) |
|---|---|---|
| This emulator | `0x2BF2` | `0x2848` |
| Real hardware (measured 2026-07-31, see [VERIFICATION.md](VERIFICATION.md)) | `0x2BF2` | `0x2849` |

**Real hardware agrees with this emulator, within the jitter of 1 tick.** Thus the write cost of `IRDA_DATA` is not the missing time either. With screens 6 to 8, each general memory-access candidate and interrupt-path candidate that this project can measure is now measured, and each one agrees with real hardware.

**Screen 10: is the latency from the expiry to the arm operation different over FIQ than over IRQ?** This screen uses the same layout and the same arithmetic as screen 8. It uses Timer2, which routes to FIQ, in place of Timer1, which routes to IRQ.

A disassembly trace of the *real* IR transmit handler, and not a synthetic one, shows that the handler runs on FIQ. Timer2 connects to FIQ directly. See `INT_FIQ_MASK`, and "Interrupt controller" in [docs/hardware-notes.md](../docs/hardware-notes.md). The FIQ vector (`0x1C`) reaches the real handler. The CPSR mode of `0x11`, at the exact point of its `IRDA_DATA` write, confirms this path. Screens 7 and 8 measured only IRQ. The exception-entry cost of FIQ had no measurement on real hardware. This project only assumed that it is identical to the cost of IRQ.

**This screen exists because of a real fault that this project found during its build. That fault never shipped.** The first version used `register_irq_handler` for Timer2. That helper is the `SWI 1` slot 1 call of screens 7 and 8. In this emulator, that use stopped the app: `pc` stayed inside the FIQ vector handler of the BIOS with no exit. A disassembly of a real BIOS ROM dump gives the reason. The IRQ vector handler and the FIQ vector handler read their app-registered callback from two *different* fixed RAM slots. Those slots are `0xFC` for IRQ, and `0x100` for FIQ. With no callback at `0x100`, nothing acknowledged the HOLD bit of Timer2. Thus FIQ asserted again immediately after each return of the handler, with no exit. The correction is `register_fiq_handler` in `src/thumb_loop.s`, which is `SWI 1` with slot 2 in place of slot 1. A comment in a real app already documents that same mechanism (`movs r0,#2; ... svc #1` installs a FIQ callback), and this project had never used it before. **Use `register_fiq_handler`, and not `register_irq_handler`, in a variant of this experiment. The incorrect one stops the device, and only the physical reset button recovers it.**

| | top row | effective period | latency |
|---|---|---|---|
| This emulator | `0x0FE4` | 1017 | 0 ticks |
| Real hardware (measured 2026-07-31, see [VERIFICATION.md](VERIFICATION.md)) | `0x0FE4` | 1017 | **0 ticks** |

**On real hardware, FIQ costs the same as IRQ.** The pair `0x0FE4` and `0x03F8` is the exact raw pair that screen 8 read for IRQ. With screens 6 to 9, each general memory-access cost and interrupt-path cost that this project can measure now agrees with real hardware. See "Timing measurements on real hardware" in [docs/hardware-notes.md](../docs/hardware-notes.md) for the full table of screen results.

**Screen 11: what does the FULL dispatch chain of the real transmit handler cost?** This screen measures more than a minimal arm operation. It uses the same layout and the same arithmetic as screens 8 and 10.

Screens 8 and 10 both measured a *minimal* arm operation. That operation is a minimal handler, one interrupt, a write to two timer registers, and a return. Both gave 0 ticks on real hardware. But a disassembly trace of the real transmit handler shows that its actual dispatch is not minimal. That handler acknowledges its own interrupt sources. It then calls through a jump table, with the INTC bit as its index. It then calls a nested subroutine, which reads a state flag. It then calls a second subroutine, which changes from ARM to Thumb with an interworking `BX`. It arms Timer2 again after those steps. A direct measurement in this emulator gives a steady-state cost of 128 to 160 Timer2 ticks. `tools/ir_probe.c` made that measurement, and not `pk_timing_bench`. It timed that real dispatch sequence across a real transmit burst of the serial-carrying app. That cost is most of the 184-tick value of the app.

This screen reproduces that same shape as a real `pk_timing_bench` experiment. That shape is an acknowledge, a nested ARM call, an ARM-to-Thumb transition, and then the arm operation. Thus a person can measure it on real hardware, the same direct way as screens 8 and 10. The measurement is not only possible in the emulator. This screen is original homebrew code, and not a copy. Thus it is not byte-for-byte identical to the real transmit handler, and an exact match to 128 to 160 ticks is not expected.

| | top row | effective period | latency |
|---|---|---|---|
| This emulator | `0x0FE3` | 1016.75 | 0 ticks, within the jitter |
| Real hardware (measured 2026-07-31, see [VERIFICATION.md](VERIFICATION.md)) | `0x0FE4` | 1017 | **0 ticks** |

**Real hardware agrees with screens 8 and 10 exactly, bit for bit, a third time.** Real hardware gives 0 added ticks, also with the full dispatch chain in the handler. Probe 22 of screen 29 gives the mechanism: `count` ignores writes. The handler writes `period` and `count` to a running timer, thus the timer continues from its reload at the expiry, and the arm write does not start a new period. Thus screens 8, 10, and 11 do not measure the dispatch cost, and they do not test the re-arm method of the real app, which disables Timer2 and enables it again. See "Timing measurements on real hardware" in [docs/hardware-notes.md](../docs/hardware-notes.md), and the open question about the `184` figure in "Known open questions and unconfirmed behavior".

### Screen 14: the two interrupt-line rates of the RTC

This screen executes at startup, the same as screens 1 to 11. It is the reason that the startup now needs a few more seconds. One transition of a running RTC *is* one second. Thus a measurement of two transitions costs two real seconds.

**The reason for this screen.** The available documentation gives the interrupt line of the RTC as "approximately 1Hz" while the clock runs, and as "approximately 4096Hz" while the clock is paused. The paused state is mode bit 0, `PRGSEL`. The BIOS uses that state, thus `RTC_ADJUST` can change one field while the clock stays constant. Neither figure had a measurement on hardware, and the word "approximately" carries real weight in that sentence. This emulator derives its paused rate as exactly 4096 times its running rate, in `RTC_TICK_CYCLES_PAUSED` (`core/src/rtc.h`). A different true ratio mistimes each `RTC_ADJUST` wait in the BIOS. This screen confirms the ratio of 4096.

The running rate is important for a different reason. It makes an emulated second last a real second. The constant behind it was 3.79 times incorrect for a long time, and the clock of the emulated device lost approximately 45 minutes each hour.

**The method.** This screen polls the RTC bit of `INT_STATUS`, and it times a fixed number of transitions against Timer0. It unmasks no interrupt, and it takes no interrupt. The status register reports the raw signal level, thus a poll can watch the line directly.

**Two rows of raw Timer0 ticks**, and not rates:

| row | what it timed | Timer0 divisor |
|---|---|---|
| top | 256 transitions, RTC **paused** | /32, which is the value that `start.s` leaves |
| bottom | 4 transitions, which is 2 full pulses, RTC **running** | /512 |

The two rows use different divisors deliberately. A running pulse is a full second, which overflows the real 16-bit count of Timer0 at /32. The run-mode row also discards one pulse before it counts, because the first pulse after the exit from program mode can be a partial pulse. This screen restores Timer0 afterwards, because that timer is the stopwatch of each other screen.

**The arithmetic.** The app sets `CLK_MODE 7`. Thus the CPU runs at 3,997,696Hz, and Timer0 ticks at that rate over its divisor: 124,928 each second at /32, and 7,808 each second at /512. A *waveform* Hz is one half of the transition rate, because a pulse is two transitions:

- **paused Hz** = 128 / (top / 124,928)
- **running Hz** = 2 / (bottom / 7,808)

**The real-hardware result is 4096Hz paused.** Two measurements gave `0x0F40` and then `0x0F3F`. Those two values are one tick apart, which is the resolution of this measurement. See [VERIFICATION.md](VERIFICATION.md). That is 256 transitions in 0.031250 seconds, to the tick. That result settles two things at one time. The documented 4096Hz is a waveform rate, and not a transition rate. And `CLK_MODE 7` really is 3,997,696Hz, which is a figure that the timing table had only ever taken from documentation.

**The control run in this emulator.** The top value is `0x0F44`, which is 8184 transitions each second. That value is 0.1% under hardware, from integer rounding in `RTC_TICK_CYCLES_PAUSED`. The bottom value is `0x3D00`, which is 1.0000Hz.

**The bottom row is settled too.** Hardware returns `0x3D00`, which is four transitions in exactly 2.000 seconds. That is a **1Hz waveform**, which is again exactly the documented figure. An earlier build measured one pulse with no discard, and it read 11% fast. The cause was a divider that resynchronized immediately after the exit from program mode. A discard of one pulse removes that effect completely.

**Thus both documented RTC figures are confirmed, and both are waveform rates.** See [VERIFICATION.md](VERIFICATION.md) for the confirmed values.

### Screen 13: does bit 0 of CLK control (0x0B000004) stop the CPU?

Each other screen here reports a number that it measured at startup. This screen is interactive. **UP selects the next mode, and DOWN runs the selected mode.** The digit after the `D` at the top is the selected mode, in hex: mode 10 shows `A`, mode 11 shows `B`, and mode 12 shows `C`. Mode 0 is the stop test that the paragraphs below describe. The measured quantity is the time that the CPU stayed stopped. On a device with no other input, only a button press from a person ends that time.

**The reason for this screen.** A real commercial app arms an idle timer. When that timer expires and no button is down, the app writes `1` to this register. That write is the last step of a power-down sequence. The earlier steps turn the sound off, mask the RTC interrupt, and turn the display off. Real hardware sleeps at that point, which a test confirms: the screen blanks, and a button press brings it back. But *that this register is the register that stops the CPU* was an inference from the writes next to it. This screen tests the write alone, with no other part of that sequence. No quantity of tracing of the real app can give that separation. See "CLK control" in [docs/hardware-notes.md](../docs/hardware-notes.md).

**The real-hardware result confirms the inference.** With a wait of approximately 10 seconds, the top row read `0x0000000A`, and the middle row read `0x00000000`. Thus bit 0 of this register stops the CPU, and the timers stop with it. Timer1 was armed and unmasked for the full ten seconds, and it counted zero interrupts. That second part matters as much as the first. A wake on each asserted interrupt is not a stop at all, because a running timer asserts again in microseconds. See [VERIFICATION.md](VERIFICATION.md).

**What it does.** It registers an interrupt handler. It arms Timer1 slowly, at approximately 8Hz. It unmasks the buttons and Timer1. It draws a solid bar across the middle of the screen. It then makes one 32-bit store of `1` to `0x0B000004`. Each step after that store runs only when the CPU runs again. This screen leaves the LCD **on** deliberately, which is different from the sequence of the real app. Thus the result is readable. The handler counts the Timer1 interrupts before the store and after the store separately. See `stop_test_phased` in [`src/cpu.s`](src/cpu.s).

**The modes.** Each mode keeps its rows, and the screen shows the rows of the selected mode. Modes 0 to 4, 6, 7, 10, and 11 arm Timer1 the same way, draw the same bar, and make the same store.

| mode | function in [`src/cpu.s`](src/cpu.s) | unmasked | rows |
|---|---|---|---|
| 0 | `stop_test_phased` | the buttons and Timer1 | the four rows below |
| 1 | `stop_test_masked` | ACTION | four samples `CH00NNNN` |
| 2 | `stop_test_instr` | ACTION | the Timer1 count in four sequential instructions |
| 3 | `stop_test_capture` | ACTION and Timer1 | a record of the first two interrupts |
| 4 | `stop_test_capture` | the buttons and Timer1 | a record of the first two interrupts |
| 5 | `stop_test_held` | none | the hold bit of a held button. This mode does not stop the CPU. |
| 6 | `stop_test_instr` | ACTION | the same as mode 2, with a `MOV` of 1 cycle after the store |
| 7 | `stop_test_instr` | ACTION | the same as mode 2, with 16 `MOV`s after the store |
| 8 | `stop_test_unmask` | DOWN, then none | the hold bit of a held button after a mask with no acknowledge. This mode does not stop the CPU. |
| 9 | `stop_test_release` | DOWN | the hold bit of a button after a release with no acknowledge. This mode does not stop the CPU. |
| 10 (`A`) | `stop_test_window` | ACTION | four stops, with 1, 2, 3, and 4 `MOV`s before one read of Timer1 |
| 11 (`B`) | `stop_test_wram` | ACTION | four stops: three from WRAM, and one with a `MOV` of 3 cycles |
| 12 (`C`) | `stop_test_rtc` | none | the RTC request at each edge of the RTC line. This mode does not stop the CPU. |

**Mode 1, the masked mode.** Timer1 stays masked, and the handler acknowledges only the buttons. Thus a Timer1 request stays in the hold register of the interrupt controller, and the screen reads it. Each of the four rows is one sample, in the form `CH00NNNN`: `C` is the Timer1 control (`6` = /512 and enabled), `H` is the Timer1 bit of the hold register (`1` = a request), and `NNNN` is the Timer1 count. Row 1 is before the test changes Timer1, row 2 is immediately after the arm and an acknowledge, row 3 is immediately before the store, and row 4 is immediately after the wake. This emulator gives `0000035B`, `600003E7`, `600003DB`, and `61000222`. Row 4 shows a request, because Timer1 counts while ACTION stays down after the wake, the same as on real hardware.

**Mode 2, the instruction count.** The same as mode 1, but the rows are the Timer1 count in the instruction before the store (row 1), and in each of the first three instructions after the store (rows 2 to 4). Each one is an `LDR` of the count. While ACTION stays down after the wake, the CPU executes only the interrupt handler, and Timer1 counts. Thus a read after the stop gives a different count from a read before the stop. The first row that shows that difference is the first read after the stop, and the rows before it executed before the stop. This emulator gives `3DB`, `3DB`, `223`, and `223`, the same pattern as real hardware.

**Mode 6, one instruction or a number of cycles.** The same as mode 2, but the first instruction after the store is `MOV r0, r0`, which takes 1 cycle. Rows 2 to 4 are the reads in the second, third, and fourth instructions after the store. If row 2 is equal to row 1, the CPU executes more than one instruction after the store. If row 2 is different, the CPU executes exactly one instruction after the store, also when that instruction takes 1 cycle.

**Mode 7, a number of cycles or the first data read.** The same as mode 2, but 16 `MOV`s follow the store, and rows 2 to 4 are the reads in the 17th, 18th, and 19th instructions after the store. The `MOV`s take more cycles than two `LDR`s. If the stop occurs a fixed number of cycles after the store, row 2 is different from row 1. If the stop occurs after the first data read after the store, row 2 is equal to row 1.

**Mode 8, a mask with no acknowledge.** This mode does not write `CLK control`. **Press DOWN, and keep it down for approximately 2 seconds, then release it**, the same as mode 5. The mode masks each source and acknowledges the buttons, then it unmasks only DOWN. The IRQ callback reads the hold register, masks DOWN with no acknowledge, and reads the hold register again. Nothing acknowledges DOWN after that until the end of the mode.

| row | meaning |
|---|---|
| 1 | the hold register and the status register, after the mask of each source and the acknowledge |
| 2 | in the callback: the hold register at the entry (high 4 digits), and after the mask of DOWN (low 4 digits). `FFFFFFFF` if no interrupt occurs in approximately 10 ms. |
| 3 | the hold register and the status register, approximately 10 ms after the callback |
| 4 | the hold register and the status register, approximately 10 ms after the release of DOWN |

If the hold bit of a held button is its live level AND its enable bit, the mask clears it: the low half of row 2 and the hold half of row 3 do not show `0008`. If the hold bit is a latch, they show `0008`. Row 4 shows if a latch stays set after the release. Row 3 must show `0008` in the status half. If not, DOWN was released too early. This emulator gives `02800008`, `02880280`, `02800008`, and `02800200`, the same as real hardware.

**Mode 9, a release with no acknowledge.** This mode does not write `CLK control`. **Press DOWN, and keep it down for approximately 2 seconds, then release it**, the same as mode 5. The mode masks each source and acknowledges the buttons, then it unmasks only DOWN. Its IRQ callback counts each call, and it never acknowledges DOWN. In each call after the release of DOWN, it masks DOWN.

| row | meaning |
|---|---|
| 1 | the hold register and the status register, after the mask of each source and the acknowledge |
| 2 | the number of calls of the callback |
| 3 | the hold register and the status register, in the first call after the release. `FFFFFFFF` if no call occurs after the release. |
| 4 | the hold register and the status register, approximately 10 ms after the release |

If the hold bit of a button is its level AND its enable bit, the interrupts stop at the release by themselves: row 3 is `FFFFFFFF`, or it shows no `0008` in the hold half. If the hold bit is a latch, the interrupts continue after the release, and row 3 shows `0008` in the hold half. This emulator gives `02800008`, `00002A6C`, `FFFFFFFF`, and `02800200`. The number of calls follows the time that DOWN stays down. Row 3 is `FFFFFFFF` in some runs and a call with no `0008` in the hold half in other runs, in this emulator and on real hardware. The two results are consistent with a dependence on the part of an interrupt call where the release occurs. Both results show that the request goes at the release.

**Mode 10 (`A`), the time from the store to the stop.** The same as mode 2, but the mode makes four stops, and each one needs a press of ACTION. **Hold ACTION for approximately half a second at each wake.** The digits `1` to `4` at the top show each stop that started. Rows 1 to 4 are the stops with 1, 2, 3, and 4 `MOV`s between the store and one read of Timer1. Each row is the count before the store (the high 4 digits) and the count of the read (the low 4 digits). If the two halves are equal, or 1 apart, the read occurred before the stop. If they are different by more, the read occurred after the wake. The first row with a read after the wake gives the number of `MOV`s that the time from the store to the stop does not cover. This emulator gives `03D903D9`, `021F0067`, `00650295`, and `029400DC`: the read after 1 `MOV` occurs before the stop, and the others after the wake, the same as real hardware.

**Mode 11 (`B`), cycles or instructions.** The same as mode 10, with the same rows and the same presses of ACTION. The first three stops execute from WRAM, where a `MOV` costs 1 cycle, with 2, 3, and 4 `MOV`s between the store and the read. The fourth stop executes from FLASH, with one `MOV` with a register-specified shift (3 cycles) before the read. Mode 10 gives three rules that agree with the measurements. Each rule gives a different result in this mode (b = the read occurred before the stop, a = after the wake):

| rule | row 1 | row 2 | row 3 | row 4 |
|---|---|---|---|---|
| the next instruction starts only while fewer than 3 cycles have passed | b | a | a | a |
| the same, with 4 cycles | b | b | a | b |
| two instructions after the store, or one if the first is an `LDR` | a | a | a | b |

These results use the cycle costs of this emulator. A result that agrees with no rule shows that one of those costs, or each rule, is incorrect. This emulator uses the rule with 3 cycles (`CLK_STOP_DELAY_CYCLES` in `core/src/clk.h`). It gives `03D903D9`, `021F0067`, `00650295`, and `029300DB`, the same pattern as real hardware.

**Mode 12 (`C`), the RTC request.** This mode does not write `CLK control`, and it needs no button after the press of DOWN that starts it. It takes approximately 1.5 seconds. It masks each source, then it follows the RTC line in the status register (`0200`), which is high for half of each second and low for the other half. Each row is the hold register (the high 4 digits) and the status register (the low 4 digits), approximately 10 ms after its step:

| row | step |
|---|---|
| 1 | the line is low, then an acknowledge of the RTC |
| 2 | the next rising edge of the line |
| 3 | an acknowledge of the RTC, with the line still high |
| 4 | the next falling edge of the line |

`0200` in the hold half shows an RTC request. Row 1 shows if an acknowledge clears the request. Row 2 shows if the rising edge sets it. Row 3 shows if an acknowledge clears it while the line is high, and its status half shows if the acknowledge also clears the line bit. Row 4 shows if the falling edge sets the request. If row 3 shows no `0200` in the status half, the mode does not see the falling edge, and row 4 is not at an edge. `FFFFFFFF` is a row that the mode did not get to, because the line did not change in approximately 4 seconds. The code of this mode is in the first block, after each routine of that block, because the second block is full. This emulator gives `00800008`, `02800200`, `00800200`, and `00800000`, the same as real hardware. The `0008` in row 1 is DOWN, which is still down.

**Mode 5, the hold bit of a held button.** This mode does not write `CLK control`. **Press DOWN, and keep it down for approximately 2 seconds, then release it.** That press starts the mode, and the mode reads the interrupt controller while DOWN is still down. It masks each source, thus no interrupt occurs. Each row is the hold register (the high 4 digits) and the status register (the low 4 digits). DOWN is `0008` in each register.

| row | when |
|---|---|
| 1 | at the start, before the mode changes the interrupt controller |
| 2 | immediately after the mask of each source and an acknowledge of DOWN |
| 3 | approximately 10 ms after row 2. DOWN is still down, and each source is still masked. |
| 4 | after one more acknowledge of DOWN, then the release of DOWN, then approximately 10 ms |

The `0008` digit of the hold register in row 3 shows if the request of a held button comes back after an acknowledge while its interrupt is masked. Row 3 must show `0008` in the status half. If not, DOWN was released too early. This emulator gives `02800008`, `02800008`, `02800008`, and `02800200`: no request of DOWN, the same as real hardware.

**Modes 3 and 4, the record of each interrupt.** Timer1 is unmasked, the same as mode 0. Mode 3 unmasks only ACTION of the buttons, and mode 4 unmasks each button, the same as mode 0. Thus a difference between mode 3 and mode 4 is a difference that the direction buttons make. The handler reads the hold register before it acknowledges it. For the first two interrupts of each source, it records the hold register, the Timer1 count, and the phase. Each row holds two values: the first interrupt in the high 4 digits, and the second in the low 4 digits. `FFFF` is a value that no interrupt recorded.

| row | meaning |
|---|---|
| 1 | the Timer1 interrupts before the store (high), and after the store (low) |
| 2 | the hold register at the first and the second interrupt. `0001` is ACTION, `0100` is Timer1. The hold register also shows sources that are masked. |
| 3 | the Timer1 count at the first and the second interrupt |
| 4 | the phase of the first and the second interrupt. `0000` is before the store, and `0001` is after it. |

This emulator gives `00000002`, `02810001`, `03DB03DA`, and `00010001` in mode 3, and `00020002`, `02880008`, `03E703E7`, and `00000000` in mode 4. `tools/bench_probe.c` holds each button for approximately 0.3 seconds, thus Timer1 counts two interrupts for each press. An interrupt before the store shows its source in row 2, and the Timer1 count at that time in row 3.

**Press ACTION to wake the device.** LEFT, RIGHT, UP, and DOWN do not wake it, also with their interrupts enabled. See "CLK control" in [docs/hardware-notes.md](../docs/hardware-notes.md). While the CPU is stopped, a hold on ACTION does not open the exit prompt. Press ACTION one time to wake the device, then hold ACTION.

**What appears.** If the clock really stops, the bar is the last thing that this screen draws, and it stays on the screen until a button press. If the store does nothing, the result screen replaces the bar too fast to see.

**Four rows of results**, from the top to the bottom:

| row | meaning |
|---|---|
| 1 | the whole seconds of RTC time that passed across the store |
| 2 | the Timer1 interrupts before the store (the high 4 digits), and after the store (the low 4 digits). The CPU handles each interrupt after the store when it runs again. The buttons are excluded. |
| 3 | RTC seconds (BCD), one byte each, from the left: before the store, at the first Timer1 interrupt after the store, at the second Timer1 interrupt after the store, and after the wake. `FF` means that no such interrupt occurred. |
| 4 | the Timer1 count just before the store (the high 4 digits), and just after the wake (the low 4 digits). If Timer1 stops with the CPU, the two values are almost the same. |

**How to read them together.** Wait several seconds before a press of ACTION wakes the device. Thus the top row is unambiguous:

| row 1 (seconds) | row 2 (timer IRQs, both halves) | conclusion |
|---|---|---|
| several | 0, or 1 for each 128 ms that DOWN or ACTION stays down | **The CPU stopped, and the timers stopped with it.** Real hardware gives this row, and this emulator gives the same. A held button with an unmasked interrupt gives the interrupts: see "What the real-hardware runs show". |
| several | large | The CPU stopped, but the timers continued and they did not wake it. Thus the wake logic of this emulator is incorrect. |
| 0 | 0 | **The CPU never stopped.** This register is not the stop. A different write in the sequence of the real app is the stop. |
| 0 | small | The CPU stopped, and a timer interrupt woke it almost immediately. Thus the timers continue, and they do wake the CPU. |

If the screen changes **with no button press**, then a source that is not a button wakes the CPU. The RTC is the most probable source, because this test does not mask its interrupt. The real app does mask that interrupt. No run has met this condition. Thus the question is still open.

**The value that `0x0B000004` reads back is not a stop status.** This screen keeps that value in `WRAM_STOP_READBACK`, and it does not show it. Real hardware returned `0x17`, which is the `CLK_MODE` value that this app sets, with the steady bit (`0x10`) ORed in. Thus both words of this register read back as `CLK_MODE`, and `+0x4` does not read back the value that software wrote to it. `clk_read8` in `core/src/clk.c` now models that behavior. One consequence: this screen cannot show whether the stop bit clears itself, because there is no readable stop status. The run does show that the CPU started again and continued. Thus the stop does not persist across a wake.

**A second unit counted 1 or 2 Timer1 interrupts across the store**, on each of five runs, with the earlier version of this screen. That version showed only the total. Timer1 expires approximately each 128ms, thus that total gives the time that the buttons stayed down. See the next paragraph.

**What the real-hardware runs show.** While a button stays down and its interrupt is unmasked, its request comes back after each acknowledge. Thus the CPU executes only the interrupt handler until the release, and Timer1 counts. This mode unmasks DOWN while the press of DOWN that starts it is still down. Timer1 expires one time before the code reaches the store, and the handler counts that interrupt before the store. After the wake, ACTION gives the same effect: the handler counts one Timer1 interrupt for each 128 ms that ACTION stays down, and the code of the mode runs again at the release. Row 4 gives the same count before and after the stop, because the CPU executes the first instruction after the store before it stops (mode 2). See [VERIFICATION.md](VERIFICATION.md).

**The control run in this emulator** used a stop of 13 seconds, through `tools/bench_probe.c`, which holds DOWN and ACTION for approximately 0.3 seconds each. It gave row 1 `0x0000000D`, row 2 `0x00020002`, row 3 `0x19323232`, and row 4 `0x02230223`: two Timer1 interrupts while DOWN stays down, two while ACTION stays down, and the same count before and after the stop. The seconds row compares directly against hardware, because the emulated RTC keeps true 1Hz time. Thus that row must agree with the wall-clock time of the wait, on either side.

**Recovery.** If the CPU stops and nothing wakes it, the device needs its physical reset button. Nothing in this test writes flash, thus that is the full cost.

### Screen 15: when does the COM block raise its interrupt?

This screen is interactive, like screen 13. **The memory card slot covers the buttons, thus this screen runs by itself while the device is in the slot.** DOWN arms it before the device goes in. UP shows the results after the device comes out.

**The reason for this screen.** The kernel answers a command from its FIQ handler. At the first byte of a command, that handler polls the Ready bit of `COM_STAT2` up to 30 times before it takes the byte (`0x04001086` to `0x040010A8` in `J110`). That wait is only necessary if the interrupt can arrive before the byte is complete. This emulator asserts the interrupt when the byte is complete, thus the kernel finds Ready at its first poll. If real hardware asserts it earlier, the kernel acknowledges the first byte sooner on hardware than in this emulator. See "What the kernel writes to `COM_CTRL1` and `COM_CTRL2`" in [docs/hardware-notes.md](../docs/hardware-notes.md).

**The bits of `COM_STAT2` stay set between accesses, and a read shows only the bits that `COM_CTRL1` selects.** Thus the screen clears them before each capture, and it uses `COM_CTRL1 = 3` for most captures. HOLD does not latch a request that continues, thus the screen also logs the raw input bit of `INT_COM`. See "What real hardware shows in `COM_STAT2`" in [docs/hardware-notes.md](../docs/hardware-notes.md).

**How to run it.**

1. Start the app with the device out of the slot. Go to screen 15 (LEFT from screen 1).
2. Press DOWN. A bar appears across the middle of the screen. The app now waits for the docking level, and it needs no more buttons. The armed run does not read the buttons until it ends, thus a hold on ACTION does not open the exit prompt. To end the run before the device goes into the slot, press ACTION one time. After the device goes into the slot, the run ends when the device comes out.
3. Turn the PS1 on and go to its memory card screen, which reads the slot again and again. A controller in the same port also makes transfers, because the controller and the card share the select line of the port.
4. Put the device in the memory card slot. Wait approximately 15 seconds. The app makes 8 captures, and it shows each one as it makes it.
5. Take the device out of the slot **before** you turn the PS1 off. The screen shows capture 0. Press UP for the next capture.

**After the run, press only UP on this screen.** DOWN arms a new run, and that clears the stored captures. To give the link back to the kernel, leave the app.

**What it does.** The app masks every interrupt source at startup, thus the kernel does not answer the PS1 while this app runs. After the docking level appears (`INT_INPUT` bit 11), the app waits approximately half a second for the connector to settle. It then makes 8 captures. Captures 0 to 5 use `COM_CTRL1 = 3`, capture 6 uses 1, and capture 7 uses 2. Each capture:

1. Sets Timer0 to /2, which is 0.5us for each tick at `CLK_MODE` 7. It restores Timer0 afterwards.
2. Does the link setup of the kernel at docking (`0x0400073E` to `0x0400078C`): `COM_CTRL1 = 0`, `COM_MODE = 2`, `COM_DATA = 0xFF`, `COM_CTRL2 = 3`, `COM_MODE = 6`, and then `COM_CTRL1` = the value of the capture.
3. Waits for approximately 2ms with no change. Each start of that wait writes `COM_CTRL2 = 3`, reads `COM_DATA`, and acknowledges `INT_COM`, to clear what an access left. The kernel writes `COM_CTRL2 = 1` straight after it sees Ready, thus `COM_CTRL2` is the most probable clear.
4. Logs each change of six bits for approximately 12ms after the first change: `COM_STAT1` bits 0 and 1, `COM_STAT2` bits 0 and 1, bit 6 (`INT_COM`) of the interrupt HOLD register, and bit 6 of `INT_INPUT`, the raw input of the interrupt controller.

The app never acknowledges a byte. Thus the PS1 sees no card while this screen runs, and it tries again. A capture that gets no access in approximately 4 seconds is made again, while the device stays in the slot. The run ends when the device comes out of the slot.

**Five rows of results.** Each bit time is in Timer0 /2 ticks from the first change. `FFFF` means that the bit did not change in the window.

| row | left four digits | right four digits |
|---|---|---|
| top | `F`, the capture number (0 to 7), the bits before the first change (2 digits) | the ticks that the window took (0 = no capture) |
| 2 | `COM_STAT2` bit 0 (Ready) | HOLD bit 6 (`INT_COM`) |
| 3 | `COM_STAT2` bit 1 | `INT_INPUT` bit 6 (`INT_COM`, raw) |
| 4 | `COM_STAT1` bit 0 | `COM_STAT1` bit 1 (the release of /SEL) |
| 5 | the number of changes logged (2 digits), and the last sample (2 digits) | the time of the last change |

The two digits of "bits before" and "last sample" use this order: bits 0 and 1 are `COM_STAT1`, bits 2 and 3 are `COM_STAT2`, bit 4 is HOLD bit 6, and bit 5 is `INT_INPUT` bit 6. A time is the first time that the bit differs from its value before the first change, thus it can be a set or a clear. The release bit of `COM_STAT1` stays set from the access before, until the PS1 selects the device again, thus "bits before" is usually `02`, and the start of the access clears that bit at time 0.

**The resolution.** The window value divided by 1536 is the time between two samples. In this emulator that is approximately 17 ticks (8.3us). One byte from a PS1 is approximately 32us, which is 64 ticks. A sample reads four registers one after the other, thus a change can fall between two reads, and the next sample then shows the rest of it. Thus two times that are less than approximately 35 ticks apart are the same time.

**How to read the result.** In each capture, compare the two `INT_COM` bits with Ready and with `COM_STAT1` bit 0.

| `INT_COM` (either bit) | Ready | conclusion |
|---|---|---|
| near 0 | near 0, or less than approximately 35 ticks after it | The interrupt comes when the byte is complete. This is the model of this emulator. |
| near 0 | clearly later, near one byte time or more | The interrupt comes before the byte is complete. Thus the kernel acknowledges the first byte sooner on hardware than in this emulator. `COM_STAT2` bit 1 then shows if it is the bit that starts the interrupt. |
| `FFFF` in both | any | The block does not request the interrupt with this `COM_CTRL1` value, or neither register shows a masked request. |

If the bits before the first change still show `COM_STAT2` bits, `COM_CTRL2 = 3` does not clear them. That is a result too.

**Real hardware and this emulator agree.** Both give `INT_COM` at the selection with `COM_CTRL1` = 3 and 2, `INT_COM` at Ready with `COM_CTRL1 = 1`, and Ready approximately 60us after the selection. This emulator takes its model of `COM_STAT2` from the real-hardware run of this screen. See [VERIFICATION.md](VERIFICATION.md), and "What real hardware shows in `COM_STAT2`" in [docs/hardware-notes.md](../docs/hardware-notes.md).

**Recovery.** This screen writes no flash. If no PS1 accesses the slot, the run waits until the device comes out of the slot.

### Screens 16 to 18: what does a misaligned load give?

An ARM7TDMI does not stop at a misaligned load. It gives a value, and some real apps use that value. These three screens show the full 32-bit result of each of the three load instructions, in the ARM state and in the Thumb state. They are not timing measurements. They take no interrupts, and they write no flash.

Screen 16 is `LDR`, screen 17 is `LDRH`, and screen 18 is `LDRSH`. The font has no digit above F, thus the top-left of each screen shows its number in hex: `10`, `11`, or `12`. The four rows are:

| row | the instruction state | the address |
|---|---|---|
| 1 | ARM | data + 1 |
| 2 | ARM | data + 3 |
| 3 | Thumb | data + 1 |
| 4 | Thumb | data + 3 |

The data is 8 bytes in WRAM: `11 22 33 84 55 66 77 88`. WRAM permits 8-bit, 16-bit, and 32-bit access, thus the result is a property of the CPU, and not of the bus of a different region. The byte at +3 is `84`, with bit 7 set, thus a sign extension of that byte is visible. The loads run each time that the screen is drawn. See `draw_load_screen` in [`src/loads.s`](src/loads.s).

**How to read the result.** Each candidate behavior gives a different value. The first value in each row is the real-hardware result, and this emulator gives the same value:

| row | real hardware | other candidates |
|---|---|---|
| `LDR` +1 | `11843322` (the aligned word, rotated right by 8) | `84332211` (the aligned word, no rotation), `55843322` (the true unaligned word) |
| `LDR` +3 | `33221184` (rotated right by 24) | `84332211`, `77665584` |
| `LDRH` +1 | `11000022` (the aligned halfword, rotated right by 8 in 32 bits) | `00001122` (the aligned halfword, with its two bytes exchanged in 16 bits), `00002211` (no rotation), `00003322` (the true unaligned halfword) |
| `LDRH` +3 | `33000084` | `00003384`, `00008433`, `00005584` |
| `LDRSH` +1 | `00000022` (a sign-extended byte read from the odd address) | `00001122` or `11000022` (a rotated halfword), `00002211` (no rotation) |
| `LDRSH` +3 | `FFFFFF84` | `FFFF8433` (no rotation), `33FFFF84` (a rotated sign-extended halfword) |

On real hardware, the ARM rows and the Thumb rows of each screen agree. Thumb instructions use the load path of the ARM instructions. A different emulator that gives different ARM and Thumb rows has a fault in one of the two paths.

**Why these screens exist.** Real apps use misaligned loads, but each known app uses only the low byte of the result. A homebrew ID editor reads a font table with `LDRH`, and a fighting-game app reads a table of sprite frame numbers with a Thumb `LDR`. The low byte cannot separate the first two `LDRH` candidates: only the upper bits do. These screens show the full register. See "CPU" in [docs/hardware-notes.md](../docs/hardware-notes.md), and the results in [VERIFICATION.md](VERIFICATION.md).

### Screens 19 to 28: the CPU rules of GBATEK, on real hardware

GBATEK is the CPU documentation of this emulator. See "CPU" in [docs/hardware-notes.md](../docs/hardware-notes.md). These screens measure CPU rules of GBATEK that an app can see, and that a simple model of the CPU does not have. GBATEK is documentation, thus a result on real hardware is the authority. [VERIFICATION.md](VERIFICATION.md) gives the real-hardware value and the value of this emulator for each row.

Each screen runs its test each time that it is drawn. The test data is in WRAM. The top-left of each screen shows its number in hex, from `13` to `1C`. [`src/cpu.s`](src/cpu.s) holds each test, with the exact meaning of each row.

**No screen here stops the device.** Screens 26, 27, and 28 execute instructions that can jump. Screens 26 and 28 prepare a safe target for each result, and real hardware confirms each one. Screen 27 runs its tests with the traps of screen 29 (see "Screen 29" below). Thus a trap catches each result that does not return. No screen here writes flash. Screens 27 and 28 write kernel RAM, and they write the old values back after the test.

**Screens 19 to 23 and 26 to 28 give exact values.** In each row below, the GBATEK value is the value that the text of GBATEK gives. A dash means that GBATEK gives no value. Real hardware agrees with each GBATEK value, except on screen 27.

| screen | row | the test | GBATEK |
|---|---|---|---|
| 19 (`13`) | 1 | `SWP` at data + 1: the value that it reads | `11843322` |
| | 2 | the aligned word at data + 0 after that `SWP`, which writes `A1B2C3D4` | `A1B2C3D4` |
| | 3 | the aligned word at data + 4 after that `SWP` | `88776655` |
| | 4 | `SWP` at data + 3: the value that it reads | `33221184` |
| 20 (`14`) | 1 | R15 as an ALU operand, minus the address of the instruction. One byte for each form, from the high byte: `ADD r1,PC,r3,LSL r2`, `MOV r1,PC,LSL r2`, `ADD r1,PC,r3`, `MOV r1,PC` | `0C0C0808` |
| | 2 | the stored R15, minus the address of the instruction. From the high byte: `STR PC`, `STRH PC`, `STMIA {PC}`. The low byte is not used | `0C0C`, then a dash for `STM` |
| 21 (`15`) | 1 | `LDMIA r0!,{r0,r1}` over the data `11111111 22222222`: the value of r0 | `11111111` |
| | 2 | `LDMIA r1!,{r0,r1}`: the value of r1 | `22222222` |
| | 3 | `STMIA r0!,{r0,r1}`: the stored r0 minus the old base. 0 is the old base, and 8 is the new base | `00000000` |
| | 4 | `STMIA r1!,{r0,r1}`: the stored r1 minus the old base | `00000008` |
| 22 (`16`) | 1 to 4 | the same as screen 21, with the Thumb `LDMIA` and `STMIA` | the same as screen 21 |
| 23 (`17`) | 1 | Thumb `PUSH {r1}` with SP = data + 6, and r1 = `11223344`: the aligned word at data + 0 | `11223344` |
| | 2 | the aligned word at data + 4 | `00000000` |
| | 3 | SP after the `PUSH`, minus data | `00000002` |
| | 4 | the value that a `POP` then reads | `11223344` |
| 26 (`1A`) | 1 | ARM `STMIA r0!,{}`. High half: the base after, minus the base before. Low half: the word at the base minus the address of the `STM`, or `0000` if no store occurred at the base | `0040`, then a dash |
| | 2 | ARM `LDMIA r0!,{}`. High half: as row 1. Low half: 1 if the instruction loaded the PC | `00400001` |
| | 3 | Thumb `STMIA r0!,{}`, the same as row 1 | `0040`, then a dash |
| | 4 | Thumb `LDMIA r0!,{}`, the same as row 2 | `00400001` |
| 27 (`1B`) | 1 | `MOV PC,r2` with an address 2 above a word boundary. The high byte is the result of the traps (`01` = the test returned, `02` = undefined instruction). The low 3 bytes are R15 minus the aligned target for a return, or LR minus the aligned target for a trap. The words at the target give `01000008` if R15 is aligned, `0100000A` if R15 keeps bit 1 and the fetch gives the aligned word, and `0200000E` if R15 keeps bit 1 and the fetch gives the halfword in both halves. See `test_misaligned_pc` in `src/cpu.s` | `01000008` |
| | 2 | the same with `LDR PC,[r5]` | `01000008` |
| | 3 | the same with `LDMIA r5,{PC}` | `01000008` |
| | 4 | the same with `BX r2`, where bit 0 is clear | `01000008` |
| 28 (`1C`) | 1 | ARM `0xE7F000F0`. `LR_und` minus the address of the instruction, or 0 if the undefined-instruction handler did not run | `00000004` |
| | 2 | Bits 8 to 15: the low byte of CPSR in the handler. Bits 0 to 7: the low byte of `SPSR_und` | `00009B10` |
| | 3 | Thumb `0xDE00`, the same as row 1 | `00000002` |
| | 4 | Thumb `0xDE00`, the same as row 2 | `00009B30` |

Notes on the GBATEK values:

- Screen 26: GBATEK says that an empty list transfers R15, but it does not give the stored value of R15. Thus the low half of rows 1 and 3 has no GBATEK value.
- Screen 28, rows 2 and 4: `9B` is Undefined mode with I set. GBATEK gives I=1 and an unchanged F for this exception. The app runs in User mode, which cannot set F. `10` and `30` are User mode in the ARM state and in the Thumb state.
- Screen 28: the kernel vector at `0x04` is `LDR PC,[PC,#0x18]`, which reads the handler address from RAM `0x24`. The test writes the address of its own handler to that word, and it writes the old value back after the test. If that word does not keep the written value, the test does not execute the undefined instruction, and each row is `EEEEEEEE`.
- A CPU that does not decode these encodings as undefined can execute `0xE7F000F0` as `LDRB r0,[r0,r0,ROR r0]!`, and `0xDE00` as a branch to +4. The test gives those two instructions a safe result.

**Screens 24 and 25 give Timer0 ticks across `LOOP_N` loops.** Compare the rows of one screen. Do not compare the absolute values with other screens.

| screen | row | the test | GBATEK |
|---|---|---|---|
| 24 (`18`) | 1 | Thumb `MUL` with Rd = 5 and Rs = `12345678` | equal to row 3 |
| | 2 | Thumb `MUL` with Rd = `12345678` and Rs = 5 | equal to row 4 |
| | 3 | Rd = 5 and Rs = 5: the cost with m = 1 | |
| | 4 | Rd = `12345678` and Rs = `12345678`: the cost with m = 4 | |
| 25 (`19`) | 1 | `UMULL` with Rs = `000000FF`: the cost with m = 1 | |
| | 2 | `UMULL` with Rs = `FFFFFFFF` | equal to row 4 |
| | 3 | `SMULL` with Rs = `FFFFFFFF`, which is m = 1 for both rules | equal to row 1 |
| | 4 | `UMULL` with Rs = `12345678`: the cost with m = 4 | |

GBATEK gives the multiply cost from the high bytes of the incoming Rd for a Thumb `MUL`, because Thumb `MUL Rd,Rs` is ARM `MULS Rd,Rs,Rd`. For `UMULL` and `UMLAL`, GBATEK stops the multiply early only for high bytes that are all zero. The signed forms also stop early for high bytes that are all one.

### Screen 29: the probe of a misaligned write to R15

This screen finds how the CPU executes after a write of a misaligned address to R15. The kernel sends the undefined-instruction exception, the prefetch abort, and the data abort to `0x040001C8`, which is a loop with no exit (`nop; b .`). Thus each one of these exceptions stops the device, and the LCD keeps its last contents.

This screen runs nothing when it is drawn. **UP** selects the next probe, and **DOWN** runs the selected probe. During a probe, the kernel slots of the undefined-instruction, SWI, prefetch-abort, data-abort, and IRQ handlers hold the trap handlers of the app, and Timer1 gives an IRQ after approximately 8ms as a watchdog. A trap records the exception, and it returns to the app. After each probe, the app writes the old slots and the old Timer1 back. [`src/cpu.s`](src/cpu.s) holds each probe.

**Row 1:** the high byte is the probe number. The next byte is the result:

| result | meaning |
|---|---|
| `00` | not run |
| `01` | the probe returned |
| `02` | undefined-instruction exception |
| `03` | SWI |
| `04` | prefetch abort |
| `05` | data abort |
| `06` | the watchdog IRQ |
| `EE` | a kernel slot did not keep the written value. The probe did not run |
| `FF` | the probe is running. If the device stops, the LCD keeps this value |

**Rows 2 to 4 after a trap:** row 2 is the return address of the exception minus the target of the probe. Row 3 is the CPSR at the exception. Row 4 is r1 at the exception minus the target, or 0 if r1 was 0.

| probe | test | rows 2 to 4 if the probe returns |
|---|---|---|
| 0 | control: `MOV PC` to an aligned target in FLASH1 | row 2: 8 |
| 1 | control: the undefined instruction `0xE7F000F0` | a trap, `02`, with row 2 = 4 |
| 2 | control: a loop with no exit | a trap, `06`, with row 2 = 4 |
| 3 | `LDR` from FLASH1 at an aligned word + 2. The data is `11 22 33 84 55 66 77 88` | row 2: that `LDR`. Rows 3 and 4: the aligned words at +0 and +4 |
| 4 | the same, from BIOS ROM at `0x04000210` | the same |
| 5 | the same, from WRAM | the same |
| 6 | `MOV PC` to a FLASH1 target + 2 | row 2: 8 if R15 was aligned, A if not, 0 if the target instruction did not execute |
| 7 | `LDR PC`, to the same | the same |
| 8 | `LDMIA {PC}`, to the same | the same |
| 9 | `BX`, bit 0 clear, to the same | the same |
| A to D | probes 6 to 9, with the target in WRAM | the same |
| E | the fetch rule: `MOV PC` to a FLASH1 target + 2, where each fetch rule executes a different instruction. See `probe_fetch_pad` in `src/cpu.s` | a trap. Row 2 and row 4 identify the rule |
| F | the same, with the target in WRAM | the same |
| 10 | `MOV PC` to `0x02004000`, with R15 aligned. In the default build, that is the first address after the app. In a build with `BLOCKS=3`, it is a target in the third block | row 2: 8 with `BLOCKS=3` |
| 11 | `LDR` at `0x02004000` + 2, + 0, and + 4, the same as probe 3 | the same as probe 3 |
| 12 | `MOV PC` to `0x02006000`, with R15 aligned. That is after the app in both builds | row 2: 0 |
| 13 | `LDR` at `0x02006000`, the same as probe 11 | the same as probe 3 |
| 14 | three words in the encoding space of `MSR CPSR,Rm`, with bits 12 to 15 not all 1. Each row is the CPSR after one word. See `probe_msr_space` in `src/cpu.s` | row 2: `E12FE12F`, with NZCV = 1111 before it. Row 3: `E12FE120`, with r0 = `A0000000` and NZCV = 0000. Row 4: `E12F0000`, with r0 = `50000000` and NZCV = 0000. An `MSR` writes the flags from R15, from `A0000000`, and from `50000000`. A `TEQ` with no flag write keeps them |
| 15 | `MSR CPSR,Rm` with three shifts: `ASR #1` of `80000000`, `ROR #4` of `00000005`, and `RRX` of `20000000` with C = 1. See `probe_msr_shifts` in `src/cpu.s` | rows 2 to 4: the CPSR after each word. A shift gives `C`, `5`, and `9` in the flags |
| 16 | row 2: a correct `MSR CPSR_f` with `5F000000`. Row 3: `MSR CPSR_f,#50000000` with bits 12 to 15 clear. See `probe_msr_other` in `src/cpu.s` | row 2 shows if the CPSR keeps bits 24 to 27. Row 3 shows if the immediate form ignores bits 12 to 15 |
| 17 to 1A, 1D, 1E | one word in the encoding space of `BX`, in WRAM, with r0 = a target: `E12FF150`, `E12FFF30`, `E12F0010`, `E12FFF70`, `E120FF10`, and `E12AFF10`. See `probe_bx_space` in `src/cpu.s` | row 2: 1 if the word branched to r0. Row 3: LR minus the word, or `EEEEEEEE` if LR did not change. Row 4: the CPSR |
| 1B, 1C, 20 | words in Supervisor mode. The probe writes its own handler to the SWI slot of the kernel, and it executes SWI. The handler keeps the SPSR, executes the words, and writes the SPSR back. See `probe_priv_1b`, `probe_priv_1c`, and `probe_priv_20` in `src/cpu.s` | rows 2 to 4: the CPSR or the SPSR after each word |
| 1F | `E12F0010`, `E12F1010`, and `E12FE110`, with a value in Rm that an `MSR` writes to the flags. See `probe_msr_bit4` in `src/cpu.s` | rows 2 to 4: the CPSR after each word |
| 21 | in Supervisor mode: a write of mode 03, the 26-bit Supervisor mode, to the CPSR. See `probe_priv_21` in `src/cpu.s` | rows 2 to 4: the CPSR before the write, immediately after it, and after the old value is back |
| 22 | writes to COUNT of Timer2. See `probe_count_write` in `src/cpu.s` | row 2: COUNT of a stopped timer after a write of `0ABC`, from `0555`. Row 3: COUNT of a running timer immediately after a write of `0ABC`, from `7FFF`. Row 4: PERIOD after that write. A COUNT that ignores writes gives approximately `0555` and `7FFF` |

The word at the target is `MOV r1,PC`, and each other word near the target is `BX r3`, which returns to the app. Probes 3 to 5 show how each memory region answers a word read at an address 2 above a word boundary. A difference between FLASH1 and WRAM in probes 6 to D then points to the memory, and not to the CPU.

Run the controls first. Probes 1 and 2 confirm that the traps operate on the unit. If either one stops the device, do not run probes 6 to D. See "Screen 29" in [VERIFICATION.md](VERIFICATION.md) for the real-hardware values, and for the rules that they show.

### Reading the hex digits

Each row is 8 hex digits, from 0 to 9 and A to F, with the most significant nibble first. A small 3 by 5 pixel font draws each digit. The rows use the full 32-pixel screen width exactly: 8 digits at a 4px pitch is 32px.

A "confirmed slower" result looks like this. The top value is clearly and consistently larger than the bottom value, for example approximately 2 times larger, or one more hex digit of magnitude.

A "confirmed equal" result looks like this. The two numbers are within a few percent of each other. The loop overhead and the call overhead are never perfectly identical between the two sides. Do not expect an exact match, even when the underlying cost is equal.

## Other real-hardware findings

These findings are not screen values. [VERIFICATION.md](VERIFICATION.md) holds each screen value.

**A departure with Action still held started the app again.** A hold on Action opens the continue/exit prompt of this app, and the Action *press* edge confirms EXIT. The departure sequence then ran while the button was still physically down. A press lasts much longer than the exit operation. Thus control returned to the system while the user held Action. The browse screen of the system then read that condition as a new press, and it started this app again immediately.

The correction waits for the release before the departure. It then acknowledges the button sources, thus no latched HOLD survives either. `INT_INPUT` reports a live button level on real hardware. That level lets the wait operation end. The hold-to-open gesture of this app already depends on the same property, because it counts 75000 sequential reads with Action held. The wait is bounded, thus a stuck contact cannot spin forever. Recovery from that condition needs the physical reset button.

`tools/pk_exit_test.c` confirms this behavior completely in the emulator. It holds Action, it opens the prompt, and it selects EXIT. It then confirms the selection while it still holds Action. It confirms that the CPU stays outside BIOS memory until the release of Action. It then confirms that the app exits correctly after the release. A removal of the wait loop makes that same test fail exactly the way that the real-hardware report describes. That failure is a departure into BIOS space with Action still held. The validated departure sequence has no change. The wait only comes before it.

That test needed one correction in the core first. `INT_LEVEL_MASK` in `core/src/intc.h` did not include the buttons. Thus an acknowledge could clear the STATUS bit of a held button, but `docs/hardware-notes.md` gives STATUS as a register that follows the live level. `pk_exit_test` does not exercise that correction, because this app never enables its own button interrupts and never acknowledges them. Thus nothing in this specific flow ever clears STATUS. That correction is still correct. The same recorded behavior supports it, and it has its own unit test, `test_button_status_survives_acknowledge` in `tests/cpu_test.c`. But that correction is not the cause of this exit fault, and it is not the reason that the fault occurs or stops in the emulator. The wait loop above is the full reason.

**This app left two header bytes at zero. Each real app sets both bytes.** Both bytes are inside zero-fill regions in `header.s`, and this project assumed that they were reserved:

- **`0x03` is the block count.** It holds the number of 8KB blocks that the save occupies. Each real app that this project examined sets it correctly. The available reference dumps cover saves of 1, 2, 4, 7, and 13 blocks, and each one declares its own true count. This app declared 0.
- **`0x56` is `0x01` in each of the nine real apps that this project examined.** That value does not change with the block count, the icon style, or the frame count. Thus it is not a frame count. It is most probably a format marker or a version marker. This app declared 0, and it was the only file in that set with that value.

Together, these two bytes corrupted the app-select screen of the real BIOS. LEFT and RIGHT browse between saves correctly, with a horizontal transition. But this app also had a vertical axis on UP and DOWN. An UP press made a transition down into a screen with an error. Each further UP press gave the identical error. A DOWN press made a transition back up to the correctly drawn icon.

The screen with the error draws the contents of body `0x200` to `0x2FF`. A deliberate edit to that region changed the data that appeared. That method confirmed the region as the read target, and not as the cause.

One reference dump isolated this fault. That dump has byte-for-byte the container shape that this app had at that time: one block, a `0x2000` body, and chain link `0xFFFF`. It draws correctly. Thus the container is not the cause, and only the declared header fields remain. With both bytes corrected, the header of this app agrees with a known-good multi-block dump in each field except the block count, which is correctly different.

This work removed two candidates. The first candidate is the unidentified icon data at `0x200` to `0x2FF`. A test filled that area with the bitmap of this app. The error on the screen changed, but it did not stop, and this project reversed that change. The second candidate is the object link order for a manual build. See "Building".

## Known caveats

- A CPSR-based disable of IRQ and FIQ has no effect on real hardware, from unprivileged User mode. The real BIOS always dispatches an app from User mode. The real protection is the INTC-mask write. That write runs first, and it operates at each privilege level. If one run gives a number that is very different from a repeat of the same run, examine an interrupt that got through in the short window before that write lands.
- The "test" measurement of screens 3 and 4, which calls the real BIOS, does not force a specific internal code path inside the real BIOS routine. It calls the routine exactly as it is, with the live kernel RAM state that is present. The WRAM "control" copy reads exactly the same live memory, thus the comparison stays fair. The absolute numbers can still be slightly different between power cycles, if that live state is different.
- **The last 256 bytes of the browse-screen icon are zero-filled in this build.** That range is Title Sector body offset `0x200` to `0x2FF`. This build does not take these bytes from a real reference app. Their real purpose is still unknown. See `docs/app-notes.md`.

  During development, this build held those bytes exactly as they are in a real reference app. A test confirms that version on real hardware. That data is third-party app data, thus it has no place in a committed tool that this project shares.

  The zero-filled version now also operates on real hardware. Each run from 2026-07-30 and later used the committed build, and the browse-screen icon drew correctly at each launch from the card directory. This is no longer an untested condition. Nothing suggests that these bytes matter for a single-frame icon. See the comment in `src/icon.s`.
