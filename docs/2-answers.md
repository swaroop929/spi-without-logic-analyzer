# Part 2: the answer

**Spoiler. Try [Step 5 of the guide](2-ra0e1-device.md#step-5-the-puzzle) first.**

Worked out from two real recordings ([examples/round2-decode.txt](../examples/round2-decode.txt)), step by step.

## The data, sorted by the 2nd byte

| Type | Messages |
|---|---|
| `01` | `A5 01 01 AB 0E`, `A5 01 01 AC 09`, `A5 01 01 6C C9`, `A5 01 01 6D C8` |
| `10` | `A5 10 02 00 D9 6E`, `A5 10 02 00 DA 6D`, `A5 10 02 00 DB 6C`, `A5 10 02 00 DC 6B` |
| `20` | `A5 20 03 00 FF 00 79`, `A5 20 03 00 80 FF F9`, `A5 20 03 80 00 FF F9`, `A5 20 03 FF 00 00 79` |

## 1. The first byte `A5` is a sync byte ("start of message")

It never changes. The receiver looks for `A5` to know "a new message starts here". `A5` is a popular choice
because in binary it's `1010 0101`, an alternating pattern that's unlikely to appear by accident on a stuck or
noisy line (stuck lines read `00` or `FF`).

## 2. The second byte is the message type

It takes only 3 values: `01`, `10` and `20`. Each one always has the same shape, so it says *what kind* of
message this is. They always come in the order `01`, `10`, `20`, then repeat.

## 3. The third byte is the payload length

- Type `01` has 3rd byte `01`, and there is **1** byte after it before the last byte.
- Type `10` has `02`, and **2** bytes follow.
- Type `20` has `03`, and **3** bytes follow.

So the 3rd byte says how many data bytes come next. The receiver uses it to know where the message ends.

## 4. The data bytes

**Type `01`: a counter.** `AB`, `AC` in recording 1, `6C`, `6D` in recording 2. It goes up by 1 every round
(the jump between recordings is just time passing; it's one byte, so after `FF` it wraps to `00`). This is a
**sequence number**, which lets the receiver notice a lost message.

**Type `10`: temperature.** Read the 2 bytes as one 16-bit number, high byte first:

| Bytes | Decimal | ÷ 10 |
|---|---|---|
| `00 D9` | 217 | 21.7 °C |
| `00 DA` | 218 | 21.8 °C |
| `00 DB` | 219 | 21.9 °C |
| `00 DC` | 220 | 22.0 °C |

It drifts slowly, by 0.1 °C per round: room temperature in units of 0.1 °C. (It runs 21.5 → 22.2 °C, then
jumps back, so it's a simulated sensor.)

**Type `20`: a colour (Red, Green, Blue).** Each byte is the brightness of one colour, 00 = off, FF = full:

| Bytes | Colour |
|---|---|
| `FF 00 00` | red |
| `FF 80 00` | orange |
| `00 FF 00` | green |
| `00 80 FF` | sky blue |
| `00 00 FF` | blue |
| `80 00 FF` | purple |

It cycles through these 6 steps, a rainbow. Every recording catches a few of them.

## 5. The last byte is a checksum (XOR of all bytes before it)

XOR compares bits: same bits give 0, different bits give 1. Take `A5 01 01 AB 0E`:

```
A5 = 1010 0101
01 = 0000 0001  → XOR so far: 1010 0100 (A4)
01 = 0000 0001  → XOR so far: 1010 0101 (A5)
AB = 1010 1011  → XOR so far: 0000 1110 (0E)   ← matches the last byte
```

The receiver does the same calculation. If its result differs from the last byte, a bit got corrupted on the
wire and it throws the message away.

## 6. What device is this?

A small **sensor/status node**: every few milliseconds it sends "I'm alive, message number N", "the temperature
is 21.8 °C", and "my LED colour is green". Think of a smart thermostat or an RGB room-sensor talking to a main
controller.

## The full protocol

```
A5 | type | length | data (length bytes) | checksum
```

| Type | Name | Data |
|---|---|---|
| `01` | Heartbeat | sequence counter, 1 byte |
| `10` | Temperature | 16-bit, high byte first, 0.1 °C |
| `20` | LED colour | R, G, B, 1 byte each |
