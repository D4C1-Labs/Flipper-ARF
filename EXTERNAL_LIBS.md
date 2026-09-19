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

## toolbox symbols exported for the private libs

Because `lib/subghz`, `lib/nfc`, `lib/lfrfid` are no longer linked into the
firmware, any *firmware* symbol they call must be part of the public SDK API
(`targets/f7/api_symbols.csv`) so the app loader can resolve it at runtime.
Most already were, but some `lib/toolbox` helpers used only internally by these
libs were not exported. If you extract another lib or bump these libs and hit
`MissingImports`/`unresolved` for a `lib/toolbox` (or other firmware) function,
export it the SDK way — **do not hand-edit the CSV blindly**:

1. Add the header to `SDK_HEADERS` in the owning lib's SConscript
   (e.g. `File("buffer_stream.h")` in `lib/toolbox/SConscript`).
2. Build once — the SDK checker stops with "API version is still WIP" and writes
   the new entries into `targets/f7/api_symbols.csv` with status `?`
   (and bumps `Version` to `v`).
3. Flip those `?` to `+` and the `Version,v,X.Y` to `Version,+,X.Y`, then rebuild.

Already exported this way: the 9 `buffer_stream.h` symbols (`buffer_get_data`,
`buffer_get_size`, `buffer_reset`, `buffer_stream_alloc`, `buffer_stream_free`,
`buffer_stream_get_overrun_count`, `buffer_stream_receive`, `buffer_stream_reset`,
`buffer_stream_send_from_isr`) — needed by `lib/nfc` (nfc.c, mf_classic, felica…)
and `lib/lfrfid` (raw worker). `mbedtls_des3_*` stay `-` (NOT exported): apps
that need them link `mbedtls` statically via `fap_libs=["mbedtls"]`.

## Where the private libs come from

The private lib each app carries is **the same firmware library**, reached
through a relative symlink — there is no separate copy on disk:

- `applications/*/<app>/lib/subghz -> ../../../../lib/subghz`
- `applications/*/<app>/lib/nfc    -> ../../../../lib/nfc`
- `applications/*/<app>/lib/lfrfid -> ../../../../lib/lfrfid`

(4 `../` for apps at `applications/<category>/<app>/`.) The `fap_private_libs`
`Lib(name=...)` then compiles that symlinked tree into a per-app static lib.
So edits to `lib/subghz` etc. affect both the firmware headers and every app's
private lib automatically.

---

# Updating a vendored SubGhz app (ProtoPirate case study)

ProtoPirate (`applications/system/ProtoPirate`) is a vendored-protocol SubGhz app
tracked from upstream `https://protopirate.net/ProtoPirate/ProtoPirate`. It is
NOT a git submodule/subtree — it lives inside this repo, so updates are a manual
"copy upstream + re-apply our XIP adaptations". Everything we change lives in
**files under our control** (`application.fam` + `api/`); upstream `.c`/`.h`
files are kept byte-identical so future merges stay easy.

## The 4 XIP adaptations that MUST survive an update

1. **`lib/subghz` symlink** — `applications/system/ProtoPirate/lib/subghz ->
   ../../../../lib/subghz`. Preserve it (don't delete `lib/`).
2. **`api/` table** — `api/protopirate_api_table.cpp` (defines
   `subghz_application_api_interface`) + `api/protopirate_api_table_i.h` (the
   symbol list). These expose the SubGhz lib symbols the runtime `.fal` plugins
   import. Preserve both.
3. **Resolver wiring in the 3 host files** — each of
   `helpers/protopirate_protocol_plugin_host.c`,
   `helpers/protopirate_psa_bf_host.c`,
   `helpers/protopirate_tool_scene_host.c` needs, after its
   `#include <loader/firmware_api/firmware_api.h>`:
   ```c
   extern const ElfApiInterface* const subghz_application_api_interface;
   ```
   and, right after every `composite_api_resolver_add(resolver, firmware_api_interface);`:
   ```c
   composite_api_resolver_add(resolver, subghz_application_api_interface);
   ```
   (2 sites in protocol_plugin_host.c, 1 each in the other two.)
4. **`application.fam`** — the `_PROTOPIRATE_SUBGHZ_LIB = Lib(name="subghz", …
   sources=["*.c*","blocks/*.c*","devices/*.c*","devices/cc1101_int/*.c*",
   "protocols/base.c"])` block (curated: infra only, NOT the vendored concrete
   protocols), plus on the main `proto_pirate` App: `sources=_MAIN_APP_SOURCES +
   ["!lib"]`, `fap_libs=["hwdrivers"]`, `fap_private_libs=[_PROTOPIRATE_SUBGHZ_LIB]`,
   `stack_size=8*1024`.

## Step-by-step update procedure

```bash
# 1. Clone upstream to a temp dir
git clone --depth 1 https://protopirate.net/ProtoPirate/ProtoPirate.git /tmp/pp
DEST=applications/system/ProtoPirate

# 2. See what changed / is new (excluding our api/ and lib/)
for f in $(cd /tmp/pp && find . -type f -not -path './.git*'); do
  if [ -f "$DEST/$f" ]; then diff -q "$DEST/$f" "/tmp/pp/$f" >/dev/null || echo "CHANGED: $f";
  else echo "NEW: $f"; fi; done

# 3. Copy everything EXCEPT application.fam (merge that by hand) and never touch api/ or lib/
for f in $(cd /tmp/pp && find . -type f -not -path './.git*' -not -name application.fam); do
  mkdir -p "$DEST/$(dirname "$f")"; cp "/tmp/pp/$f" "$DEST/$f"; done
```

4. Re-apply adaptation **#3** to the 3 host files (they got overwritten).
5. Merge **#4**: `cp /tmp/pp/application.fam $DEST/application.fam`, then re-inject
   the private-lib block + the main-app XIP fields (see above). Keep any NEW
   upstream apps/plugins/sources from the merge.
6. Build the app only, then verify plugins resolve (see next section). Any newly
   imported SubGhz symbol goes into `api/protopirate_api_table_i.h`; any newly
   imported app-level (`protopirate_*`) symbol means a plugin needs an extra `.c`
   added to its `sources` in the fam.

## Gotchas learned from the v3.4 update

- **New plugins that drag in the host stack.** v3.4 added
  `protopirate_config_plugin`, which compiles `protopirate_txrx.c` +
  `protopirate_protocol_plugin_host.c` and therefore pulls the whole RX radio +
  plugin-loader stack. To make that `.fal` self-contained we added to its
  `sources`: `helpers/protopirate_radio.c`, `helpers/radio_device_loader.c`,
  `views/protopirate_receiver.c`, `protopirate_history.c`,
  `helpers/protopirate_storage.c`, `protocols/protocols_common.c` (defines the
  `FF_*` flipper-format key strings), and `api/protopirate_api_table.cpp`
  (defines `subghz_application_api_interface`).
- **Generated icons header name.** `views/protopirate_receiver.c` does
  `#include "proto_pirate_icons.h"`. That header is generated from the app's
  `fap_icon_assets` and named `<appid>_icons.h`. For a plugin whose appid differs,
  set `fap_icon_assets_symbol="proto_pirate"` in its App() so the generated header
  matches the upstream include — this avoids editing upstream sources.
- **Resolving is iterative.** Adding one `.c` can pull in a new family of
  undefined symbols (e.g. `radio_device_loader.c` pulled `subghz_devices_begin/
  end/get_by_name/is_connect`). Re-run the verify command after each change until
  it prints nothing.

---

# Troubleshooting a broken external SubGhz / NFC / RFID app

Symptoms and how to chase them down. The root cause is almost always a symbol the
private lib (or a plugin) needs that is neither in the firmware SDK
(`api_symbols.csv`) nor in the app's own API table.

## Symptom → meaning

- On device, opening the app **reboots with an out-of-memory crash**, or the log
  shows `XIP: unresolved fast rel record N` and then cuts off → a symbol used by
  the XIP'd `.fap` is unresolved; the XIP relocation aborts mid-stream.
- `Status [3]: … MissingImports` in the loader log → same root cause, but the app
  fit in RAM (no XIP) so it failed cleanly instead of crashing.
- `[E][Fap] Failed to preload …` while browsing → the `.fap` header/manifest
  couldn't be read (usually a stale/othertarget build), not a symbol issue.

## Fast offline check (do this before flashing)

Compare the app's *undefined* symbols against everything that can resolve them
(firmware SDK + the app's own API table). Anything left over (minus known
libgcc/linker builtins) is a real problem.

```bash
NM=/opt/postmarket/test/Flipper-ARF/toolchain/current/bin/arm-none-eabi-nm
ELF=build/f7-firmware-C/.extapps/<app>_d.elf     # or a *_plugin_d.elf

# symbols exported by the firmware SDK
grep -E "^(Function|Variable),\+," targets/f7/api_symbols.csv | awk -F, '{print $3}' | sort -u > /tmp/resolvable.txt
# (SubGhz vendored apps also add their own table; append it:)
grep -A1 "API_METHOD(\|API_VARIABLE(" applications/system/<app>/api/*_api_table_i.h \
  | grep -vE "API_METHOD|API_VARIABLE|^--" | tr -d ' ,' >> /tmp/resolvable.txt
sort -u -o /tmp/resolvable.txt /tmp/resolvable.txt

# undefined in the app that nothing can resolve (ignore builtins)
"$NM" -u "$ELF" 2>/dev/null | awk '{print $2}' | sort -u \
  | comm -23 - /tmp/resolvable.txt \
  | grep -vE "^__|uxTopUsedPriority"
```

Known-harmless leftovers (resolved at final `.fal`/loader link): `__aeabi_*`,
`__paritysi2`, `__popcountsi2`, `uxTopUsedPriority`. Also ignore `FF_*` style
`extern const char[]` if the app defines them in one of its own `.c` (e.g.
`protocols_common.c`) — check with `grep`.

## Fix depending on where the symbol lives

- **Symbol is a firmware/`lib/toolbox` function not in the SDK** → export it via
  `SDK_HEADERS` (see "toolbox symbols exported" above). Fixes the app AND every
  other external app.
- **Symbol is a SubGhz lib function** and the app is a vendored SubGhz app with
  `.fal` plugins → add it to that app's `api/*_api_table_i.h` (with the exact
  signature from the lib header) so the plugin resolver can satisfy it. Include
  the relevant `subghz/*.h` at the top of the table if needed.
- **Symbol is an app-level function** (`<app>_*`) undefined in a `.fal` plugin →
  the plugin compiles a `.c` that *calls* it but not the `.c` that *defines* it.
  Add the defining `.c` to that plugin's `sources` in `application.fam`. Watch for
  cascades (adding a `.c` can introduce new undefined symbols) and for duplicate
  definitions (don't add a `.c` whose symbols another listed source already
  defines).
- **Symbol is a `mbedtls_*`/`bit_lib`/`assets` function** → the app must link that
  static lib: `fap_libs=["mbedtls", "bit_lib", "assets"]` (nfc needs all three;
  subghz needs none).

## NFC-specific notes

- Every external NFC app needs `fap_libs=["mbedtls","bit_lib"]` (the nfc lib uses
  both) plus the `nfc` private lib. `furi_hal_nfc` stays in the firmware SDK.
- App API table for the bundled NFC app is
  `applications/main/nfc/api/nfc_app_api_table_i.h` (exposes the lib symbols the
  card parsers / protocol `.fal` plugins import). If a parser plugin fails to
  resolve, add the missing lib symbol there.
- **C vs C++ linkage trap:** the api table is a `.cpp`. Any app-layer header it
  includes to reference a symbol MUST have `extern "C"` guards, or C++ name
  mangling makes the symbol undefined. This bit us with
  `helpers/protocol_support/emv/emv_render.h` and `helpers/nfc_emv_parser.h`
  (they lacked the guards; every other table header had them). Symptom: nm shows
  both a mangled `_Z…name` and a plain `name` undefined.
- A symbol that is *declared + called + listed in the table but never defined*
  (dead code upstream tolerates via `--gc-sections`) becomes undefined once the
  table takes its address. We hit `nfc_render_emv_name` (had no definition in
  Moon/Momentum either) and had to define it in `emv_render.c`.

## RFID-specific notes

- External RFID apps carry the `lfrfid` private lib (symlink +
  `fap_private_libs=[Lib(name="lfrfid", …)]`). No `mbedtls` needed.
- RFID apps are usually small enough to load into RAM (log:
  `App fits in RAM … skipping XIP`), so unresolved symbols show as a clean
  `MissingImports` rather than a reboot. Same fix paths as above — most commonly
  a missing `lib/toolbox` export (this is exactly how the `buffer_stream_*` gap
  surfaced first on `lfrfid.fap`).
