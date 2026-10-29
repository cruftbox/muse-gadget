# Waveshare ESP32-S3-Touch-LCD-1.54 board notes

These values are the implemented board contract in
`firmware/esp32/components/muse/boards/board_waveshare_s3_lcd154.c`.

## Display and touch

The 240 x 240 ST7789 display uses SPI2 in mode 3 at 40 MHz. The pins are MOSI
GPIO39, SCLK GPIO38, CS GPIO21, DC GPIO45, reset GPIO40, and backlight GPIO46.
The panel uses RGB color order and inverted output. LVGL uses two 40-line DMA
buffers. There is no display-enable or tearing-effect pin.

The CST816S touch controller uses I2C0 with SDA GPIO42 and SCL GPIO41. Its
interrupt is GPIO48 and reset is GPIO47.

The backlight uses LEDC channel 0 at 5 kHz with 10-bit duty control.

## Audio

I2S0 runs as master with 16-bit stereo Philips framing. MCLK is GPIO8, BCLK is
GPIO9, word select is GPIO10, codec input is GPIO11, codec output is GPIO12,
and speaker-amplifier enable is GPIO7. DMA uses four descriptors of 160 frames.

The ES8311 drives the speaker and the ES7210 captures the two microphones at
their driver-default I2C addresses.

## Buttons and power

All three buttons are active low:

- `+/KEY`: GPIO4
- `PWR`: GPIO5
- `BOOT/-`: GPIO0

The power-button long-press threshold is 1.5 seconds. A power-button press held
during boot is consumed by initialization so it does not immediately trigger a
UI action.

GPIO2 holds the battery rail on after the power button is released. Firmware
asserts it during board initialization and lowers it for shutdown. GPIO3 is the
active-low charge-status input.

Battery voltage is measured on ADC1 channel 0, GPIO1, through a 200 kohm / 100
kohm divider. The board exposes a two-pin 1.25 mm battery connector marked
`BAT`. Connector polarity must be checked against the board markings before a
replacement battery is attached.

## Memory

The target configuration uses 16 MB flash and 8 MB octal PSRAM. Large display,
Muse session, and speech buffers are allocated from PSRAM so internal RAM stays
available for Wi-Fi, TLS, and DMA.
