# Project prompt: Muse gadget port to Waveshare ESP32-S3-Touch-LCD-1.54

## Role

You are working with me on embedded firmware for an ESP32-S3 board. I am an experienced technologist and hardware hobbyist. Be direct and precise. When you are not sure of a fact about the hardware, the SDK, or the Muse service, say so and tell me how to verify it. Do not guess pin numbers, register values, or API behavior.

## Goal

Port Meta's open-source Muse Gadget SDK (ESP32 firmware) to the Waveshare ESP32-S3-Touch-LCD-1.54 so the board works as a handheld voice companion for Muse (muse.ai). I will test it with my own Muse account first, then reset it and give it to a friend, who will pair it with his own account using written instructions.

## Hardware

Waveshare ESP32-S3-Touch-LCD-1.54 (also sold by SpotPear). I have two units.

- ESP32-S3R8, dual-core LX7 at 240 MHz, 8 MB PSRAM, 16 MB external flash
- 1.54" LCD, 240×240, 262K color, touch
- ES7210 ADC with dual-mic array and echo cancellation (audio in)
- ES8311 codec with NS4150B amplifier and onboard speaker (audio out)
- QMI8658 6-axis IMU
- Three user buttons, plus BOOT and PWR
- MX1.25 3.7V LiPo charge/discharge header
- microSD slot
- USB-C (native USB for flashing and console)
- I2C and UART pads

Source of truth for pins, display controller, touch controller, and init sequences: the Waveshare wiki page and demo code for this exact board. Read those before writing drivers. Do not assume the pinout matches any other Waveshare board.

## Software baseline

- Upstream: `github.com/facebookincubator/muse-gadget-sdk` (Apache 2.0, with some assets excluded, such as avatar images). Use the `esp32` folder.
- Toolchain: ESP-IDF 6.0.x (confirm the exact version the SDK pins).
- Closest existing board: Waveshare ESP32-S3-Touch-AMOLED-1.75C, Meta's recommended starter board. It likely uses the same ES7210/ES8311 audio path. Confirm by reading its board definition.
- Useful reference: `github.com/raullenchai/esp32-thinks`, a patch set on top of the SDK that adds configurable STT/chat/TTS and has backup and flashing scripts.

## Phases

### 1. Bring-up
- Back up the full factory flash of board 1 with esptool before anything else.
- Build and run Waveshare's demos to confirm the display, touch, mics, speaker, buttons, IMU, and battery all work.
- Record the confirmed pin map and peripheral addresses in `docs/board-notes.md`.

### 2. Port
- Read how the SDK defines boards: folder layout, Kconfig and sdkconfig defaults, display, audio, and input abstractions. Summarize this for me before writing code.
- Create a new board definition for the 1.54" board, copied from the AMOLED-1.75C and then changed:
  - Display driver and panel init for the 1.54" LCD
  - Touch controller driver
  - UI layout for a 240×240 square screen instead of a round one
  - I2C and I2S pin remap for the ES7210 and ES8311
  - Button mapping, with push-to-talk on a physical button
  - Battery voltage readout if the hardware supports it
- Keep the changes isolated to the board definition and drivers where possible, so upstream updates merge cleanly. If an upstream file has to change, keep that change minimal and explain why.

### 3. Test on my account
- Pair through the Muse app: SDK token from gadgets.muse.ai, developer mode on, then Settings > Devices.
- Verify the full loop: push-to-talk, speech captured, response shown, and audio out if the SDK supports spoken replies.
- Measure and report:
  - Round-trip latency
  - Speech recognition accuracy at arm's length
  - Speaker volume
  - Idle and active current draw
  - Estimated battery life
- Find out whether the stock SDK speaks replies or only shows text. If it only shows text, propose options for adding TTS (for example, the esp32-thinks approach) before implementing anything.

### 4. Handoff readiness
- **Credentials isolation.** Store all account-bound state (SDK token, Wi-Fi credentials, any pairing keys or device IDs) in one NVS namespace or a dedicated partition.
- **Factory reset.** Holding a specific button at boot wipes that state completely and returns the device to first-run setup. The screen confirms the reset.
- **First-run setup.** No toolchain needed. Wi-Fi and token are entered on the device, through a captive portal or the pairing flow, whichever the SDK supports. Never hardcode my token or Wi-Fi in firmware or source.
- **Status screens.** Clear on-screen states for:
  - Setup needed
  - Connecting
  - Paired and ready
  - Listening
  - Thinking
  - Speaking
  - Error, with a short reason
  - Low battery
- **Browser flasher.** A web flasher page (ESP Web Tools) and a release binary, so a non-developer can reinstall the firmware.
- **Handoff test.** Factory reset, unpair from my Muse app, then do the whole setup from scratch using only the written instructions.

### 5. Documentation
- `docs/board-notes.md`: pinout, peripherals, and quirks found.
- `docs/build.md`: how to build, flash, and debug.
- `docs/setup-guide.md`: a one-page guide for my friend:
  - Charging and power
  - Getting his own SDK token
  - Developer mode and pairing
  - Wi-Fi setup (2.4 GHz only)
  - How to talk to it
  - What each screen means
  - Factory reset
  - Reinstalling firmware with the browser flasher

## Constraints

- SDK tokens are per gadget and the SDK terms limit them to personal, non-commercial use. My friend uses his own token. Flag anything in gadgets.muse.ai/sdk-terms that affects this plan.
- Never commit secrets. Keep tokens and Wi-Fi in a gitignored local config or in NVS only.
- Board 2 stays untouched until board 1 works end to end.
- Before any erase, flash, or partition-table change, tell me what will be overwritten and confirm a backup exists.
- Match the existing code style of the SDK. Prefer ESP-IDF components and the SDK's own abstractions over new dependencies.

## Working style

- Start each phase by stating what you will read or change and what "done" looks like.
- Show diffs for upstream files you touch.
- When something fails, give the exact error, your hypothesis, and the next diagnostic step. Do not stack speculative fixes.
- Keep a running `docs/progress.md` with what works, what doesn't, and open questions.
- Writing style for docs: plain, concrete, no marketing language, no em dashes.

## Open questions to resolve early

1. Exact display controller and touch controller on this board.
2. Whether the SDK's audio pipeline uses the ES7210's echo cancellation or needs configuration for it.
3. Whether replies are spoken natively or text only.
4. Whether the token is entered on the device or bound through app pairing alone.
5. Whether a gadget paired to one Muse account can be cleanly re-paired to another after a factory reset.
