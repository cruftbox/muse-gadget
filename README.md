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

- Board 1 completes Muse pairing, Wi-Fi setup, voice capture, text replies, and
  Wit.ai speech playback.
- The owner's Muse SDK and Wit.ai tokens are provisioned at runtime. Neither is
  embedded in the firmware.
- Wit.ai setup is performed from a phone on the same Wi-Fi network. USB token
  provisioning is no longer required.
- A verified 16 MB factory backup exists outside Git. Board 2 remains untouched.
- The Waveshare target builds with ESP-IDF 6.0.1.

The verified backup is intentionally excluded from Git because it may contain
device-specific data.

Detailed implementation and verification history is in
[`docs/progress.md`](docs/progress.md). The board pin map and hardware notes are
in [`docs/board-notes.md`](docs/board-notes.md).

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

The `lcd154` profile speaks completed text replies through Wit.ai. Each owner
stores their own Wit.ai client token after completing Muse and Wi-Fi setup:

1. Open the Wit.ai app settings on a phone and copy its client access token.
2. On the gadget, open **Settings > Sound > Wit.ai token** and tap
   **Configure**.
3. Open the local URL shown by the gadget from a phone on the same Wi-Fi
   network.
4. Paste the token and tap **Save and test**.

The gadget validates the token with Wit.ai before storing it. The temporary
local setup page closes after success, cancellation, leaving the Sound page, or
five minutes. The stored token is never displayed. It is removed by **Remove**
on the Sound page and by the gadget's setup reset. No computer, firmware rebuild,
or USB data connection is required. Implementation and security details are in
[`docs/wit-token-provisioning.md`](docs/wit-token-provisioning.md).

Wit.ai currently accepts up to 280 bytes of reply text per request. Longer
replies are spoken only through that boundary but remain fully visible on
screen.

The board implementation is
`firmware/esp32/components/muse/boards/board_waveshare_s3_lcd154.c`. It uses
the upstream Muse board interface and Espressif component drivers. The
Waveshare factory BSP is a pin-map and bus-configuration reference only.

## License

Project-authored code and documentation outside `firmware/` are licensed under
the MIT License. The `firmware/` subtree is licensed under Apache-2.0 as stated
in [`firmware/LICENSE`](firmware/LICENSE). Third-party assets retain their
original licenses.
