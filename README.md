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

After the firmware and the owner's Muse SDK token are loaded, the intended
recipient can complete pairing, Wi-Fi, and speech setup without source-code
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
- The Muse SDK token still requires a one-time USB serial transfer. Wit.ai
  setup is performed from a phone on the same Wi-Fi network.
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

## Install the firmware

This section is for the person preparing the gadget. A recipient who receives
an already prepared gadget can skip to [Configure a prepared gadget](#configure-a-prepared-gadget).

### Requirements

- Waveshare ESP32-S3-Touch-LCD-1.54 and a USB-C data cable
- Git and Python 3
- ESP-IDF 6.0.1, activated in the current shell
- A complete 16 MB backup of the board's original flash, stored outside Git

The repository's general web flasher does not currently list the `lcd154`
target. Build and flash this board with its existing wrapper instead of choosing
another board profile. This repository also has no published web-flasher site,
so loading the Muse SDK token remains a preparer step over USB.

From `firmware/esp32`:

```sh
tools/muse/board.sh build lcd154
tools/muse/board.sh flash lcd154
```

The flash command finds the board's native USB Serial/JTAG port when only one
matching board is connected. Pass a port as the final argument if detection is
ambiguous:

```sh
tools/muse/board.sh flash lcd154 COM3
```

Reflashing preserves the Muse pairing, Wi-Fi settings, Muse SDK token, and
Wit.ai token. Do not erase the flash unless the intention is to remove all
stored setup data.

### Load the owner's Muse SDK token

Every gadget needs the owner's Muse SDK token before it can pair. Obtain one
from [Muse Gadget SDK tokens](https://gadgets.muse.ai/settings/sdk-tokens),
open the gadget's USB serial port at 115200 baud, and send this single line:

```text
>sdk_token=mgst_your_token_here
```

The gadget replies `@sdk_token ok` and restarts. Do not place the real token in
a script, build option, source file, or Git commit. Loading this token is a
one-time USB step; later firmware updates preserve it.

## Configure a prepared gadget

These steps require no firmware changes, ESP-IDF installation, or command-line
work.

### Pair with Muse and connect Wi-Fi

1. Install the Muse app on the owner's phone and sign in to the owner's Muse
   account.
2. Power on the gadget. Its setup screen shows a name beginning with
   `MuseGadget-`.
3. In the Muse app, open **Settings > Devices** and enable **Developer mode**.
4. Tap the **+** button, select the matching `MuseGadget-` device, and tap
   **Connect**.
5. Read the community-device warning and select **Continue anyway** only if
   this is the expected gadget and firmware.
6. Enter the Wi-Fi network and password when the app asks for them. The phone
   and gadget must use the same local network for the remaining setup.
7. Return to **Settings > Devices**. Setup is complete when the gadget appears
   under **Other devices**. If the app returns to the connection page after
   accepting Wi-Fi, close and reopen the Muse app once, then check the Devices
   page again.

### Enable spoken replies with Wit.ai

The `lcd154` profile shows Muse's complete text reply and speaks up to the first
280 bytes through Wit.ai. Each owner supplies their own Wit.ai client access
token:

1. Create or open a Wit.ai app and copy its client access token.
2. On the gadget, open **Settings > Sound** and tap **Configure Wit.ai token**.
3. The gadget displays a temporary local URL. Open it on a phone connected to
   the same Wi-Fi network.
4. Paste the client access token and tap **Save and test**.
5. Return to the gadget's Sound page and confirm it reports **Wit.ai ready**.

The gadget validates the token before storing it. It never displays the stored
token. The temporary setup page stops after submission, cancellation, leaving
the Sound page, or five minutes. **Remove Wit.ai token** deletes it directly.
The explicit **Settings > Muse > Reset pairing** transfer reset deletes both
owner-provided service tokens. Automatic pairing recovery preserves them.
Implementation and security details are in
[`docs/wit-token-provisioning.md`](docs/wit-token-provisioning.md).

### Test the gadget

1. Set the speaker volume under **Settings > Sound**.
2. Hold `+/KEY`, speak a short request, and release the button to send it.
3. Confirm that the reply appears as text and plays through the speaker.

## Controls and reset

- Hold `+/KEY` to record a voice request; release it to send.
- Use `BOOT/-` as the auxiliary control.
- Use `PWR` for sleep and shutdown.
- To transfer the gadget to another owner, open **Settings > Muse** and tap
  **Reset pairing** twice within five seconds. This removes Wi-Fi, Muse pairing,
  the Muse SDK token, and the Wit.ai token, then returns the gadget to initial
  setup.

The board implementation is
`firmware/esp32/components/muse/boards/board_waveshare_s3_lcd154.c`. It uses
the upstream Muse board interface and Espressif component drivers. The
Waveshare factory BSP is a pin-map and bus-configuration reference only.

## License

Project-authored code and documentation outside `firmware/` are licensed under
the MIT License. The `firmware/` subtree is licensed under Apache-2.0 as stated
in [`firmware/LICENSE`](firmware/LICENSE). Third-party assets retain their
original licenses.
