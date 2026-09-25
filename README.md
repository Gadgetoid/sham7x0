# zq77x-emu

Proof of concept emulator for the Sharp OZ/ZQ-7xx organisers, running the real firmware behind the Pocket front-end (bezel, keyboard, LCD simulation).

The firmware is not included. Put `r162.da1` from the Sharp System Update Utility v1.62 in `rom/`.

## Build

macOS, SDL3 and the imgui submodule.

```
git submodule update --init
brew install sdl3
make
make run
make headless   # CLI harness: run N seconds, dump the screen
```

## First run

A blank machine reports "memory not initialized". Choose Run > Initialize Memory (or type `init` in the console) to reset with ON held, then press ENTER to initialise. The setup wizard follows.

Console commands: `reset`, `init`, `on`, `save`, `pc`, `trace on`, `trace off`. Cmd-R resets, the bezel POWER key presses ON.

The machine state (CPU, RAM, flash data area, clock) is saved to `data/state.bin` on exit and every minute, and restored on launch with the clock advanced by the time away. `--fresh` ignores it. Screenshot runs don't touch it.

Clicked keyboard keys go straight to the key matrix, so Shift, 2nd and CAPS behave as the firmware decides. A clicked left Shift stays down until the next key. Host typing and the bezel keys are translated to matrix presses.

```
./zq77x-emu --exec=init --keys="{WAIT}...{ENTER}"
./headless rom/r162.da1 --seconds=10 --keys=0:99.0/1.5,2:6.6/0.3 --pbm=out.pbm
```

Headless keys are `SECONDS:COLUMN.ROW/HOLD`. Column 99 is the ON key.

## Hardware notes

From the firmware and [ozdev](https://github.com/arpruss/ozdev).

| Area | Detail |
|---|---|
| CPU | Z80, IM 1, 6 MHz assumed |
| 0000-7FFF | flash offset 0 |
| 8000-9FFF | 8KB page from ports 1/2, physical page = value + 4 |
| A000-BFFF | 8KB page from ports 3/4, physical page = value |
| C000-FFFF | fixed RAM pages 402-403 |
| Pages 000-17F | flash, firmware in 000-047, data from 048 |
| Page 300 | LCD control word: bit 7 on, bit 6 blank, bits 0-5 contrast, bit 8 backlight |
| Pages 400+ | RAM, display at 400 or 404 (port 22/23) |
| Ports 5/6/7 | interrupt status / acknowledge / mask. Bit 0 keyboard, 4 1Hz RTC, 5 64Hz tick, 7 ON key |
| Port 8 | sleep before HALT |
| Ports 10/11/12 | keyboard rows / columns 0-7 / columns 8-10. Keycode table at 0x23a3. Column 9 rows 0-4 are MAIN, TEL, CAL, MEMO, PROG |
| Port 12 read | status inputs, bits 7, 6, 5, 3 (battery, switch) |
| Port 46 bit 4 | ON key |
| Ports 30-3F | RP5C01-style RTC, one BCD nibble per register |
| Ports 16-19 | sound: 19 tone mode, 17/18 divisor, 16 bit 0 on. 16384 / (divisor + 2) Hz |
| Ports 40-47 | UART |
| Flash | Intel/Sharp command set (FF, 40, 20/D0, 50, 70, 90), 64KB erase blocks |

## Not done

- Port 12 status inputs always report battery and switch OK.
- Serial and IrDA are stubs, so PC sync and add-on installs don't work.
