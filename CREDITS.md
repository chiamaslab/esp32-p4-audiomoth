# Credits, provenance, and related projects

Review date 2026-11-12.

## Contribution

This project packages a tested path from an AudioMoth USB Microphone through a Waveshare ESP32-P4-NANO PoE node to a separate BirdNET-Go server. Its application code supplies descriptor-driven UAC1 capture, concurrent native 384 kHz and filtered 256 kHz HTTP WAV streams, a PSRAM ring buffer, capture epochs and sample counters, microphone recovery, and reproducible checks.

USB audio, network microphone streaming, FIR resampling, and bat classification all have substantial prior work. The contribution here is the implementation and validation of this particular ultrasonic PoE integration. A world-first claim is unsupported by this review. A new classifier or independently validated species-identification accuracy is also outside the demonstrated result.

## Credits

- **HudsonReynolds2, esp32p4-usb-audiomoth.** The earlier project demonstrated AudioMoth USB capture on an ESP32-P4 and was linked in the initial project research. It deserves acknowledgement as a prior implementation and research reference. The reviewed snapshot is commit `4316bd8ed42c46a3f5442a899a8d8830f7061b3c`. Its probe uses a fixed 96-byte isochronous packet size and prints capture diagnostics. [Earlier project](https://github.com/HudsonReynolds2/esp32p4-usb-audiomoth/tree/4316bd8ed42c46a3f5442a899a8d8830f7061b3c)
- **Espressif Systems and ESP-IDF contributors.** ESP-IDF supplies the USB host, Ethernet, FreeRTOS integration, networking, memory services, and toolchain interfaces. The SDK version used here is v5.5.4. The USB host examples and documentation are relevant implementation references. [ESP-IDF](https://github.com/espressif/esp-idf/tree/v5.5.4) · [USB host library example](https://github.com/espressif/esp-idf/tree/v5.5.4/examples/peripherals/usb/host/usb_host_lib)
- **Waveshare.** The ESP32-P4-NANO hardware, PoE module, board documentation, and pin assignments make the physical node possible. [Board documentation](https://docs.waveshare.com/ESP32-P4-NANO)
- **Open Acoustic Devices.** AudioMoth hardware, USB microphone firmware, and its configuration app supply the ultrasonic capture device. [AudioMoth USB Microphone](https://www.openacousticdevices.info/usb-microphone)
- **tphakala and BirdNET-Go contributors.** BirdNET-Go provides receiving-host audio ingestion, model management, analysis, detection storage, and the user interface. [BirdNET-Go](https://github.com/tphakala/birdnet-go)
- **R. D. Zinck and BattyBirdNET contributors.** BattyBirdNET supplies the regional bat-classification work used through BirdNET-Go. Classifier models and their licenses are maintained by their upstream providers. [BattyBirdNET-Analyzer](https://github.com/rdz-oss/BattyBirdNET-Analyzer)
- **FreeRTOS, lwIP, Mbed TLS, Newlib/Picolibc, TLSF, GCC, and other SDK contributors.** The compiled firmware includes SDK/runtime dependencies. Retain the accompanying third-party notices when distributing firmware builds.

## Source comparison and its limits

The five application C files were compared with both C files in the HudsonReynolds2 snapshot. No substantial identical implementation blocks were found. The shared fragments were ordinary C declarations and required USB-host API patterns. The descriptor parser, Ethernet server, ring buffer, and resampling pipeline are separate implementations in this repository.

This comparison supports describing the current code as a custom integration. It cannot reconstruct every earlier development step or establish originality against every existing codebase. The retained project history does not provide a complete file-by-file provenance record. The prior research link establishes influence even where the implementations differ.

The HudsonReynolds2 snapshot has no root license file. Its inherited `class_driver.c` carries an Espressif `Unlicense OR CC0-1.0` notice, while `usb_host_lib_main.c` has no explicit license header. This repository includes neither of those source files. Their acknowledgement here is a reference to prior work.

## Comparable projects

| Project | Existing capability | Relationship to this implementation |
| --- | --- | --- |
| [HudsonReynolds2/esp32p4-usb-audiomoth](https://github.com/HudsonReynolds2/esp32p4-usb-audiomoth) | AudioMoth-on-P4 USB capture probe, fixed 96-byte packets, serial diagnostics | Closest hardware precedent. This implementation adds descriptor-based rate selection, tested 384 kHz capture, Ethernet HTTP streams, rate conversion, recovery, and receiver verification. |
| [Sukecz/esp32-birdnet-mic](https://github.com/Sukecz/esp32-birdnet-mic) | ESP32 I2S/PDM microphone streaming to BirdNET-Go/Pi, browser configuration, OTA, and microSD recording | A more developed bird-audio appliance with different capture hardware. Its documented boards and microphone path differ from the AudioMoth USB/P4/PoE path. |
| [jpmurray/esp32-audio-streamer](https://github.com/jpmurray/esp32-audio-streamer) | I2S microphone, Wi-Fi, HTTP WAV and RTSP, 24/48 kHz profiles, web configuration | Demonstrates that HTTP WAV and BirdNET-compatible network microphones are established approaches. This implementation addresses a much higher capture rate and wired PoE. |
| [rdz-oss/BattyBirdNET-Pi](https://github.com/rdz-oss/BattyBirdNET-Pi) | Continuous ultrasonic bat/bird analysis using a Linux system such as a Pi 4/5 and a USB microphone | Already solves bat monitoring. This implementation allows the USB microphone to sit at a small PoE node while a separate server runs the analysis. |

The reviewed projects establish substantial prior art. Their documented configurations did not provide this same complete AudioMoth/P4/PoE-to-BirdNET-Go implementation. That finding is limited to the projects reviewed on the date above.

## Publication and licensing

The packet has been checked for deployment addresses, owner-specific identifiers, personal filesystem paths, credentials, recordings, and flash backups. Those installation details are excluded. Manufacturer names, upstream authors, public project URLs, and license notices are intentional public attribution.

File checks do not cover GitHub account or Git commit identity settings. Before changing repository visibility, also verify that author and committer metadata use the intended public identity and a GitHub noreply email where appropriate.

A project-level license for the custom application code has not yet been selected. The notices under `licenses/` describe third-party components and do not assign a license to every file in this repository. Select an appropriate project license before presenting this as generally reusable open-source software. [GitHub's licensing guidance](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository)

BirdNET-Go, AudioMoth firmware, and model weights are separate upstream downloads. Their own licenses and model-use conditions continue to apply. The node serves unauthenticated HTTP audio on the LAN, as documented in the setup guide. Its demonstrated status is a working prototype with software and streaming validation, with field identification and long-duration reliability requiring further evidence.
