"""Check both live WAV streams and the board counters from the same LAN."""
import argparse
import concurrent.futures
import json
from pathlib import Path
import struct
import time
import urllib.request
import wave


def open_direct(url):
    return urllib.request.build_opener(urllib.request.ProxyHandler({})).open(url, timeout=8)


def status(base):
    with open_direct(base + "/status") as response:
        return json.load(response)


def measure(base, name, rate, seconds, native_clip):
    with open_direct(f"{base}/{name}.wav") as response:
        header = response.read(44)
        if len(header) != 44 or header[:4] != b"RIFF" or header[8:16] != b"WAVEfmt ":
            raise ValueError(f"{name}: invalid WAV header")
        fmt = struct.unpack_from("<HHIIHH", header, 20)
        if fmt != (1, 1, rate, rate * 2, 2, 16) or header[36:40] != b"data":
            raise ValueError(f"{name}: unexpected audio format {fmt}")
        sample = bytearray()
        left = seconds * rate * 2
        start = time.monotonic()
        while left:
            data = response.read(min(32768, left))
            if not data:
                raise EOFError(f"{name}: disconnected with {left} bytes remaining")
            if native_clip and len(sample) < rate * 4:
                sample.extend(data[:rate * 4 - len(sample)])
            left -= len(data)
        elapsed = time.monotonic() - start
        if abs(elapsed - seconds) > max(0.6, seconds * 0.03):
            raise ValueError(f"{name}: {seconds}s of samples arrived in {elapsed:.2f}s")
        if native_clip:
            with wave.open(str(native_clip), "wb") as out:
                out.setparams((1, 2, rate, 0, "NONE", "not compressed"))
                out.writeframes(sample)
        return {"samples": rate * seconds, "bytes": rate * seconds * 2, "seconds": round(elapsed, 3),
                "boot": int(response.headers["X-Boot-ID"]), "epoch": int(response.headers["X-Capture-Epoch"]),
                "first_input_sample": int(response.headers["X-First-Input-Sample"])}


def run(base, seconds, clip):
    before = status(base)
    if (before["input_hz"], before["bat_hz"]) != (384000, 256000):
        raise ValueError(f"Expected 384000 Hz capture and 256000 Hz output. Board reports {before}")
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        native = pool.submit(measure, base, "native", 384000, seconds, clip)
        bats = pool.submit(measure, base, "bats", 256000, seconds, None)
        streams = {"native": native.result(), "bats": bats.result()}
    after = status(base)
    for key in ("boot_id", "epoch", "usb_errors", "reader_overruns"):
        if before[key] != after[key]:
            raise ValueError(f"{key} changed: {before[key]} -> {after[key]}")
    for name, stream in streams.items():
        if (stream["boot"], stream["epoch"]) != (after["boot_id"], after["epoch"]):
            raise ValueError(f"{name}: source changed during check")
        start = stream["first_input_sample"]
        if start < before["epoch_samples"] - before["samples_per_block"] or start + seconds * 384000 > after["epoch_samples"]:
            raise ValueError(f"{name}: receiver's sample range exceeds the producer's timeline")
    dsp_rate = (after["samples_total"] - before["samples_total"]) * 1e6 / (after["uptime_us"] - before["uptime_us"])
    if abs(dsp_rate / 384000 - 1) > .01:
        raise ValueError(f"Measured USB rate {dsp_rate:.1f} samples/s")
    return {"streams": streams, "measured_capture_hz": round(dsp_rate, 1), "status_after": after}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("board", help="IP or URL of the ESP32-P4")
    p.add_argument("--seconds", type=int, default=60)
    p.add_argument("--clip", type=Path, help="save the first two seconds of native audio as PCM WAV")
    args = p.parse_args()
    if args.seconds < 1:
        p.error("--seconds must be positive")
    base = args.board if "://" in args.board else "http://" + args.board
    print(json.dumps(run(base.rstrip("/"), args.seconds, args.clip), indent=2))
