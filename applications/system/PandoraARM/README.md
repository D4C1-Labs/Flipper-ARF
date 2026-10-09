# Pandora D-605 (ARM) keyfob emulator for Flipper Zero

This app runs the **real, unmodified firmware** of a Pandora D-605 car-alarm
keyfob (Silicon Labs EFM32, ARM Cortex-M3) on the Flipper Zero. It emulates the
keyfob hardware and bridges the Flipper's CC1101 radio, so the firmware behaves
exactly like the physical keyfob:

- **CPU**: a Thumb-2 interpreter runs the firmware's ARM code.
- **Display**: the keyfob OLED is captured from the firmware's SPI writes and
  drawn on the Flipper screen.
- **Buttons**: the Flipper D-pad is wired to the keyfob's 6 physical buttons.
- **Radio**: the firmware talks to its Si4432 radio; the app bridges those
  operations to the Flipper's CC1101 (RX/TX, OOK/FSK, frequency).

The app is **firmware-agnostic**: it emulates the physical D-605 hardware, so any
D-605 firmware (`pandora`, `pandora max`, or other versions) should work.

## Button mapping (Flipper D-pad -> keyfob button number)

The Flipper D-pad is mapped so it matches the keyfob layout visually:

```
          Flipper key        keyfob button   function
          -----------        -------------   --------
   UP        ▲                   6            trunk        (hold: back to menu)
   LEFT      ◄                   2            confirm/mode (hold: erase cell)
   OK       (OK)                 5            AM+FM / open car
   RIGHT     ►                   1            On / menu / cell +
   DOWN      ▼                   4            RX frequency / close car
   BACK      (back)              3            menu / cell -  (hold: auto)
```

- A **short press** = short click of that keyfob button.
- **Holding** a key = long press of that keyfob button. The firmware decides what
  each button does in each mode/screen.
- **Hold UP (>1s)** = EXIT the app (host-side, does not reach the keyfob).
- **Hold BACK (>1s)** = cycle the RF bridge (OFF / TX / RX).

## The unlock PIN

The real keyfob boots to a **black screen** until the correct unlock **PIN** is
entered. The PIN is a blind sequence of keyfob buttons (not digits). The app
**auto-enters the PIN on launch** so you land on the main screen:

- `pandora`: UP, OK, OK, UP, UP, DOWN
- `pandora max`: UP, OK, DOWN, OK, UP, DOWN

You can change the PIN sequence from the main menu: open the **`PIN:`** item and
build the sequence with the on-screen buttons (UP / DOWN / OK / BACK / B5 / B6),
then **Done** (or **Clear** to reset). Even though it is entered with button
names, it is the firmware's unlock PIN. If you leave it empty, the app uses the
detected profile's default PIN.

## What the app needs (firmware file)

You must provide the **Pandora D-605 firmware image** as an Intel HEX (`.hex`) or
raw binary (`.bin`) file on the Flipper SD card (e.g. under `/ext/` or
`/ext/Pandora_FW/`). On launch:

1. Open **`Firmware:`** in the menu and pick your `.hex`/`.bin`.
2. The app detects the profile (`pandora` / `pandora max`) from the image.
3. Pick **`Launch`**. The app loads the firmware (paged from the SD card to fit
   the Flipper's limited RAM), auto-enters the PIN, and shows the keyfob screen.

Notes:
- `.hex` (Intel HEX) and flat `.bin` are both accepted; the app normalises the
  image to a flat binary on the SD card and pages it on demand.
- The firmware runs **unmodified** — this app only emulates the hardware around
  it. Nothing is patched in the firmware.

## RF bridge (CC1101)

Both firmwares drive the Si4432 in **direct mode** (bit-bang OOK/FSK), not the
packet/FIFO mode. The bridge maps:

- Si4432 `OP_MODE` RXON -> CC1101 async RX; the demodulated OOK stream is fed to
  the firmware's RX-data pin (PB0), which the firmware demodulates itself.
- Si4432 `OP_MODE` TXON -> the firmware bit-bangs the waveform on its TX-data pin
  (PB3); the bridge transmits it on the CC1101.
- Carrier frequency and modulation are taken from the Si4432 registers the
  firmware programs (0x75-0x77 freq, 0x71 modulation).

Decoding/encoding of received/transmitted frames is done by the **firmware
itself** (its demod ISR and protocol decoders) — the bridge only moves the raw
RF between the CC1101 and the firmware's radio pins. A real RF frame received by
the Flipper is therefore decoded by the keyfob firmware as it would on real
hardware.

## Diagnostics

The app logs to the Flipper CLI (`log` / `log debug`). Useful lines:

- `boot OK -> PC=... insn=...` and `unlock OK/FAILED` (PIN result)
- `poll_raw_keys: 0xNN` (which buttons are seen)
- `nav_button: btn=.. ...` (button injected into the firmware)
- `hb: ninsn=.. rate=.. k-insn/s` (emulation speed heartbeat)
- `si4432: REG[0x07]=.. -> mode TX/RX/READY/IDLE` (radio mode changes)
- `RF: CC1101 RX started @.. Hz`, `RX hb: air_edges=+X` (RX activity)
- `LED: port.. pin.. -> ON/OFF`, `BUZZER? ...` (LED / buzzer detection)

## Credits / scope

Research/analysis of firmware the user owns. The firmware is not included; you
supply your own D-605 image. The app emulates the keyfob hardware for
interoperability and analysis on the Flipper Zero.
