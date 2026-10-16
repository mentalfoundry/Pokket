# Real-hardware verification

This file gives the confirmed real-hardware value of each `pk_timing_bench` screen, and the value that this emulator gives for the same screen. Use it to compare a different unit, or the output of a different emulator, against a known-good result.

This file is a reference of current values. It is not a run log. When a new run supersedes a value here, replace the value. The git history of this file holds each superseded per-run entry.

See [README.md](README.md) for the meaning of each screen, for the build method, and for the reason that each experiment exists.

## Where these values come from

**Real hardware:** a retail PocketStation unit, with the values read directly off the 32 by 32 LCD. The date column in each table gives the run that supplied the value.

**This emulator:** one run of `tools/bench_probe.c` on 2026-08-06, against the committed `pk_timing_bench.mcs` and a real J-110 BIOS dump. That tool reads the raw results out of the WRAM result block. Thus it needs no person to read hex digits off the screen. Screens 13 and 14 are the exception. See their sections below.

**Run-to-run jitter** on the same unit is ±1 in the last hex digit. Treat a difference of 1 as agreement. Treat a larger difference as a real difference.

No run recorded here had a crash, a stop, or any other anomaly.

## Screens 1 to 4: memory-access cost

Each value is the number of Timer0 ticks across 30000 iterations of a tight loop.

| Screen | What it compares | Hardware test | Hardware control | Emulator test | Emulator control | Hardware date |
|---|---|---|---|---|---|---|
| 1 | an ARM loop against a Thumb loop | `0x00002BF2` | `0x000019A3` | `0x00002BF2` | `0x000019A2` | 2026-07-31 |
| 2 | `FLASH_CTRL` against WRAM | `0x00002BF2` | `0x00002BF2` | `0x00002BF2` | `0x00002BF2` | 2026-07-31 |
| 3 | a real BIOS ARM helper against a WRAM copy | `0x0000802D` | `0x00006A33` | `0x0000802D` | `0x00006A33` | 2026-07-31 |
| 4 | a real BIOS Thumb helper against a WRAM copy | `0x0000B71B` | `0x0000B371` | `0x0000B71B` | `0x0000B371` | 2026-07-31 |

Screens 2, 3, and 4 agree exactly. The control value of screen 1 is 1 tick apart, which is inside the jitter.

## Screen 5: the raw Timer0 diagnostic

These are raw counter snapshots, and not deltas.

| Source | single_before | single_after | full_before | full_after | Date |
|---|---|---|---|---|---|
| Real hardware | `0x0000FFEF` | `0x0000FFEE` | `0x00006269` | `0x0000E23C` | 2026-07-31 |
| This emulator | `0x0000FFF0` | `0x0000FFEF` | `0x0000626A` | `0x0000E23D` | 2026-08-06 |

The sanity check for this screen is `(full_before − full_after) mod 65536`. Both sources give `0x802D`, which agrees with the test value of screen 3 exactly. Thus the raw counter data is internally consistent on both sides.

## Screens 6 to 11: timer and interrupt cost

| Screen | What it measures | Hardware test | Hardware control | Emulator test | Emulator control | Hardware date |
|---|---|---|---|---|---|---|
| 6 | the Timer2 period behavior | `0x00003F90` | `0x00003F87` | `0x00003F91` | `0x00003F88` | 2026-07-31 |
| 7 | the cost of one interrupt | `0x00002BF2` | `0x00002D56` | `0x00002BF2` | `0x00002D4E` | 2026-07-31 |
| 8 | the expiry-to-arm latency over IRQ | `0x00000FE4` | `0x000003F8` | `0x00001060` | `0x000003F8` | 2026-07-31 |
| 9 | the write cost of `IRDA_DATA` against WRAM | `0x00002BF2` | `0x00002849` | `0x00002BF2` | `0x00002849` | 2026-07-31 |
| 10 | the expiry-to-arm latency over FIQ | `0x00000FE4` | `0x000003F8` | `0x00001080` | `0x000003F8` | 2026-07-31 |
| 11 | the same latency, with the full dispatch chain | `0x00000FE4` | `0x000003F8` | `0x00001103` | `0x000003F8` | 2026-07-31 |

The control row of screens 8, 10, and 11 is the armed period, which is a constant that the app writes. That value is `0x3F8`, which is 1016. It is identical on both sides by construction.

**The derived values of screen 7**, through `3200 * (1 - test/control)`: real hardware gives approximately 98 raw cycles for one interrupt. This emulator gives approximately 96 raw cycles. That is a difference of 2.2%.

**The derived values of screens 8, 10, and 11**, through `top * 32 / 64 / 2` against an armed period of 1017 ticks:

| Screen | Hardware effective period | Hardware latency | Emulator effective period | Emulator latency |
|---|---|---|---|---|
| 8 | 1017 | **0 ticks** | 1048 | 31 ticks |
| 10 | 1017 | **0 ticks** | 1056 | 39 ticks |
| 11 | 1017 | **0 ticks** | 1088.8 | 71.8 ticks |

The period is 1017, and not 1016, because a timer that is armed with P runs P+1 ticks. See screen 6.

## Screen 13: does bit 0 of CLK control stop the CPU?

This screen is interactive. A person presses DOWN to run it, and then waits before a button press wakes the device. Thus `bench_probe` cannot run it. The emulator row below comes from an interactive control run.

| Source | Seconds stopped | Timer1 IRQs | `CLK control` readback | Date |
|---|---|---|---|---|
| Real hardware, with a wait of approximately 10 seconds | `0x0000000A` | `0x00000000` | `0x00000017` | 2026-08-03 |
| This emulator, with a stop of five seconds | `0x00000005` | `0x00000000` | see below | 2026-08-03 |

The seconds row compares directly, because the emulated RTC keeps true 1Hz time. Thus that row must agree with the wall-clock time of the wait, on either side.

**The readback row of the emulator needs a new run.** The recorded run gave `0x00000000`, and that value came from the model that returned the stored control word. `clk_read8` in `core/src/clk.c` now models the measured hardware behavior: both words read back as `CLK_MODE` with the steady bit (`0x10`) ORed in. This app sets `CLK_MODE 7`, thus the modeled readback is now `0x17`. No interactive run has confirmed that value since the correction.

## Screen 14: the two interrupt-line rates of the RTC

This screen runs at startup, but `bench_probe` does not read its two WRAM slots. The emulator row below comes from a recorded control run.

| Source | Paused (top) | Running (bottom) | Date |
|---|---|---|---|
| Real hardware | `0x00000F3F` | `0x00003D00` | 2026-08-03 |
| This emulator | `0x00000F44` | `0x00003D00` | 2026-08-03 |

Real hardware gave the paused value twice, as `0x0F40` and then `0x0F3F`. Those two values are one tick apart, which is the resolution of this measurement, at 0.026%.

## Screen 15: when does the COM block raise its interrupt?

This screen runs by itself while the device is in the slot, and `bench_probe` does not run it.

### A run with no clear of `COM_STAT2` (real hardware, 2026-10-08)

This run used a form of the screen that does not write `COM_CTRL2` before a capture, with `COM_CTRL1` = 0, 1, 2, 3, 0, 1, 2, 3 for captures 0 to 7. Its rows were: top as now; row 2 Ready and HOLD bit 6; row 3 `COM_STAT2` bits 1 and 2; row 4 `COM_STAT1` bits 0 and 1; row 5 `COM_STAT1` bit 2, the number of changes, and the last sample. Its bit order for "bits before" was `COM_STAT1` bits 0 to 2, `COM_STAT2` bits 0 to 2, and HOLD bit 6. A PS1 memory card screen accessed the slot.

| Capture | `COM_CTRL1` | Top row | Row 4 | Row 5 |
|---|---|---|---|---|
| 0 | 0 | `F0024B0B` | `FFFF0000` | `FFFF0202` |
| 1 | 1 | `F10A4B69` | `007A0000` | `FFFF0D0A` |
| 2 | 2 | `F2124B69` | `00860000` | `FFFF0D12` |
| 3 | 3 | `F31A4B69` | `00870000` | `FFFF0D1A` |
| 4 | 0 | `F4024B69` | `00870000` | `FFFF0D02` |
| 5 | 1 | `F50A4B69` | `00860000` | `FFFF0D0A` |
| 6 | 2 | `F6124B69` | `00860000` | `FFFF0D12` |
| 7 | 3 | `F71A4B69` | `007A0000` | `FFFF0D1A` |

Rows 2 and 3 were `FFFFFFFF` in each capture. A repeated run gave capture 2 again, digit for digit.

What these values give:

- **"Bits before" is `02`, `0A`, `12`, `1A` for `COM_CTRL1` = 0, 1, 2, 3.** `0x08` is `COM_STAT2` bit 0 and `0x10` is `COM_STAT2` bit 1. Thus a read of `COM_STAT2` shows only the bits that `COM_CTRL1` selects, and the bits stay set between accesses. The app read `COM_DATA` before it took the "bits before" sample, thus that read does not clear Ready.
- **`COM_STAT1` bit 0 sets at 122 to 135 ticks (61 to 67us)** after the selection, with each value of `COM_CTRL1`. Capture 0 is the exception: 2 changes only, with no byte. It is probably a different kind of access.
- **HOLD bit 6 never set.** The window was `4B0B` or `4B69`, which is the same sample time as this emulator.

See "What real hardware shows in `COM_STAT2`" in [docs/hardware-notes.md](../docs/hardware-notes.md).

### The screen as it is

The emulator rows below come from a recorded control run, with the model of `COM_STAT2` that the real-hardware run below gives. In that run the PS1 accesses the slot every 20ms: it selects the device, the byte is complete 62us later, it gives no acknowledge, and it releases /SEL 60us after the byte. The first change of a capture is the selection, or the sample after it.

| Source | Captures | Ready | HOLD bit 6 | `COM_STAT2` bit 1 | `INT_INPUT` bit 6 | `COM_STAT1` bit 0 | Release of /SEL | Window | Date |
|---|---|---|---|---|---|---|---|---|---|
| This emulator | 2, 4, 5 | `007D` | `0000` | `0000` | `0000` | `007D` | `00F9` (last change) | `6310` | 2026-10-08 |
| This emulator | 1, 3 | `0085` | `0000` or `001A` | `0000` or `001A` | `0000` | `0085` | `0101` (last change) | `6318` | 2026-10-08 |
| This emulator | 6 (`COM_CTRL1 = 1`) | `006C` | `006C` | `FFFF` | `006C` | `0085` | `00F1` (last change) | `6318` | 2026-10-08 |
| This emulator | 7 (`COM_CTRL1 = 2`) | `FFFF` | `0000` | `0000` | `0000` | `0085` | `0101` (last change) | `6318` | 2026-10-08 |
| Real hardware | 0 | `FFFF` | `0000` | `0000` | `0000` | `FFFF` | `001A` (last change) | `6308` | 2026-10-08 |
| Real hardware | 1 | `0096` | `0000` | `001A` | `0000` | `0096` | `03CA` (last change) | `636E` | 2026-10-08 |
| Real hardware | 2 | `0085` | `0000` | `001A` | `0000` | `009E` | `03D2` (last change) | `6376` | 2026-10-08 |
| Real hardware | 3 | `007D` | `0000` | `0000` | `0000` | `007D` | `03C1` (last change) | `6365` | 2026-10-08 |
| Real hardware | 4 | `0096` | `001A` | `001A` | `0000` | `0096` | `03C9` (last change) | `636D` | 2026-10-08 |
| Real hardware | 5 | `008D` | `0000` | `0000` | `0000` | `008D` | `03C1` (last change) | `6365` | 2026-10-08 |
| Real hardware | 6 (`COM_CTRL1 = 1`) | `007D` | `007D` | `FFFF` | `007D` | `007D` | `03C1` (last change) | `6365` | 2026-10-08 |
| Real hardware | 7 (`COM_CTRL1 = 2`) | `FFFF` | `0000` | `0000` | `0000` | `008E` | `03D2` (last change) | `6365` | 2026-10-08 |

The raw rows of the real-hardware run:

| Capture | Top | Row 2 | Row 3 | Row 4 | Row 5 |
|---|---|---|---|---|---|
| 0 | `F0026308` | `FFFF0000` | `00000000` | `FFFF0000` | `023A001A` |
| 1 | `F102636E` | `00960000` | `001A0000` | `0096001A` | `0E3E03CA` |
| 2 | `F2026376` | `00850000` | `001A0000` | `009E001A` | `0F3E03D2` |
| 3 | `F3026365` | `007D0000` | `00000000` | `007D0000` | `0D3E03C1` |
| 4 | `F402636D` | `0096001A` | `001A0000` | `0096001A` | `0E3E03C9` |
| 5 | `F5026365` | `008D0000` | `00000000` | `008D0000` | `0D3E03C1` |
| 6 | `F6026365` | `007D007D` | `FFFF007D` | `007D0000` | `0D3603C1` |
| 7 | `F7026365` | `FFFF0000` | `00000000` | `008E0000` | `0D3A03D2` |

What these values give:

- **"Bits before" is `02` in each capture.** Thus `COM_CTRL2 = 3` clears both bits of `COM_STAT2`.
- **`INT_COM` sets at the selection** with `COM_CTRL1` = 3 and 2, in the same sample as `COM_STAT2` bit 1, or with a split across two samples (`001A` against `0000`). The first change of an access is the selection: `COM_STAT1` bit 1 clears.
- **With `COM_CTRL1 = 1`, `INT_COM` sets at Ready** (`007D`), and `COM_STAT2` bit 1 never shows.
- **With `COM_CTRL1 = 2`, Ready never shows,** and the byte still sets `COM_STAT1` bit 0 (`008E`).
- **Ready sets 107 to 124 ticks after the selection** (53 to 62us), within one sample of `COM_STAT1` bit 0.
- **The access ends at 961 to 978 ticks** (approximately 485us). The last sample has `COM_STAT1` bit 1 set again (the release) and `COM_STAT1` bit 0 clear.
- **Capture 0 has no byte,** like capture 0 of the run with no clear: the selection and a release 13us later.
- **The window is `6308` to `6376`,** against `6307` in this emulator.

See "What real hardware shows in `COM_STAT2`" in [docs/hardware-notes.md](../docs/hardware-notes.md).

This emulator gives the same pattern as real hardware: `INT_COM` at the selection with `COM_CTRL1` = 3 and 2, `INT_COM` at Ready with `COM_CTRL1 = 1`, and each masked bit never. Times of `001A` against `0000` are one event split across two samples, as on real hardware. See "The resolution" in [README.md](README.md). The release comes sooner here than on real hardware, because the PS1 of the control run releases /SEL 60us after the byte.

## Screens 16 to 18: what does a misaligned load give?

These values are exact. They have no jitter. See "Screens 16 to 18" in [README.md](README.md) for the meaning of each candidate value.

**Real hardware:** 2026-10-13. **This emulator:** a run on 2026-10-13 that read the WRAM result block at `0x540`.

| screen | row | real hardware | this emulator |
|---|---|---|---|
| 16 (`LDR`) | ARM +1 | `11843322` | `11843322` |
| | ARM +3 | `33221184` | `33221184` |
| | Thumb +1 | `11843322` | `11843322` |
| | Thumb +3 | `33221184` | `33221184` |
| 17 (`LDRH`) | ARM +1 | `11000022` | `11000022` |
| | ARM +3 | `33000084` | `33000084` |
| | Thumb +1 | `11000022` | `11000022` |
| | Thumb +3 | `33000084` | `33000084` |
| 18 (`LDRSH`) | ARM +1 | `00000022` | `00000022` |
| | ARM +3 | `FFFFFF84` | `FFFFFF84` |
| | Thumb +1 | `00000022` | `00000022` |
| | Thumb +3 | `FFFFFF84` | `FFFFFF84` |

## Screens 19 to 28: the CPU rules of GBATEK, on real hardware

See "Screens 19 to 28" in [README.md](README.md) for the test of each row, and for the value that GBATEK gives. Screens 24 and 25 are Timer0 ticks. Each other screen gives exact values.

**Real hardware:** 2026-10-16. **This emulator:** a run of `tools/bench_probe.c` on 2026-10-16, after the corrections of the CPU core that these screens confirm. That tool read the WRAM result block at `0x570`.

| screen | row 1 | row 2 | row 3 | row 4 | source |
|---|---|---|---|---|---|
| 19 (`SWP`) | `11843322` | `A1B2C3D4` | `88776655` | `33221184` | Real hardware |
| | `11843322` | `A1B2C3D4` | `88776655` | `33221184` | This emulator |
| 20 (R15 read) | `0C0C0808` | `0C0C0C00` | `00000000` | `00000000` | Real hardware |
| | `0C0C0808` | `0C0C0C00` | `00000000` | `00000000` | This emulator |
| 21 (ARM base in list) | `11111111` | `22222222` | `00000000` | `00000008` | Real hardware |
| | `11111111` | `22222222` | `00000000` | `00000008` | This emulator |
| 22 (Thumb base in list) | `11111111` | `22222222` | `00000000` | `00000008` | Real hardware |
| | `11111111` | `22222222` | `00000000` | `00000008` | This emulator |
| 23 (`PUSH`, misaligned SP) | `11223344` | `00000000` | `00000002` | `11223344` | Real hardware |
| | `11223344` | `00000000` | `00000002` | `11223344` | This emulator |
| 24 (Thumb `MUL` cost) | `000019A3` | `000024A0` | `000019A3` | `000024A0` | Real hardware |
| | `000019A3` | `0000249F` | `000019A3` | `0000249F` | This emulator |
| 25 (`UMULL` cost) | `00002BF2` | `000036EE` | `00002BF2` | `000036EE` | Real hardware |
| | `00002BF2` | `000036EF` | `00002BF2` | `000036EE` | This emulator |
| 26 (empty list) | `0040000C` | `00400001` | `00400006` | `00400001` | Real hardware |
| | `0040000C` | `00400001` | `00400006` | `00400001` | This emulator |
| 27 (misaligned R15) | `0200000E` | `0200000E` | `0200000E` | `0200000E` | Real hardware |
| | `0200000E` | `0200000E` | `0200000E` | `0200000E` | This emulator |
| 28 (undefined instruction) | `00000004` | `00009B10` | `00000002` | `00009B30` | Real hardware |
| | `00000004` | `00009B10` | `00000002` | `00009B30` | This emulator |

Each real-hardware value of screens 19 to 26 and 28 agrees with the value that GBATEK gives. Screen 26 also gives the stored value of R15, which GBATEK does not give: PC+12 for ARM, and PC+6 for Thumb. The timing rows of screens 24 and 25 agree with this emulator to the tick, except for the row that tests the rule.

Screen 27 runs its tests with the traps of screen 29, and its target ends each fetch rule in a known way. Real hardware gives `0200000E` in each row: R15 keeps bit 1, the fetch in FLASH1 gives the halfword in both halves, and the trap catches the undefined word at the target + `0xE`. GBATEK gives `01000008`. A version of screen 27 with no traps and a different target stopped a real unit, with a blank LCD.

This emulator agrees with each real-hardware value of screens 19 to 28. The timing rows of screens 24 and 25 agree within the jitter of 1 tick.

## Screen 29: the probe of a misaligned write to R15

See "Screen 29" in [README.md](README.md) for the meaning of each probe and each row. **Real hardware:** 2026-10-16, a J-110 BIOS. Each probe ran one time.

Probes 6 to 9, E, and 11 give values that depend on the addresses of the code in the build. Real hardware ran those probes on two earlier builds: probes 0 to D on the build before probes E to 11, and probes E to 11 on the build before probes 12 and 13. **This emulator** gives the values below for those same builds, after the corrections of the CPU core. The committed build gives different values for these probes, on real hardware and in this emulator. For example, in the committed build, probes 6 to 9 stop at the undefined target of probe E, which comes after their target.

| probe | real hardware | this emulator |
|---|---|---|
| 0 (control: aligned `MOV PC`) | `00010000 00000008 00000000 00000000` | the same |
| 1 (control: undefined instruction) | `01020000 00000004 60000010 00000000` | the same |
| 2 (control: watchdog) | `02060000 00000004 60000010 00000000` | the same |
| 3 (`LDR` from FLASH1 at +2) | `03010000 84338433 84332211 88776655` | the same |
| 4 (`LDR` from BIOS ROM at +2) | `04010000 04000400 04000004 040001D0` | the same |
| 5 (`LDR` from WRAM at +2) | `05010000 22118433 84332211 88776655` | the same |
| 6 (`MOV PC`, FLASH1 target + 2) | `06040000 000013C2 00000010 00000000` | the same |
| 7 (`LDR PC`, FLASH1) | `07040000 000013C2 00000010 00000000` | the same |
| 8 (`LDMIA {PC}`, FLASH1) | `08040000 000013C2 00000010 00000000` | the same |
| 9 (`BX`, FLASH1) | `09040000 000013C2 00000010 00000000` | the same |
| A (`MOV PC`, WRAM target + 2) | `0A010000 0000000A 00000000 00000000` | the same |
| B (`LDR PC`, WRAM) | `0B010000 0000000A 00000000 00000000` | the same |
| C (`LDMIA {PC}`, WRAM) | `0C010000 0000000A 00000000 00000000` | the same |
| D (`BX`, WRAM) | `0D010000 0000000A 00000000 00000000` | the same |
| E (the fetch rule, FLASH1) | `0E020000 0000000A 20000010 91FFD332` | the same |
| F (the fetch rule, WRAM) | `0F020000 0000000A 20000010 00000000` | the same |
| 10 (aligned `MOV PC` to `0x02004000`) | `10040000 00000004 60000010 00000000` | the same |
| 11 (`LDR` at `0x02004000`) | `11050000 FFFFEB40 60000010 FDFFC628` | the same |
| 14 (the decode of the `MSR` space) | `14010000 00000010 20000010 50000010` | the same |
| 15 (`MSR` with `ASR`, `ROR`, and `RRX`) | `15010000 C0000010 50000010 90000010` | the same |
| 16 (reserved bits, and `MSR #imm` with bits 12 to 15 clear) | `16010000 50000010 50000010 00000000` | the same |
| 17 (`E12FF150`) | `17010000 00000001 EEEEEEEE 00000010` | the same |
| 18 (`E12FFF30`) | `18010000 00000001 EEEEEEEE 00000010` | the same |
| 19 (`E12F0010`) | `19010000 00000000 EEEEEEEE 00000010` | the same |
| 1A (`E12FFF70`) | `1A010000 00000001 EEEEEEEE 00000010` | the same |

The same unit, with a build of three blocks (`make BLOCKS=3`), on 2026-10-16. In that build, `0x02004000` is a target in the third block: `MOV r1,PC`, and then `BX r3`.

| probe | real hardware, `BLOCKS=3` | this emulator, `BLOCKS=3` |
|---|---|---|
| 10 (aligned `MOV PC` to `0x02004000`) | `10010000 00000008 00000000 00000000` | the same |
| 11 (`LDR` at `0x02004000`) | `11010000 E1A0E1A0 E1A0100F E12FFF13` | the same |
| 12 (aligned `MOV PC` to `0x02006000`) | `12040000 00000004 60000010 00000000` | the same |
| 13 (`LDR` at `0x02006000`) | `13050000 FFFFCB60 60000010 FDFFA628` | the same |

What these values show:

- **A write of a misaligned address to R15 keeps bit 1, in the ARM state.** `MOV PC`, `LDR PC`, `LDMIA {PC}`, and `BX` all give the same result. In WRAM, the instruction at the target reads R15 as the target + `0xA`. GBATEK says that R15 is always force aligned. Real hardware does not do that.
- **A word read at an address 2 above a word boundary depends on the region.** WRAM gives the aligned word, and the CPU then rotates it, the same as screen 16. FLASH1 and BIOS ROM give the halfword at that address in both halves. The rotation of the CPU does not change that value.
- **An instruction fetch follows the same rule as a data read.** Probe E puts `13A52F01` at the target in FLASH1. Only the halfword `13A5` in both halves gives `MOVNE r1,#0x94000002`, and row 4 is that value minus the target (`0x02002CD0`). Probe F, with the same target in WRAM, executes the aligned word, and r1 stays 0. Thus in FLASH1, a misaligned R15 does not execute the instruction at the aligned target.
- **A fetch or a data read in FLASH1 after the end of the app causes an abort.** Probe 10 makes an aligned jump to `0x02004000`, the first address after the 16KB app. The first fetch causes a prefetch abort, with LR = the target + 4. Probe 11 reads `0x02004000` + 2. The first `LDR` (`0x02002B38`) causes a data abort, and it does not write its destination register: r1 keeps `0x628`, its value before the `LDR`.
- **Probes 6 to 9 are a result of the two rules above.** The CPU executed each duplicated halfword to the end of the app, with bit 1 of R15 set, and it did not branch. The first fetch after the end of the app, at `0x02004002`, caused the prefetch abort.
- **The kernel sends the prefetch abort to its loop with no exit at `0x040001C8`.** Thus a misaligned R15 in FLASH1 stops the device if no handler of the app catches the abort.
- **The abort boundary is the end of the blocks of the app, and not a fixed address.** In the build of two blocks, an access at `0x02004000` causes an abort. In the build of three blocks, the same access executes the target, and the abort moves to `0x02006000`. Probe 11 of the build of three blocks also shows the duplicated halfword (`E1A0E1A0`) in the third block.
- **The decode of `MSR` and `BX` ignores bits that GBATEK gives as fixed.** Probes 14 to 16 show that `MSR` ignores bits 12 to 15, that bits 4 to 11 of the register form are a shift of Rm, and that the CPSR does not keep bits 24 to 27. Probes 17 to 1A show that `cond 0001 0010 1111 1111 xxxx 0xx1 Rm` is `BX`, with no link. Probes 6 to 9 execute `E12FE12F`, which is `MSR CPSR_fsxc,PC,LSR #2`. That word clears the flags before the abort. See "The decode of MSR and BX" in `docs/hardware-notes.md`.
- **This emulator agrees with each value.** It executes the same words to the same abort address in probes 6 to 9.

## What these values settle

- **`FLASH_CTRL` gets the fast 1-cycle data-access rate of WRAM.** Screen 2 gives a test value that is exactly equal to its control value. It does not get the slow 2-cycle rate.
- **A BIOS opcode fetch follows the ARM and Thumb division of FLASH.** Screen 3 gives a real BIOS ARM helper that is clearly slower than its WRAM copy. Screen 4 gives a real BIOS Thumb helper that is almost equal to its WRAM copy.
- **The measurement method is sound.** Screen 1 gives a ratio of approximately 1.7:1 between ARM and Thumb. The loop overhead and the call overhead dilute the ratio from a pure 2:1 opcode-fetch signal.
- **The 16-bit Timer0 mask correction holds on real hardware.** Screens 3 and 4 give clean, small values. Neither one shows the `0xFFFFxxxx` wraparound artifact.
- **A timer that is armed with period P expires each P+1 ticks.** Screen 6 measures exactly 1 extra tick for each reload, at two different periods, and the extra time is the same absolute value at both. Thus a rate error and a divisor error are not the cause. `core/src/timer.c` has this correction, and three timer values in `tests/cpu_test.c` were corrected with it.
- **The interrupt entry and dispatch cost of this emulator is essentially correct.** Screen 7 gives approximately 98 raw cycles on hardware, against approximately 96 raw cycles here.
- **A timer expiry reaches the arm operation of its handler with 0 added ticks.** Screens 8, 10, and 11 give that same result over IRQ, over FIQ, and with the full realistic dispatch chain of the real transmit handler. Thus Timer2 reloads in hardware at the moment that it expires, independently of the time when software services the interrupt. The arm write must only occur before the *next* natural expiry.
- **The write cost of `IRDA_DATA` is not special.** Screen 9 agrees with this emulator bit for bit. The general 2-cycle "I/O" cost that this emulator gives that register is already correct.
- **Bit 0 of `CLK control` (`0x0B000004`) stops the CPU, and the timers stop with it.** Screen 13 makes that store alone, with no other part of the power-down sequence of the real app. Ten seconds of RTC time passed, and there were zero Timer1 interrupts across those seconds, with Timer1 armed and unmasked for the full time. No quantity of tracing of the real app can give that separation.
- **`CLK control` does not read back the value that software wrote to it.** Screen 13 returns `0x17`, which is the `CLK_MODE` value of the app with the steady bit ORed in. One consequence: this screen cannot answer whether the stop bit clears itself, because there is no readable stop status. The run does show that the CPU started again and continued. Thus the stop does not persist across a wake.
- **Both documented RTC rates are waveform rates, at 1Hz and 4096Hz, with two transitions for each pulse.** Screen 14 gives 256 transitions in 0.031250 seconds while the RTC is paused, and four transitions in exactly 2.000 seconds while it runs.
- **`CLK_MODE 7` really is 3,997,696Hz.** That figure had only ever come from documentation. A screen 14 result of exactly 0.031250 seconds is not possible if the real CPU rate is meaningfully different.
- **A misaligned load gives one rule for each instruction, and the ARM state and the Thumb state agree.** `LDR` rotates the aligned word right by 8 bits for each byte of misalignment. `LDRH` rotates the aligned halfword right by 8 bits in 32 bits, thus the byte below the address goes to bits 24 to 31. `LDRSH` is a sign-extended byte load from the odd address. Screens 16 to 18 give these values.

## Known differences between this emulator and real hardware

- **This emulator applies too much cost to a dispatch that reaches an arm operation.** Screens 8, 10, and 11 give 31, 39, and 71.8 ticks here, against 0 ticks on hardware. The cost grows with the complexity of the dispatch, and real hardware shows no such growth. This is a real cycle-cost fault. See "Known open questions and unconfirmed behavior" in [docs/hardware-notes.md](../docs/hardware-notes.md).
- **The paused RTC rate of this emulator is 0.1% under hardware.** Screen 14 gives `0x0F44` here, against `0x0F3F` on hardware. Integer rounding in `RTC_TICK_CYCLES_PAUSED` causes that difference.
- **The interrupt cost of this emulator is 2.2% under hardware.** See screen 7 above.
- The remaining differences are 1 tick or less, which is inside the run-to-run jitter of the hardware itself.
