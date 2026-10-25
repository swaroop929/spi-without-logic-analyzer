# SPI reverse engineering without a logic analyzer

Record real SPI traffic with an **STM32 NUCLEO-U083RC** acting as its own logic analyzer, decode it with
[REA](https://github.com/morluto/rea) (sigrok under the hood), and work out an unknown protocol from the bytes alone.

No logic analyzer, no extra hardware: one or two dev boards and a few jumper wires.

```
 Part 1:  Nucleo SPI1 (controller) ──4 wires──> Nucleo SPI2 ("unknown device")
                    └── the Nucleo's CPU samples the 4 pins ──USB──> PC: record_spi.py → capture.vcd → REA

 Part 2:  FPB-RA0E1 ("unknown device") ──4 wires──> Nucleo (listen only) ──USB──> PC → REA
```

## How the recorder works

The Nucleo's CPU reads the four SPI pins (clock, MOSI, MISO, CS) in a tight, branch-free loop with a fixed number
of cycles per pass, timed with SysTick. It stores one byte per sample in RAM, then prints the recording over the
ST-LINK virtual COM port as a **VCD** file, the same text format real logic analyzers export.

| | Part 1 (self-recording) | Part 2 (`LISTEN_ONLY 1`) |
|---|---|---|
| Who drives the bus | Nucleo SPI1 → SPI2 | another board |
| Cycles per sample (16 MHz) | ~50 | ~9 |
| Samples | 30,000 | 30,000 (~17 ms) |

## Parts

1. **[Self-recording](docs/1-self-recording.md):** SPI1 talks to SPI2 on the same board while the CPU records
   both. Puzzle: work out a small flash/sensor-style command set. ([answers](docs/1-answers.md))
2. **[A second board](docs/2-ra0e1-device.md):** a Renesas FPB-RA0E1 sends a framed protocol, the Nucleo only
   listens. Puzzle: work out the frame format. ([answers](docs/2-answers.md), [real decode](examples/round2-decode.txt))

## Files

| Path | What it is |
|---|---|
| `nucleo-recorder/spi_recorder.c`, `.h` | Nucleo firmware. Add to a CubeIDE project and call `spi_recorder_run()` from `main()`. |
| `pc/record_spi.py` | Sends `c` to the board and saves the VCD (`pip install pyserial`). |
| `ra0e1-device/ra0e1_device_code.c` | RA0E1 "unknown device" (bit-banged SPI). Replaces `hal_entry()` in an e² studio project. |
| `examples/round2-decode.txt` | Two real Part 2 recordings as decoded by REA. |

## Quick start

```powershell
python pc/record_spi.py COM5 capture.vcd
```

```bash
node scripts/rea.mjs decode-signal-recording capture.vcd --input_format vcd \
  --decoders spi:clk=clk:mosi=mosi:miso=miso:cs=cs \
  --annotations spi=mosi-transfer:miso-transfer --all_lines
```

REA's signal decoding needs Linux (it wraps `sigrok-cli` and `prlimit`); on Windows, run it in WSL.

## Things I learned the hard way

- A Debug (-O0) build of the capture loop took 98 cycles per sample. `optimize("O2")` on just that function
  halved it.
- On the FPB-RA0E1, not every pin printed in the PMOD pinout reaches the connector by default. If a line shows
  no activity, try another pin before blaming the code.
- Plug the wires into the connector you think you're using. 🙂

## License

[MIT](LICENSE)
