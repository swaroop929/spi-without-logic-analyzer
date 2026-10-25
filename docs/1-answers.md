# Answer key for the recorder's "unknown device"

**Spoiler. Try [Step 8 of the guide](1-self-recording.md#step-8-reverse-engineer-it) first.**

The device is modelled on an SPI flash or sensor chip with a JEDEC-style command set. Each transfer
starts with a 1-byte command on MOSI. MISO is `FF` while the device is still receiving the command.

| # | MOSI | MISO | Meaning |
|---|---|---|---|
| 1 | `9F 00 00` | `FF C2 17` | **Read ID** (0x9F). Manufacturer `C2`, device `17`. |
| 2 | `01 03` | `FF FF` | **Write config** (0x01) with value `03`. The device answers nothing. |
| 3 | `05 00` | `FF 01` | **Read status** (0x05). Bit 0 = 1 means **busy** (still applying the config). |
| 4 | `05 00` | `FF 00` | **Read status** again. Bit 0 = 0 means **ready**. |
| 5 | `03 10 00 00` | `FF FF 09 C4` | **Read data** (0x03) from address `0x10`. It returns 2 bytes. |

Transfer 5: `09 C4` = 0x09C4 = 2500. In units of 0.01 °C that's **25.00 °C**, a temperature reading.

Why this is a typical pattern:

- **Command byte first**, then arguments (address) and dummy bytes to clock the answer out.
- **Polling a status register** until a busy bit clears is how most SPI chips signal "operation finished".
- Big-endian (high byte first) is common for SPI. The ADXL345 was the opposite (low byte first),
  so always test both orders.
