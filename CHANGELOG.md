# Changelog

---

### Added
- All SubGHz protocols enabled by default, including the full automotive and
  keyfob/gate catalog (KIA, Renault, Fiat, Ford, Honda, PSA, VAG, Subaru,
  Chrysler, Star Line, Scher-Khan and more). Nothing to toggle: every supported
  protocol is available out of the box.
- New car preset pack for better range and reception. Adds tuned presets for
  common remotes (Honda, VAG, PSA, Renault, FCA, KIA) plus general AM/FM
  sensitivity presets. Your original presets are untouched.
- GM (General Motors) car remote support: capture and replay.
- Renault Seed recovery ("Seed BF") for classic Renault remotes. Once
  recovered, the seed is saved to the file so you don't have to run it again.
- Visual lock screen menu: a redesigned quick-access grid to toggle SubGHz,
  Bluetooth, sound/stealth, ProtoPirate, brightness and volume without leaving
  the menu.
- SubGHz auto-save on receive: captured signals can be saved automatically, with
  an option to skip duplicates so your history stays clean.
- Protocol name filter in Receiver Config ("Proto Filter"): pick which protocols
  the receiver listens for. Restricting to your target protocol reduces memory
  use and improves the odds of capturing it. Leave all off to disable.

### Changed
- Slimmer firmware, bigger app catalog. The Sub-GHz, NFC and 125 kHz RFID
  libraries no longer live inside the firmware image; each app now ships as a
  self-contained external app that carries its own copy and runs it directly
  from flash (execute-in-place). This shrinks the firmware from ~940 KB to
  ~480 KB and frees a large block of internal flash, which is what lets Sub-GHz
  enable its complete protocol catalog and leaves ample headroom for new
  protocols and apps.
- NFC and 125 kHz RFID are back and fully featured, running as external apps
  (all card types, parsers and protocol plugins included).
- Battery info is now one tap away from the power-off screen.

### Fixed
- Car remotes: pressing the D-pad to pick a different button (Lock, Unlock,
  Trunk, Panic, etc.) now sends and shows the correct button across all
  supported car protocols. Some remotes previously kept sending the same
  button and never advanced their rolling code.
- Fewer false detections: the receiver no longer feeds AM signals to FM-only
  decoders (and vice versa), so protocols like KIA and Fiat stop misreading
  each other's signals.
- Fixed freezes when running the long brute-force operations (Hitag2 and Seed
  recovery); progress now updates smoothly and can be cancelled.

### Want to add or modify an external app?

- If you want to add a new external app or modify one please read: EXTERNAL_LIBS.md
