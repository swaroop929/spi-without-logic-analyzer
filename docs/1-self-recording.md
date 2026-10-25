# Part 1: record real SPI traffic with only a NUCLEO-U083RC

No logic analyzer needed. One Nucleo board plays both roles at once:

- **SPI1** is the controller (master). It talks to an "unknown device".
- **SPI2** is that unknown device (slave). It answers with a protocol you will reverse engineer.
- **The CPU** acts as the logic analyzer. While the two SPIs talk over real wires, it reads the four pins
  about 2–3 million times per second. Afterwards it sends the recording to your PC as a `.vcd` file,
  which REA decodes exactly like the accelerometer example.

The signals are real electrical SPI on real pins; only the "device" is simulated by the second SPI block.

Files used:

| File | What it is |
|---|---|
| [`nucleo-recorder/spi_recorder.c` / `.h`](../nucleo-recorder/) | Firmware for the Nucleo |
| [`pc/record_spi.py`](../pc/record_spi.py) | PC script that asks the board for a recording and saves it |
| [`1-answers.md`](1-answers.md) | The solution. **Don't open it until you've tried!** |

---

## Step 1: Wire the board (4 jumper wires)

Use female–female jumper wires. All eight pins are on the **right-hand long double header, CN10**
(the "morpho" connector). The pin pairs sit right next to each other:

| Signal | From (SPI1, controller) | To (SPI2, device) |
|---|---|---|
| Clock (CLK) | CN10 pin 31 (PB3) | CN10 pin 30 (PB13) |
| MOSI | CN10 pin 29 (PB5) | CN10 pin 26 (PB15) |
| MISO | CN10 pin 27 (PB4) | CN10 pin 28 (PB14) |
| Chip select (CS) | CN10 pin 17 (PB6) | CN10 pin 16 (PB12) |

The morpho pins have no printed numbers. Find them in the CN10 pinout figure of the board's user
manual (search "NUCLEO-U083RC user manual" on st.com) before powering up. Two connected outputs
can damage the pins, so check each wire twice. The pin numbers above come from ST's and Zephyr's
board pin maps.

> PB3, PB5 and PB4 are also on the Arduino header as D3, D4 and D5, and PB6 is D10. You can
> use those instead of CN10 pins 31, 29, 27 and 17 if that's easier.

## Step 2: Create the project in STM32CubeIDE

Install **STM32CubeIDE** from st.com if you don't have it (it's free; you need an ST account).

1. **File → New → STM32 Project.**
2. Open the **Board Selector** tab, type `NUCLEO-U083RC` and select it. Click **Next**.
3. Name the project `spi_recorder`, leave *Targeted Language = C*, *Binary Type = Executable*,
   *Project Type = STM32Cube*. Click **Finish**.
4. When it asks **"Initialize all peripherals with their default Mode?"**, click **Yes**.
   This sets up the clock and the USB serial port (USART2) for you.
5. If it offers to open the "Device Configuration Tool" perspective, click Yes. Close the `.ioc`
   pin view when it appears; the code is generated already.

## Step 3: Add the firmware files

1. Copy `spi_recorder.c` into the project folder **`Core/Src`**.
2. Copy `spi_recorder.h` into **`Core/Inc`**.
   (Drag them into those folders in the Project Explorer, choose *Copy files*.)
3. Open **`Core/Src/main.c`** and add two lines, each between the existing `USER CODE` comments:

```c
/* USER CODE BEGIN Includes */
#include "spi_recorder.h"
/* USER CODE END Includes */
```

- `#include "spi_recorder.h"` tells the compiler that a function called `spi_recorder_run()` exists
  in another file, so `main.c` is allowed to call it.

```c
  /* USER CODE BEGIN 2 */
  spi_recorder_run();
  /* USER CODE END 2 */
```

- This line runs after all of ST's start-up code (clock, pins, serial port) and hands control to the
  recorder. It never returns, so the `while (1)` loop below it is never reached.
- Code **must** go between the `USER CODE BEGIN` / `END` markers. If you later regenerate the code
  from the `.ioc` file, anything outside those markers is deleted.

## Step 4: Build and flash

1. Click the **hammer** icon (Build). The console at the bottom should end with `Build Finished. 0 errors`.
2. Plug the Nucleo into USB.
3. Click the green **Run** button. The first time, a launch window opens: click **OK**.
   CubeIDE flashes the board through the on-board ST-LINK.

## Step 5: Find the board's COM port

Open **Device Manager** (right-click Start → Device Manager) → **Ports (COM & LPT)**.
Look for **"STMicroelectronics STLink Virtual COM Port (COMx)"**. Note the number, e.g. `COM5`.

## Step 6: Take a recording

In **PowerShell**:

```powershell
python -m pip install pyserial
cd D:\REA
python record_spi.py COM5 D:\REA\capture.vcd
```

- `python -m pip install pyserial` installs **pyserial**, the Python library for talking to serial ports.
  You only need it once. If `python` isn't found, install Python 3 from python.org and tick
  *"Add python.exe to PATH"* during setup.
- `cd D:\REA` moves to the folder where you saved `record_spi.py` (copy it there first).
- `python record_spi.py COM5 D:\REA\capture.vcd` sends the letter `c` to the board, which means
  "record now". It prints the board's check lines and saves the recording. Replace `COM5` with your port.

You should see something like:

```
CPU clock (Hz): 56000000
Cycles per sample (x100): 2100
Samples: 4600
Bytes sent: 13
Clock pulses seen (expected 8 x bytes): 104
CS pulses seen (expected 5): 5
MISO rising edges seen (0 = check MISO wire): 12
Saved 490 lines to D:\REA\capture.vcd
```

The exact numbers depend on your clock settings. What matters:

- **Clock pulses = 104** (13 bytes × 8 bits). Fewer than that means the clock wire is loose.
- **CS pulses = 5.** That's the number of separate transfers.
- **MISO edges = 0** means the MISO wire (CN10 27 ↔ 28) is missing.

## Step 7: Decode it with REA

In the **Ubuntu** window:

```bash
cp /mnt/d/REA/capture.vcd ~/captures/
cd ~/rea
node scripts/rea.mjs decode-signal-recording ~/captures/capture.vcd --input_format vcd \
  --decoders spi:clk=clk:mosi=mosi:miso=miso:cs=cs \
  --annotations spi=mosi-transfer:miso-transfer --all_lines
```

- `cp /mnt/d/REA/capture.vcd ~/captures/` copies the file from Windows into Linux. WSL shows your
  `D:` drive as `/mnt/d`.
- `--input_format vcd` tells sigrok the file is a VCD (Value Change Dump), a plain-text list of
  "at time T, signal X changed to 0 or 1".
- `--decoders spi:clk=clk:mosi=mosi:miso=miso:cs=cs` maps decoder channels to the signal names inside
  the VCD. This time the names are words, not numbers, because the firmware writes them that way.
  No `cpol`/`cpha` here: the recorder uses SPI mode 0, which is the decoder's default.

As before, each transfer gives two lines with the same sample range: the MISO bytes, then the MOSI bytes.

## Step 8: Reverse engineer it

You now have five transfers between a controller and an unknown chip. Work out what the protocol is:

1. Make a table: one row per transfer, with the MOSI bytes and MISO bytes side by side.
2. Look at the **first MOSI byte** of each transfer. It's probably a command. Which commands repeat?
3. Look at where MISO is `FF`. That usually means "the device isn't saying anything yet".
4. One transfer asks the same thing twice and gets a different answer. What could change between them?
5. The last transfer returns two data bytes. Combine them into a 16-bit number. What physical quantity
   could it be, and in what units?

Write down your guesses, then compare with [`1-answers.md`](1-answers.md).

## Next steps

- **Invent your own protocol.** The conversation lives in the `script[]` table near the top of
  `spi_recorder.c`. Change the bytes, rebuild, record, and decode.
- **Try another SPI mode.** Set `SPI_CR1_CPOL` and/or `SPI_CR1_CPHA` in *both* `SPI1->CR1` and
  `SPI2->CR1` in `spi_init()`. Then see what happens when you decode with the wrong `cpol`/`cpha`.
- **Record a different board.** See [Part 2](2-ra0e1-device.md): a Renesas RA0E1 plays the unknown device and
  the Nucleo only listens.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `No answer from the board` | Wrong COM port, or the board wasn't reset after flashing. Press the black RESET button. |
| `Clock pulses seen: 0` | The firmware isn't reaching the pins. Check that `spi_recorder_run();` is inside `USER CODE BEGIN 2`. |
| Decoded bytes look shifted or wrong | A loose wire, or the MOSI/MISO pair is swapped. |
| Build error `region RAM overflowed` | Other code uses too much RAM. Lower `BUF_SIZE` in `spi_recorder.c` (e.g. to 20000). |
