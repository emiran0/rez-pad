"""Talk to the firmware debug console on the USB-UART bridge port.

Needs pyserial (PlatformIO's own Python has it):
  %USERPROFILE%\\.platformio\\penv\\Scripts\\python.exe tools/devconsole.py --port COM4 --reset
  ... tools/devconsole.py --port COM4 "bench" "bl 50"

--reset pulses EN via RTS and prints the boot log. Each command is sent as a line,
then output is read until the console goes quiet for --quiet seconds.
"""

import argparse
import sys
import time

import serial


def read_until_quiet(s: serial.Serial, quiet: float, limit: float) -> str:
    buf = b""
    last = time.time()
    end = time.time() + limit
    while time.time() < end:
        chunk = s.read(4096)
        if chunk:
            buf += chunk
            last = time.time()
        elif time.time() - last > quiet:
            break
    return buf.decode("utf-8", "replace")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM4")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--reset", action="store_true", help="reset the board and print the boot log")
    ap.add_argument("--quiet", type=float, default=1.5, help="seconds of silence that end a reply")
    ap.add_argument("--limit", type=float, default=60.0, help="max seconds to wait per step")
    ap.add_argument("commands", nargs="*")
    args = ap.parse_args()

    s = serial.Serial()
    s.port = args.port
    s.baudrate = args.baud
    s.timeout = 0.1
    s.dtr = False  # keep GPIO0 high so a reset boots the app, not the ROM loader
    s.rts = False
    s.open()
    try:
        if args.reset:
            s.rts = True
            time.sleep(0.1)
            s.rts = False
            sys.stdout.write(read_until_quiet(s, max(args.quiet, 2.0), args.limit))
        else:
            s.reset_input_buffer()
        for cmd in args.commands:
            s.write((cmd + "\n").encode())
            s.flush()
            sys.stdout.write(read_until_quiet(s, args.quiet, args.limit))
    finally:
        s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
