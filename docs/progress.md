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
- Separated owner-provided SDK and Wit tokens from shared recovery clears.
  Automatic pairing/setup recovery now preserves them; the explicit Muse menu
  transfer reset erases both with NVS readback verification.
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
audio initialization, connection to the Muse VM, and OTA image validation.
End-to-end testing then verified a real push-to-talk request, a visible Muse
text reply, streamed 24 kHz mono MP3 playback through the onboard speaker, and
normal turn completion.

The runtime Wit.ai token setup was also verified after separating its temporary
local HTTP server from HTTPS validation. The setup server now stops before the
gadget opens the Wit.ai connection, and the validated token is committed to NVS
on the existing internal-stack keeper task. Board 1 accepted the token, retained
it, and produced a spoken reply after the final flash.

A replacement battery was installed in Board 1 on November 2, 2026. The gadget
keeps running with USB disconnected, which the original battery did not do. It
reports 83 percent, about 4.05 V, on battery and 95 percent, about 4.16 V, on
USB. The USB reading stayed at 95 percent across 7 samples over about 2.5
minutes. The USB figure probably reflects charger voltage rather than state of
charge, so the unplugged reading is the better estimate. Charging and battery
runtime have not been verified. This build does not report the charge-status
pin over serial; it appears only on the screen.

## Remaining work

- Perform a clean handoff test after resetting the gadget, using a different
  owner's Muse account, Muse SDK token, Wi-Fi, and Wit.ai token.
- Verify charging and battery runtime with the replacement battery.

## Deferred web-flasher concept

A browser flasher is not planned for this project. Its implementation and
maintenance cost are disproportionate to this one-device handoff. The existing
source build and USB serial token procedure remain the supported installation
path.

If the project is expanded later, most of the browser interface already exists
under `firmware/web-flasher`. A narrow implementation would:

- add `muse-waveshare-s3-lcd154` to the existing board list;
- add a root GitHub Actions workflow that builds only the `lcd154` target,
  generates its ESP Web Tools manifest, and deploys the static files;
- host the page over public HTTPS, or serve it from localhost for private use;
- flash through Web Serial, then send the owner's Muse SDK token directly from
  the browser to the gadget over USB serial; and
- verify first installation, wrong-board rejection, SDK-token transfer, and an
  update that preserves Wi-Fi, pairing, and stored tokens.

The page would not need a backend or token database. A public deployment would
make the compiled firmware downloadable, while Muse SDK tokens would remain in
the user's browser and USB connection.
