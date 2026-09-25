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

Console commands: `reset`, `init`, `on`, `pc`, `trace on`, `trace off`. Cmd-R resets, the bezel POWER key presses ON.

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
| Page 300 | LCD control word, low 6 bits contrast |
| Pages 400+ | RAM, display at 400 or 404 (port 22/23) |
| Ports 5/6/7 | interrupt status / acknowledge / mask. Bit 0 keyboard, 4 1Hz RTC, 5 64Hz tick, 7 ON key |
| Port 8 | sleep before HALT |
| Ports 10/11/12 | keyboard rows / columns 0-7 / columns 8-10. Keycode table at 0x23a3. Column 9 rows 0-4 are MAIN, TEL, CAL, MEMO, PROG |
| Port 12 read | status inputs, bits 7, 6, 5, 3 (battery, switch) |
| Port 46 bit 4 | ON key |
| Ports 30-3F | RP5C01-style RTC, one BCD nibble per register |
| Ports 16-19 | sound |
| Ports 40-47 | UART |
| Flash | Intel/Sharp command set (FF, 40, 20/D0, 50, 70, 90), 64KB erase blocks |

## Not done

- 2nd functions go through the front-end's key translation, so some are lost.
- No persistence: flash data and RAM are lost on exit.
- No sound, LCD contrast or LCD power.
- Port 12 inputs, serial and IrDA are stubs.
