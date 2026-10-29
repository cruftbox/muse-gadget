# Muse Gadget for Waveshare ESP32-S3-Touch-LCD-1.54

This repository tracks the port of Meta's Muse Gadget SDK to the Waveshare
ESP32-S3-Touch-LCD-1.54.

## Goal

Build a handheld Muse voice companion that supports:

- Push-to-talk voice input
- On-screen Muse status and responses
- Spoken replies through the onboard speaker
- Touch and physical-button controls
- Consumer-style first-run setup
- Factory reset and transfer to another Muse account

The intended recipient must be able to complete setup without source-code
changes, ESP-IDF, command-line tools, or firmware compilation.

## Hardware

- ESP32-S3R8, dual-core 240 MHz
- 8 MB PSRAM
- 16 MB flash
- 240 x 240 ST7789 SPI LCD
- CST816 capacitive touch controller
- ES7210 dual-microphone ADC
- ES8311 audio codec and NS4150B amplifier
- QMI8658 six-axis IMU
- microSD slot and 3.7 V battery interface

## Current status

- Board 1 identified over native USB Serial/JTAG as an ESP32-S3 on COM3.
- Complete 16 MB factory flash backed up and verified with two independent reads.
- The backup is `backups/factory-2026-10-29-80456b35af58.bin`, excluded from
  Git, with SHA-256
  `01146BC3F053CB9F2E498E62EE613AA7429073BAB2F5272D6F6949F0F1A4522A`.
- Secure Boot and Flash Encryption are disabled.
- Hardware documentation and current Muse SDK support have been reviewed.
- Muse SDK revision `1db6a26f4ca56939c4465d8a12e241c1b1b5c8dc` is imported under
  `firmware/` as a tracked Git subtree.
- The board profile, pin map, build overlay, and Muse build-tool integration
  are implemented on branch `port/waveshare-s3-touch-lcd-154`.
- The Waveshare target compiles successfully with ESP-IDF 6.0.1. The signed
  application image is 2,166,784 bytes and leaves 48% of each 4 MiB OTA
  partition free.
- Board 1 is running that build. The boot log reaches the setup screen without
  a panic or reboot loop. Display, touch registration, battery reporting,
  Bluetooth advertising, both microphones, the speaker codec, Wi-Fi scanning,
  and the 8 MB PSRAM initialize successfully.
- The screen is correctly oriented and shows `SET UP WI-FI` with Bluetooth
  provisioning name `MuseGadget-35AF58`. USB status reports the same board,
  95% battery, no saved Wi-Fi network, and `Ready to pair`.
- The build contains no embedded Muse SDK token. The owner's SDK token is
  provisioned at runtime over USB, retained in NVS across firmware flashes, and
  reported by the boot log without exposing the complete token.
- A complete Muse app setup trace showed pairing confirmation and Wi-Fi scanning
  succeeding, followed by `provision_v2` failing before Wi-Fi connection. The
  cause was the transient provisioning task requesting an 8 KiB internal-RAM
  stack when the largest available block was 5 KiB.
- Muse builds now use a 4 KiB provisioning stack because they save credentials
  and restart before opening the TLS session that requires the upstream 8 KiB
  stack. Non-Muse builds retain the 8 KiB allocation. The revised firmware is
  built and flashed. Boot verification confirms the SDK token, display, touch,
  speaker, both microphones, Wi-Fi scan, Bluetooth, and PSRAM initialize.
- The subsequent Muse app setup stores pairing credentials and joins Wi-Fi.
  Registration originally stalled because the normal 16 KiB DMA transmission
  reserve exceeded the 12 KiB available after audio and TLS initialization.
  Initial `link.register` transmission now uses an 8 KiB startup reserve while
  retaining the 2 KiB contiguous AES allocation floor; normal traffic keeps the
  16 KiB reserve. On-device verification shows `link.register` sent and
  acknowledged, its tunnel opened, and the Link state changed to `Online`.
- The on-device two-step reset was tested previously with a synthetic token. It
  restarted the board with no SDK token, Wi-Fi credentials, or pairing state and
  returned to `Ready to pair`, also verifying touch navigation.
- Board 2 remains untouched.

The verified backup is intentionally excluded from Git because it may contain
device-specific data.

The host-side config-store test is present, but it was skipped on this Windows
host because no host C compiler is installed. The ESP-IDF target build passed.

## Important requirements

- Use ESP-IDF 6.0.1, as required by the upstream SDK.
- Never commit Muse SDK tokens, Wi-Fi credentials, or pairing data.
- Runtime setup must accept the recipient's own Muse SDK token.
- Factory reset must remove all account-bound state without requiring a reflash.
- Do not erase or flash the device unless the factory backup is confirmed.
- Keep board 2 untouched until board 1 works end to end.

## Upstream and hardware references

- [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk)
- [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.54)
- [Waveshare examples](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.54)
- [Muse Gadget SDK token terms](https://gadgets.muse.ai/sdk-terms)

## Project notes

The detailed working requirements and phased plan are in
[`muse-gadget-port-prompt.md`](muse-gadget-port-prompt.md).

## Firmware build

The board alias is `lcd154`. From `firmware/esp32`, with ESP-IDF 6.0.1
activated:

```sh
tools/muse/board.sh build lcd154
```

The `lcd154` profile speaks completed text replies through Wit.ai. Put the Wit
client token in the ignored repository-root `.wit-token` file as either the raw
token or `wit_client_token="..."`, then build normally. The build embeds the
token in the device firmware. The token and the local Wit test MP3 are excluded
from Git. Wit.ai currently accepts up to 280 bytes of reply text per request;
longer replies are spoken only through that boundary but remain fully visible
on screen.

The board implementation is
`firmware/esp32/components/muse/boards/board_waveshare_s3_lcd154.c`. It uses
the upstream Muse board interface and Espressif component drivers. The
Waveshare factory BSP is a pin-map and bus-configuration reference only.

## License

Project-authored code and documentation are licensed under the MIT License.
Upstream Muse SDK code and third-party assets retain their original licenses.
