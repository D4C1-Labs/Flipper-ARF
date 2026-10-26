# Changelog

---

### Added
- **Cars app (ARF Cars):** a new external Sub-GHz app, dedicated to car key
  fobs. It uses the exact same look and feel as the main Sub-GHz app (same
  Receiver, Read RAW, Saved and Transmitter screens), so it feels built-in. It
  carries its own set of automotive protocols, which keeps them out of the main
  firmware and leaves room for everything else. Open it from the Sub-GHz menu
  with "Read Cars".
- **Garage Door app (ARF Garage):** a second external Sub-GHz app, same native
  look, dedicated to garage doors, gates and remotes. It bundles the full set of
  gate/garage protocols. Open it from the Sub-GHz menu with "Read Garage Door".
- **Many new vehicle protocols** are now available (in the Cars app): GM V0, GM
  V1, Toyota V0, Toyota V1, Mazda V1, Mazda V2, Mercedes V0, Mercedes V1, Opel
  V1 and BMW. Each one shows the correct button labels (Lock / Unlock / Trunk /
  etc.) and, where the protocol allows it, can re-transmit a fresh rolling code
  instead of a plain replay.
- **New garage/remote protocols** (in the Garage Door app): Urmet intercom and
  X10 home-automation.
- **Hitag2 key memory:** once you crack a car's Hitag2 key or seed (Fiat V1 /
  Renault V1), it is now remembered on the SD card, keyed by the car's UID. The
  next time you capture the SAME car, the key is applied instantly and no
  brute-force is needed. This works across different captures and survives
  reboots. It runs alongside the existing key dictionary, nothing was removed.
- **Friendlier brute-force screens:** the Hitag2 brute-force, Seed brute-force
  and PSA decrypt screens now show the waiting dolphin while they work, next to
  the live stats (level, keys/sec, ETA, elapsed and progress). Bluetooth offload
  to your phone still works exactly as before.
- **Second external radio supported: Si4463.** Alongside the external CC1101,
  the firmware now ships a driver for Si4463-based external modules. Apps that
  use an external radio pick whichever one is connected automatically.
- **New companion apps** (available from the Apps menu):
  - **Freq Analyzer** — standalone Sub-GHz frequency analyzer (internal or
    external CC1101).
  - **Mod Analyzer** — tells you whether a signal is AM (ASK/OOK) or FM (2-FSK)
    by measuring its RSSI envelope.
  - **Spectrum Analyzer** — spectrogram view of the RF activity around you.
  - **Sub-GHz RAW Edit** — open a recorded RAW `.sub`, find the real burst in the
    noise, and save just the trimmed signal.
  - **Jammer** — RF jammer (lab use only).
  - **NFC Sniffer** — ISO14443-A passive HF sniffer.

### Changed
- **Sub-GHz main menu reorganized.** The entries are now: Read Cars, Read Garage
  Door, Read RAW, plus the rest of the usual tools. "Read" and "Saved" were
  removed from this screen because Cars and Garage Door each open with their own
  Read and Saved views. The new "Read Cars" / "Read Garage Door" items only show
  up when their app is installed.
- **Sub-GHz Bruteforcer** replaced with a newer, improved static-code
  brute-forcer.
- **More protocols shipped out of the main firmware.** Automotive and
  garage/gate protocols now live in the Cars and Garage Door apps instead of
  inside the firmware image. This frees internal flash and keeps NFC, 125 kHz
  RFID and everything else intact.

### Fixed
- **Fiat V0 — fixed crashes and signals that didn't open the car.** Three bugs
  were hiding in this protocol:
  - **Memory leak / crash:** every time you changed the custom button, the app
    reloaded the signal and leaked about 10 KB of RAM each time, crashing the
    Flipper after a dozen presses. The old buffer is now freed before a new one
    is allocated, so you can switch buttons as much as you like.
  - **Wrong frame length:** the transmitter was sending 70 bits in roughly half
    of the attempts, but the car's receiver needs exactly 71 and silently throws
    a 70-bit frame away — which is why it worked sometimes and failed others. It
    now always sends the correct 71-bit frame.
  - **Corrupted button byte:** a bad bit-shift in the transmitter mangled the
    "end byte", so even the transmissions that did go out often didn't open the
    car. The end byte is now sent intact.

### Earlier

#### Added
- Protocol name allowlist filter: in Receiver Config, a new "Proto Filter"
  field accepts a comma-separated list of protocol names (e.g. "Ford V2,VAG").
  When set, the receiver ignores all decoded signals that are not in the list,
  reducing RAM usage and increasing the chance of capturing the target protocol.
  Leave empty to disable (default behavior, all protocols accepted).
  Setting is persisted in last_subghz.settings under the ProtocolFilter key.

#### Changed
- Protocol Filter: replaced free-text input with a dedicated protocol list
  scene (Proto Filter in Receiver Config). All registered protocols are shown
  as toggleable items (--- / ONLY). Selecting one or more protocols restricts
  the receiver to only show those; leaving all as --- disables the filter.
  The active count is shown inline in Receiver Config ("N set" or "All").
  Filter is persisted across sessions and cleared by Reset to default.

---

### Credits & Thanks

Big thanks to the firmwares and authors this release borrows from:

- **FoxFW** — https://github.com/FoxFW — for the external Sub-GHz app design that
  inspired the Cars / Garage Door apps.
- **The Doctor's Firmware** (@thedoctorz) — https://github.com/thedoctorz — for
  the Hitag2 key database, several of the new vehicle protocols (BMW, Mercedes
  V0/V1, Mazda V2, Opel V1, Toyota, Urmet, X10), the waiting-dolphin brute-force
  look, the improved Sub-GHz Bruteforcer, the Si4463 external-radio driver (plus
  the RSSI-burst support it needs), and the companion apps (Freq Analyzer, Mod
  Analyzer, Spectrum Analyzer, Sub-GHz RAW Edit, Jammer, NFC Sniffer).

All imported code was adapted to ARF and credited in-source with `[ARF_PORT]`
comments explaining what changed and why.
