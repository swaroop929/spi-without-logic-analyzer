# Part 2: record a second board as the "unknown device"

This time the **RA0E1** sends a secret protocol over SPI, and the **Nucleo** only listens and records.
Your job is to work out the protocol from the recording.

```
 FPB-RA0E1 (secret protocol)  ── 4 wires ──>  NUCLEO-U083RC (listen only)  ── USB ──>  PC: record_spi.py → REA
```

Files:

| File | Board | What to do with it |
|---|---|---|
| [`ra0e1-device/ra0e1_device_code.c`](../ra0e1-device/ra0e1_device_code.c) | RA0E1 | Paste into `hal_entry.c` (Step 1). **Try not to read the code.** |
| [`nucleo-recorder/spi_recorder.c`](../nucleo-recorder/spi_recorder.c) | Nucleo | Same firmware as Part 1, built with listen-only mode (Step 2) |

---

## Step 1: Flash the RA0E1

1. Open **e² studio**. Choose any folder as the workspace.
2. **File → New → C/C++ Project → Renesas RA → Renesas RA C/C++ Project → Next.**
3. Project name: `ra0e1_device`. **Next.**
4. **Board: FPB-RA0E1.** The device fills in automatically. Toolchain: GNU ARM Embedded (the default). **Next.**
5. Keep **Executable** and **No RTOS**. **Next.**
6. Choose the template **Bare Metal - Minimal**. **Finish.** If it asks to open the FSP Configuration perspective, click **Yes**.
7. In the Project Explorer, open **`src/hal_entry.c`**. You'll see a function `void hal_entry(void) { ... }`.
   **Delete only that function** (from `void hal_entry(void)` to its closing `}`) and paste the whole contents of
   `ra0e1_device_code.c` in its place.
   - Keep the rest of the file. In particular, `R_BSP_WarmStart()` must stay, because it switches on the
     I/O ports that the code uses.
8. Click the **hammer** (Build). It should finish with `0 errors`.
9. Plug the RA0E1 into USB and click the **bug** icon (Debug). e² studio flashes the board through its on-board
   debugger and then pauses. Press **Resume (F8)** until it runs (usually once or twice).

The RA0E1 now sends its secret messages on the PMOD1 connector, over and over. You won't see anything happen on the
board itself.

## Step 2: Switch the Nucleo to listen-only mode

The Nucleo must **stop driving** the wires, otherwise both boards would push against each other.

1. Use the CubeIDE project from [Part 1](1-self-recording.md).
2. Near the top of the file, change

   ```c
   #define LISTEN_ONLY 0
   ```

   to

   ```c
   #define LISTEN_ONLY 1
   ```

   - `#define` gives a name to a value. With `LISTEN_ONLY` set to 1, the firmware sets all 4 pins as **inputs**,
     leaves SPI1 and SPI2 switched off, and only records.
3. Build and Run (flash) as before.

## Step 3: Wire the boards (power off)

Unplug both USB cables. **Remove the 4 loop-back wires from Part 1.** Then connect 4 male–female wires
(male end into the RA0E1's PMOD1 socket, female end onto the Nucleo's CN10 pins):

| Signal | RA0E1 PMOD1 pin | Nucleo CN10 pin |
|---|---|---|
| CS | 1 | 17 |
| MOSI | 2 | 29 |
| Clock | **3** | 31 |
| GND | 5 | 20 |

- PMOD1 pin 1 is marked on the RA0E1 board (a "1" or a square pad). Pins 1 to 6 are one row.
- Check CN10 pin 20 (GND) against the Nucleo's user manual before powering up.

Plug both boards into the laptop again. The RA0E1 starts sending immediately.

## Step 4: Record and decode

PowerShell (same as before):

```powershell
python record_spi.py COM5 D:\REA\capture.vcd
```

The Nucleo waits until one burst of messages ends, then records about 15 milliseconds, which is enough for about two rounds of
messages. Check the output:

- `Clock pulses seen` should be in the hundreds.
- `CS pulses seen` should be at least 3.
- If you get `ERROR: no CS activity seen`, the CS or GND wire is loose, or the RA0E1 isn't running (press Resume
  in e² studio).

Ubuntu (same as before):

```bash
cp /mnt/d/REA/capture.vcd ~/captures/
cd ~/rea
node scripts/rea.mjs decode-signal-recording ~/captures/capture.vcd --input_format vcd \
  --decoders spi:clk=clk:mosi=mosi:cs=cs \
  --annotations spi=mosi-transfer --all_lines
```

- This device only talks and never answers, so this time we decode **MOSI only** (no `miso=`).

## Step 5: The puzzle

Record **3 or 4 times** (it's different every time) and put all the messages into one table. Then answer:

1. Every message starts with the same byte. What could it be for?
2. What does the 2nd byte tell you? How many different values does it take?
3. Compare the 3rd byte with the length of each message. What's the pattern?
4. Which bytes change from one round to the next, and how? One of them counts, one drifts slowly, one cycles.
5. The last byte of each message looks random. Try combining all the other bytes of that message somehow
   (adding, XOR, …) until you can predict it.
6. Give each message type a name: what kind of device would send these?

Write down your guesses, then compare with [`2-answers.md`](2-answers.md).
