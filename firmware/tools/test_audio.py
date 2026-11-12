"""Host regression tests for the exact USB descriptor, resampler and WAV code in firmware."""
import array
import ctypes as c
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BLOCK = 1536
class Format(c.Structure):
    _fields_ = [("rate", c.c_uint32), ("packet", c.c_uint16), ("interface", c.c_uint8),
                ("alt", c.c_uint8), ("ep", c.c_uint8), ("interval", c.c_uint8), ("set_rate", c.c_bool)]


class AudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        lib = Path(cls.temp.name) / "audio.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "main/format.c"), "-o", str(lib)], check=True)
        cls.audio = c.CDLL(str(lib))
        cls.audio.find_mic_format.argtypes = [c.c_char_p, c.c_size_t, c.c_bool, c.POINTER(Format)]
        cls.audio.find_mic_format.restype = c.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_descriptor(self):
        interface = bytes([9,4,1,1,1,1,2,0,0, 7,0x24,1,1,0,1,0])
        def check(rates, mps, ctrl=False, high=False, interval=1):
            fmt = bytes([8+3*len(rates),0x24,2,1,1,2,16,len(rates)])
            ep = bytes([9,5,0x82,0x0d,mps & 255,mps >> 8,interval,0,0, 7,0x25,1,int(ctrl),0,0,0])
            data = interface + fmt + b"".join(x.to_bytes(3, "little") for x in rates) + ep
            parsed = Format()
            ok = self.audio.find_mic_format(data, len(data), high, c.byref(parsed))
            return parsed if ok else None
        native = check([384000], 768)
        self.assertEqual((native.rate, native.ep, native.packet, native.alt), (384000,0x82,768,1))
        self.assertFalse(native.set_rate)
        self.assertEqual(check([48000], 96).rate, 48000)
        self.assertEqual(check([48000,384000], 768, True).rate, 384000)
        self.assertTrue(check([48000,384000], 768, True).set_rate)
        self.assertIsNone(check([48000,384000], 768))
        self.assertIsNone(check([384000], 96))
        self.assertEqual(check([384000], 96, high=True).rate, 384000)
        self.assertIsNone(check([384000], 768, interval=2))
        self.assertFalse(self.audio.find_mic_format(b"\x00", 1, False, c.byref(Format())))
        self.assertFalse(self.audio.find_mic_format(b"\x09\x04", 2, False, c.byref(Format())))

    def test_bat_rate(self):
        for src, dst in ((48000,0),(96000,96000),(192000,192000),(256000,256000),(384000,256000)):
            self.assertEqual(self.audio.bat_rate(src), dst)

    def test_filter_and_decoder(self):
        count = 25 * BLOCK
        for hz in (40000, 81000, 155000):
            history = (c.c_int16 * 47)()
            raw = array.array("h", (round(10000 * math.sin(2*math.pi*hz*j/384000)) for j in range(count)))
            buf = (c.c_int16 * (BLOCK*2//3))()
            converted = array.array("h")
            for offset in range(0, count, BLOCK):
                part = (c.c_int16 * BLOCK).from_buffer(raw, offset*2)
                self.audio.convert_bat_block(history, part, buf)
                converted.extend(buf)
            self.assertEqual(len(converted), count*2//3)
            data = converted[1024:]
            power = sum(x*x for x in data)/len(data)
            if hz == 155000:
                self.assertLess(power, 50e6 * 10**(-55/10))  # Stopband alias at 101 kHz
                continue
            self.assertAlmostEqual(10*math.log10(power/50e6), 0, delta=0.1)
            crossings = sum(a <= 0 < b for a,b in zip(data,data[1:]))
            self.assertAlmostEqual(crossings * 256000/len(data), hz, delta=15)
            for rate, pcm in ((384000,raw),(256000,converted)):
                header = c.create_string_buffer(44)
                self.audio.make_wav_header(header, rate)
                self.assertEqual(struct.unpack_from("<I",header.raw,24)[0], rate)
                stream = header.raw + pcm.tobytes()
                decoded = subprocess.run(["ffmpeg","-hide_banner","-loglevel","error","-i","pipe:0",
                                          "-f","s16le","-acodec","pcm_s16le","pipe:1"],
                                         input=stream, capture_output=True, check=True)
                self.assertEqual(decoded.stdout, pcm.tobytes())


if __name__ == "__main__":
    unittest.main(verbosity=2)
