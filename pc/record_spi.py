"""
record_spi.py - ask the NUCLEO-U083RC SPI recorder for one recording and save it as a .vcd file.

Usage (Windows PowerShell):
    python record_spi.py COM5 D:\\REA\\capture.vcd

Needs pyserial:  python -m pip install pyserial
"""

import sys

import serial


def record(port, out_path, timeout_s=60):
    with serial.Serial(port, 115200, timeout=timeout_s) as link:
        link.reset_input_buffer()
        link.write(b"c")

        vcd_lines = []
        inside = False
        while True:
            raw = link.readline()
            if not raw:
                raise TimeoutError("No answer from the board. Check the COM port and that the firmware is running.")
            line = raw.decode("ascii", errors="replace").rstrip("\r\n")
            if line.startswith("-----BEGIN VCD-----"):
                inside = True
            elif line.startswith("-----END VCD-----"):
                break
            elif inside:
                vcd_lines.append(line)
            elif line:
                print(line)

    with open(out_path, "w", newline="\n") as f:
        f.write("\n".join(vcd_lines) + "\n")
    print(f"Saved {len(vcd_lines)} lines to {out_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python record_spi.py <COM port> <output .vcd file>")
        sys.exit(1)
    record(sys.argv[1], sys.argv[2])
