# ESP32-P4 PoE + AudioMoth implementation packet

Prepared 2026-11-12. Includes the custom firmware, original flash images, configuration, and verification tools from a working prototype.

The practical contribution is a tested ultrasonic PoE microphone node for a separate BirdNET-Go server. Earlier AudioMoth/P4 and ESP32 network-microphone work is acknowledged in [credits and comparisons](CREDITS.md), along with the current provenance and licensing limits.

Clone or download this repository and follow the setup below from `firmware/`. The [complete implementation packet](downloads/ESP32_P4_AudioMoth_Packet.zip) and [standalone Markdown guide](downloads/ESP32_P4_AudioMoth_Setup.md) are available in `downloads/` for forwarding to another implementer.

## What this builds

The AudioMoth USB Microphone captures mono 16-bit audio at 384,000 samples/s. The Waveshare ESP32-P4-NANO hosts the microphone over USB and serves audio over wired Ethernet. Its Kit B PoE module supplies power through the Ethernet cable. BirdNET-Go runs on a separate Linux computer or container and performs bat classification.

| Component | Role |
| --- | --- |
| AudioMoth USB Microphone | Ultrasonic audio capture at 384 kHz |
| ESP32-P4-NANO Kit B with its PoE module | USB host, audio buffering, rate conversion, HTTP streaming |
| PoE switch or compatible injector | Node power and Ethernet connectivity |
| BirdNET-Go on a Linux host | Bat model, analysis, detection database, clips and spectrograms |
| Setup computer | Flash firmware and run verification tools |

The node offers these endpoints on TCP port 80.

| Endpoint | Output |
| --- | --- |
| `/native.wav` | Continuous 384 kHz mono signed 16-bit little-endian PCM in a WAV stream |
| `/bats.wav` | Continuous 256 kHz mono signed 16-bit PCM, resampled for the tested BirdNET-Go pipeline |
| `/status` | JSON capture diagnostics |
| `/` | The same diagnostics as `/status` |

The 384-to-256 kHz converter uses a 48-tap FIR anti-alias filter with a nominal 113 kHz cutoff. The bat endpoint has a 128 kHz Nyquist limit. Use the native endpoint when retaining the original sampled bandwidth matters. Each stream runs at normal recording speed. Synthetic tests verify that 40 and 81 kHz tones retain their frequencies.

Native audio carries 768,000 bytes/s, about 6.144 Mbit/s. Both endpoints together carry about 10.24 Mbit/s before protocol overhead.

## Hardware and versions

The tested node was a **Waveshare ESP32-P4-NANO Kit B**, with P4 silicon **revision v3.2**, **16 MB flash**, and **32 MiB PSRAM**. The firmware uses ESP-IDF **v5.5.4** at **400 MHz**. The supplied bootloader and application headers permit chip revisions **v3.1 through v3.99**. Check the actual chip before flashing a board from a different batch.

| Connection | Where it goes |
| --- | --- |
| AudioMoth USB cable | P4 board's USB-A OTG host port |
| Ethernet cable from PoE equipment | P4 board's RJ45 socket, with the Kit B PoE module fitted |
| Computer USB data cable during setup | P4 board's separate USB-C programming/debug port |

The firmware configures the NANO's IP101 Ethernet PHY with MDC GPIO31, MDIO GPIO52, external RMII clock GPIO50, and PHY reset GPIO51. Other RMII signals use the P4 SDK defaults. It uses the P4 high-speed USB host controller wired to USB-A. These board-specific choices are in `firmware/main/main.c` and `usb_mic.c`.

The DHCP hostname is `audiomoth-p4`. Discover the assigned address through the router's DHCP leases or the serial log. A DHCP reservation keeps BirdNET-Go's source URL stable. The firmware has no mDNS implementation, so use the numeric DHCP address unless your router provides name resolution.

Manufacturer references are [Waveshare's board documentation](https://docs.waveshare.com/ESP32-P4-NANO) and [Kit B product information](https://www.waveshare.com/esp32-p4-nano.htm).

## 1. Configure the AudioMoth

1. Connect the microphone directly to the setup computer. Put its switch in **USB/OFF**, open the **AudioMoth USB Microphone App**, and set **384 kHz**. Record the selected gain for your installation. The original gain setting was not retained, so adjust it using a recording and check for clipping.
2. Put the switch in **DEFAULT**, then connect it to the P4 USB-A port. The tested microphone exposed its configuration interfaces in USB/OFF and its audio interface in DEFAULT.

The official app distinguishes DEFAULT, which applies sample rate and gain, from CUSTOM, which also applies configured filters and advanced settings. Start with DEFAULT to reproduce this setup. The microphone runs from USB power. A standard AudioMoth recorder needs the manufacturer's USB microphone firmware to expose a USB audio interface. [Official USB Microphone firmware and app](https://www.openacousticdevices.info/usb-microphone)

## 2. Flash the supplied images

These commands assume macOS or Linux with Bash, Python 3, and `shasum`. Replace every uppercase placeholder with your own value. Run commands in `firmware/` unless another directory is specified.

```sh
cd firmware
python3 -m venv .venv
. .venv/bin/activate
python -m pip install 'esptool==5.6.0' pyserial
python -m serial.tools.list_ports -v
```

Identify the P4 programming port by comparing the port list with the P4 USB-C cable disconnected and connected. On macOS it usually resembles `/dev/cu.usbmodem...`. On Linux it may resemble `/dev/ttyACM0` or `/dev/ttyUSB0`. The microphone's configuration port is a separate device.

```sh
PORT='/dev/cu.usbmodemXXXX'
esptool --chip esp32p4 --port "$PORT" flash-id
```

Confirm revision 3.1–3.99 and 16 MB flash. Leave Ethernet connected and the microphone in DEFAULT, then run the supplied flasher.

```sh
bash tools/flash.sh "$PORT"
```

The script checks the image hashes and chip, saves a 16 MB backup of that board's existing flash in `bench/`, flashes at 115200 baud, and runs the serial/network verification. Keep the resulting backup locally. Generated files in `bench/` belong to your installation.

If download mode does not engage automatically, hold BOOT, tap RESET, release BOOT, and retry. Close serial monitors before flashing.

The supplied image offsets are fixed for this build.

| Flash address | File |
| --- | --- |
| `0x2000` | `images/bootloader.bin` |
| `0x8000` | `images/partition-table.bin` |
| `0x10000` | `images/bat_streamer.bin` |

The application image is 496,080 bytes. Its SHA-256 is `f20a28b9c656a279295e12888d06394c2ee09465c6aa265decdc32303266db06`. `images/SHA256SUMS` covers all three images. `esptool --chip esp32p4 image-info images/bat_streamer.bin` displays its SDK and revision limits without connecting hardware.

## 3. Verify streaming and PoE

Set the node URL using the address reported by your own board.

```sh
BOARD_URL='http://BOARD_IP'
curl --fail --silent --show-error "$BOARD_URL/status" | python3 -m json.tool
python3 tools/check_stream.py "$BOARD_URL" --seconds 60 --clip bench/native-test.wav
```

Create `bench/` first with `mkdir -p bench` if you skipped the supplied flasher. The check reads both streams concurrently and saves the first two seconds of native audio as a finite WAV file.

| Check | Expected result |
| --- | --- |
| `input_hz` / `bat_hz` | `384000` / `256000` |
| Native samples and bytes in 60 s | 23,040,000 samples, 46,080,000 bytes |
| Bat samples and bytes in 60 s | 15,360,000 samples, 30,720,000 bytes |
| Measured capture rate | Within 1% of 384,000 samples/s |
| `samples_total` | Increasing |
| `packet_age_ms` | Fresh, ordinarily a few milliseconds while capturing |
| `usb_errors` / `reader_overruns` | Ideally zero, and no increase during the check |
| `boot_id` / `epoch` | Stable throughout the check |

The test also verifies WAV formats, timing, and that the received sample ranges fit the producer's timeline. A successful run prints a JSON report and exits with code 0.

Disconnect the P4 USB-C programming cable and restart the node using PoE alone. Rediscover its address if necessary and repeat the same 60-second test. A changed boot ID across that deliberate restart is expected. Verify that BirdNET-Go reconnects afterward.

FFprobe can inspect the finite native recording.

```sh
ffprobe -v error -show_entries stream=codec_name,sample_rate,channels,bits_per_sample \
  -of json bench/native-test.wav
```

Expect `pcm_s16le`, 384000 Hz, one channel, and 16 bits. A known ultrasonic acoustic source and spectrogram can test the physical microphone path. The supplied synthetic tests exercise digital conversion only.

## 4. Connect BirdNET-Go

Use a Linux BirdNET-Go installation with the ONNX Bat Classifier, regional BattyBirdNET models, per-stream model assignment, and high-rate network audio support. The recorded working build label is **20261014**. An exact container digest is unavailable in this packet. Record the version and image digest you deploy because model galleries and settings can change.

If you need a fresh server, start with the upstream [BirdNET-Go repository](https://github.com/tphakala/birdnet-go) and [installation guide](https://github.com/tphakala/birdnet-go/wiki/installation). Use persistent configuration and data volumes and confirm the container can reach the board's HTTP address. The packet's firmware images are the custom node implementation.

In the tested UI, complete these settings.

1. In **Settings → Analysis → Models**, install the BattyBirdNET model covering your location. **USA West** is appropriate when deploying in that region. Confirm that the Bat Classifier is enabled globally after installation.
2. In **Settings → Audio → Streams**, add an HTTP source such as **AudioMoth Bats**, with URL `http://BOARD_IP/bats.wav`. Run **Test Stream** and confirm **256 kHz**. Assign **Bat Classifier (ONNX)** to this source, deselect the bird model for this source, and save both the stream and the settings page.

In **System → AI Models**, confirm that the Bat Classifier lists this audio source and its analysed-segment counter increases during the configured active schedule. The prototype used civil dusk to dawn, a 0.5 confidence threshold, and repeat confirmation requiring two matching analysis windows. Verify the equivalent bat-specific settings in your installed version. Set the server's own location and time zone so the schedule follows its deployment site. At midday, a dusk-to-dawn classifier can correctly show a paused state.

Recheck the source assignment after removing or reinstalling a model. Model installation, global enablement, and assignment to the audio source are separate settings in the tested release.

### Helper for the global enablement issue

The tested release could install the model files while its global enabled list still contained only the bird model. The included helper adds `bat` while preserving other enabled models, then runs the 60-second stream check.

```sh
BIRDNET_URL='http://BIRDNET_HOST:8080'
python3 tools/enable_bats.py --birdnet "$BIRDNET_URL" --node "$BOARD_URL"
```

This helper requires an already-installed bat model and a reachable API session. It keeps the CSRF cookie and token together, uses the settings ETag when supplied, and retries a concurrent-update conflict. Its only configuration write is the enabled-model list. It has no login or API-key implementation. Use the authenticated web interface if your server requires login, or adapt the client to the server's documented authentication method.

## Status and troubleshooting

Open `http://BOARD_IP/status` in a browser for a snapshot. Refresh it to compare counters. The serial log also prints the board address, capture rate, errors, and overruns every five seconds.

| Symptom | What to check |
| --- | --- |
| `/status` responds, `input_hz` is 0, or audio gives HTTP 503 | AudioMoth switch in DEFAULT, USB data cable, USB-A host connection, microphone firmware |
| Capture reports 48 kHz or another unexpected rate | Reconfigure the microphone to 384 kHz in its app, reconnect, and repeat the stream test |
| DHCP address is absent | PoE power, Ethernet link, router lease list, and serial log |
| Capture stops after USB-C removal | Kit B PoE module, PoE port/injector, and successful cold boot on PoE alone |
| USB errors or epochs increase unexpectedly | Power/cable continuity, microphone disconnects, serial diagnostics |
| Reader overruns increase | A receiver fell behind the roughly one-second ring buffer. Check network and receiving-host load |
| HTTP 503 says all slots are busy | Close unused stream readers. The server has four concurrent client slots, including status requests |
| Model installed but no segments analysed | Global model enablement, source assignment, detected 256 kHz format, active schedule |
| CSRF 403 from the helper | Server version and authentication behavior. Enable through the logged-in UI if needed |
| “Last heard” shows a species but detection list is empty | Repeat-confirmation or other filters can reject a candidate. Review the saved detections and clips separately |

The node's HTTP endpoints provide ambient audio to reachable clients and have no authentication or TLS. Restrict access to trusted LAN clients. An outdoor installation needs an enclosure and microphone protection appropriate to weather exposure while preserving the acoustic opening.

## Rebuild from source

The project includes only its own application sources and scripts. ESP-IDF dependencies are installed from Espressif. `sdkconfig` retains the tested full configuration and `sdkconfig.defaults` contains the important target settings.

Install the OS prerequisites listed in Espressif's [v5.5.4 macOS/Linux toolchain guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32p4/get-started/linux-macos-setup.html), then run from `firmware/` in a fresh shell.

```sh
bash tools/setup-idf.sh
. "$HOME/esp/esp-idf-v5.5.4/export.sh"
idf.py build
```

`setup-idf.sh` clones the v5.5.4 tag and installs the esp32p4 toolchain. `BAT_IDF_PATH` can select another SDK location. If set, source that installation's `export.sh`. The project already selects `esp32p4`.

If an agent sandbox blocks the SDK component manager's process enumeration, use `IDF_COMPONENT_MANAGER=0 idf.py build`. This project uses SDK components directly and has no registry component dependencies. That option allowed the packet's verification build to complete inside a restricted environment.

For your newly built images, use the SDK's generated flash configuration.

```sh
PORT='/dev/cu.usbmodemXXXX'
idf.py -p "$PORT" flash monitor
```

Exit the serial monitor with Ctrl+]. The `tools/flash.sh` wrapper always uses the supplied files in `images/`; `idf.py flash` uses the current build outputs. Preserve a backup of an existing device before overwriting its firmware.

## Software checks

The test suite requires Python 3, a C compiler available as `cc`, and FFmpeg available as `ffmpeg`. Run these commands from `firmware/`.

```sh
python3 tools/test_audio.py
python3 tools/test_birdnet.py
```

The audio suite compiles the actual firmware format code on the host. It checks USB descriptors, rate selection, WAV decoding, 40/81 kHz tone preservation, and suppression of a 155 kHz tone that would alias into the 256 kHz output. The API test exercises CSRF cookies, settings preconditions, preservation of existing model IDs, and idempotent enablement against a local fixture.

## Recorded validation and limits

On 2026-11-12, this packet's source completed a fresh ESP-IDF v5.5.4 build and all four existing host tests passed. The supplied flash images match their original SHA-256 values, and the application firmware source files match the working prototype byte for byte. The original flash images are included so the quick-install path uses the previously tested build.

The original hardware verification on 2026-11-07 received 60.001 seconds of native audio and 60.005 seconds of bat audio concurrently. It measured **384,028.7 input samples/s**, with **zero USB errors and zero reader overruns**. PoE-only cold boot and streaming were verified. BirdNET-Go's bat analysis counter advanced with a reported 0% processing error rate.

Those results validate that prototype's capture and processing path. Species accuracy requires reviewing actual recordings. Isolated “Last heard” candidates can be filtered before a clip is saved. Reviewable wild-bat identification and long-term outdoor reliability remain validation tasks for each installation. This packet does not establish a successful field identification.

The original binaries and firmware sources are preserved. The shared `enable_bats.py` requires explicit server and node URLs, and the shared flasher checks the image's minimum chip revision. Private addresses, device identifiers, original audio recordings, flash backups, and deployment logs are excluded. All packet files are listed in `SHA256SUMS` for integrity checking.

ESP-IDF and component license notices accompany the firmware under `licenses/esp-idf/`. The SDK source is available at [Espressif's v5.5.4 tag](https://github.com/espressif/esp-idf/tree/v5.5.4). BirdNET-Go and AudioMoth firmware remain separately installed upstream dependencies.

## Prompt for an implementation agent

```text
Implement this ESP32-P4-NANO Kit B PoE + AudioMoth setup using the attached
packet. Read this guide and the included firmware before changing anything.

Identify the owner's board revision, flash size, microphone mode/rate, serial
port, network address, BirdNET-Go version, regional model, and active schedule.
Use the supplied images on compatible hardware or build with ESP-IDF v5.5.4.
Keep native capture at 384 kHz mono 16-bit and supply the 256 kHz bat endpoint.

Run the included software tests, flash the intended board, and prove both
streams using check_stream.py. Repeat after a PoE-only cold boot. Configure
BirdNET-Go's installed model, global enablement, stream URL, detected format,
and per-source Bat Classifier assignment. Confirm its analysis counter advances
during the intended schedule and verify reconnection after the node restarts.

Use this owner's addresses and credentials. Preserve existing unrelated
BirdNET-Go settings. Keep recordings, flash backups, credentials, network
details, and device identifiers local. Report actual test results and any
remaining hardware, recording, or field-validation gaps. Give the owner
about two physical setup steps at a time when hands-on help is needed.
```
