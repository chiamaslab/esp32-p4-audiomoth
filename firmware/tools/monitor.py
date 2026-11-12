"""Listen for the first minute of boot and capture diagnostics, without asserting reset."""
import argparse
import re
import time
import serial

if __name__ == "__main__":
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("port")
    args.add_argument("--until-ip", action="store_true", help="stop after a nonzero IPv4 and a sample-rate report")
    options = args.parse_args()
    with serial.Serial(port=None, baudrate=115200, timeout=0.25) as port:
        port.port = options.port
        port.dtr = port.rts = False
        port.open()
        until = time.monotonic() + 65
        while time.monotonic() < until:
            data = port.readline()
            if data:
                line = data.decode("utf-8", errors="replace").rstrip()
                print(line, flush=True)
                match = re.search(r"\bip=([\d.]+) input=(\d+) Hz", line)
                if options.until_ip and match and match[1] != "0.0.0.0" and int(match[2]):
                    break
