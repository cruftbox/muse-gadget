# Project progress

## Verified hardware and recovery state

Board 1 is a Waveshare ESP32-S3-Touch-LCD-1.54 exposed over native USB
Serial/JTAG. Secure Boot and Flash Encryption are disabled. Its complete 16 MB
factory flash was read twice and the copies matched. The backup is excluded
from Git because it can contain device-specific data. Board 2 remains
untouched.

The imported Muse Gadget SDK revision is
`1db6a26f4ca56939c4465d8a12e241c1b1b5c8dc` under `firmware/`.

## Completed port work

- Added the Waveshare board profile, pin map, SDK configuration overlay, and
  `lcd154` build alias.
- Verified display orientation, touch, battery reporting, Bluetooth
  advertising, Wi-Fi scanning, both microphones, speaker output, and 8 MB
  PSRAM initialization on Board 1.
- Reduced the Muse provisioning task stack to 4 KiB. This task saves
  credentials and restarts before the upstream TLS path that requires the 8
  KiB stack. Non-Muse builds retain the upstream allocation.
- Reduced the initial `link.register` transmission reserve to 8 KiB while
  retaining the 2 KiB contiguous AES allocation floor. Normal traffic retains
  the 16 KiB reserve. On-device logs verified registration acknowledgement,
  tunnel creation, and the `Online` Link state.
- Verified Muse app pairing, Wi-Fi setup, voice-note capture, text replies, and
  Wit.ai speech output.
- Verified the two-step setup reset with synthetic credentials. It removed the
  SDK token, Wi-Fi credentials, and pairing state and returned to pairing mode.
- Added phone-based Wit.ai client-token setup. The gadget validates the token
  before storing it, never displays the stored token, and removes it through
  the Sound settings or setup reset.
- Changed Wit.ai speech retrieval to a bounded PSRAM stream. A worker performs
  the HTTPS download while the Muse session task incrementally receives and
  decodes audio. Turn generation changes cancel obsolete downloads.

## Current build state

The Waveshare target uses ESP-IDF 6.0.1. The repository contains no embedded
Muse SDK token, Wit.ai token, Wi-Fi credential, or pairing state.

The asynchronous Wit changes pass 40 affected host tests with WinLibs GCC
16.2.0. The ESP-IDF target build also passes. The signed application is
`0x221000` bytes and leaves `0x1df000` bytes, 47 percent, free in the smallest
OTA application partition.

The asynchronous Wit build was flashed to Board 1 on October 29, 2026. Its boot
log confirmed retained Wi-Fi and pairing credentials, successful display and
audio initialization, connection to the Muse VM, and OTA image validation. The
new streaming path is boot-verified but still requires an end-to-end voice
request before it can be called functionally device-verified.
