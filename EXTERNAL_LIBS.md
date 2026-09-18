# External libraries (Sub-GHz / NFC / LF RFID) — maintainer notes

This firmware does **not** link the Sub-GHz, NFC or 125 kHz RFID protocol
libraries into the firmware image. Each app that needs one of these libraries
carries its own **private copy** and executes it from flash via the XIP loader.
This keeps the firmware small (~480 KB, ~379 KB of free internal flash) so the
Sub-GHz app can ship the full protocol catalog and there is room for new
protocols and apps.

## What stays in the firmware

- `furi_hal_subghz` and `furi_hal_nfc` (the hardware abstraction layers) stay in
  the firmware and are exposed through the SDK.
- The libraries `lib/subghz`, `lib/nfc`, `lib/lfrfid` are **built but not linked**
  into the firmware: they are removed from `lib/SConscript` `BuildModules` and
  from `targets/f7/target.json` `linker_dependencies`. Their header roots are
  still on the include path (`#/lib/subghz`, `#/lib/nfc`, `#/lib/lfrfid` in
  `lib/SConscript`) so both the firmware core and external apps compile.

## How an app carries a private library

1. Symlink the firmware library into the app's `lib/` folder, e.g.
   `applications/system/<app>/lib/nfc -> ../../../../lib/nfc`.
2. In the app's `application.fam`:
   ```python
   fap_libs=["mbedtls", "bit_lib"],   # deps the nfc lib needs at link time
   fap_private_libs=[
       Lib(name="nfc", fap_include_paths=[".", ".."], cflags=["-Wno-error"]),
   ],
   ```
   `fap_include_paths=[".", ".."]` makes both `#include <nfc/...>` and
   `#include "..."` styles resolve; `<lib/nfc/...>` resolves via the app work dir.
   `mbedtls`/`bit_lib` are required by `lib/nfc`; the Sub-GHz lib needs neither.
3. If the app already declares `fap_private_libs` (e.g. `asn1`, `loclass`,
   `amiitool`), **add** the library to that list, don't replace it.
4. If the app has its own source folder named exactly like the library
   (e.g. a folder `nfc/`), name the private lib differently (e.g. `fwnfc`) so the
   generated `.a` doesn't collide with the folder, and add a second symlink for
   include resolution. See `applications/system/TagTinker` and
   `applications/system/wmbuster`.

## Runtime symbol resolution for .fal plugins

Standalone/embedded `.fal` plugins resolve library symbols **at load time**
against the host app's composite resolver. If a plugin references library
symbols that are no longer in the firmware API, they must be exposed in the
host app's private API table:

- Sub-GHz: `applications/main/subghz/api/subghz_app_api_table_i.h` exposes the 7
  CC1101 preset arrays for the `radio_device_cc1101_ext` plugin.
- NFC: `applications/main/nfc/api/nfc_app_api_table_i.h` exposes the lib symbols
  the bundled card parsers and protocol plugins import at runtime.

## Vendored-protocol apps (ProtoPirate / RollJam / nfc_magic) — DONE

These apps ship their own copy of protocol code. They are handled with a
"curated" private library that provides only the shared infrastructure and lets
the app keep its vendored protocols:

- `applications/system/ProtoPirate` and `applications/system/RollJam` — vendor
  their own `protocols/` (concrete car-key protocols + registry). Their private
  `subghz` lib is defined with an explicit include-only `sources` list that
  compiles only the infrastructure (`*.c*`, `blocks/*`, `devices/**`,
  `protocols/base.c`) and NOT the vendored concrete protocols. Their runtime
  `.fal` plugins resolve SubGhz symbols through a per-app private API table
  (`api/*_api_table_i.h` exposing the device/environment/receiver/transmitter/
  worker/setting/blocks symbols + 7 CC1101 presets), added to each plugin
  composite resolver via `subghz_application_api_interface`.
- `applications/system/nfc_magic` — vendored its own `crypto1` and
  `mf_classic_process_error` under `magic/protocols/gen2/`. Fixed by dropping the
  vendored duplicates: `gen2/crypto1.c` is excluded from the app sources
  (`"!crypto1.c"`) and its `gen2_poller_i.h` now includes the lib's
  `<nfc/helpers/crypto1.h>` and `<nfc/protocols/mf_classic/mf_classic_poller_i.h>`;
  the duplicate `mf_classic_process_error` definition was removed from
  `gen2_poller_i.c`. The lib provides identical implementations.

## Apps left DISABLED for manual review

- `applications/system/genie-recorder` — vendors an OLD, incompatible copy of
  the SubGhz block layer: its `protocols/generic.h` declares a smaller
  `SubGhzBlockGeneric` struct (missing `data_2`, `cnt_2`, `seed`) than the
  current `lib/subghz/blocks/generic.h`. Mixing genie's struct with the lib's
  implementation (or vice-versa) is an ABI mismatch that would corrupt memory at
  runtime, so it cannot be linked against the current private lib safely. The
  app must be updated to the current SubGhz block API before re-enabling.
  Its `application.fam` is renamed to `application.fam.disabled`.
