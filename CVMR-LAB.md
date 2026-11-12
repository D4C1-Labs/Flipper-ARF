# CVMR-Lab unified firmware image

**Defensive automotive RF lab build** — derived from **Flipper-ARF** (`D4C1-Labs/Flipper-ARF`), not a merge of every community fork.

## Why this base (not doctor / Momentum / FoxFW alone)

| Candidate | Verdict for *one* lab image |
|-----------|----------------------------|
| **Flipper-ARF** | **Chosen base** — largest integrated automotive protocol matrix (OEM enc/dec, KeeLoq/PSA/HiTag helpers, mod hopping). Purpose-built for authorized research. |
| doctor-firmware | **Complement, not merge (v1)** — unique FM `rx_raw` preset CLI for KAT; port touches `subghz_cli` + device layer (~500+ LOC vs ARF). Use second Flipper or KAT+HackRF for FM until port lands. |
| Momentum / Unleashed | General-purpose; automotive decoders mostly via external FAPs. Good **second** device for Sibyl/ProtoPirate. |
| FoxFW | ARF protocols + extra brute apps — **excluded** from CVMR unified image (high-sensitivity garage brute); keep on isolated profile if needed. |
| ARF-Shuka | **Optional overlay** — six extra OEM protocols; merge from `shuka0158/ARF-Shuka-Edition` when fleet needs GM/Toyota/Nissan coverage (verify on vehicle). |

## What “complete” means for CVMR

In-tree (this firmware):

- Full **ARF** automotive Sub-GHz stack and research UI
- CLI `subghz rx_raw` / `tx_from_file` (OOK-centric; FM via CC1101 presets in app, not doctor-style USB preset yet)

Shipped beside the `.tgz` (SD card — run `scripts/cvmr-sd-bundle.sh`):

- ProtoPirate (canonical build from protopirate.net when available)
- Optional: Sibyl, RollLab, car_breaker_19 (defensive RX), kakuzu automotive `.sub` regression set (paths only; no TX policy)

Desk stack (not flashable as one image):

- **KAT** + HackRF/RTL-SDR for FM, Compare, CVE metadata
- **Proxmark3** for Hitag2 / immobilizer

## Build

```bash
cd /agent/cvmr-firmware
./fbt COMPACT=1 DEBUG=0 updater_package
```

Artifacts: `dist/f7-C/flipper-z-f7-update-cvmr-lab.tgz` (after `DIST_SUFFIX=cvmr-lab` in `fbt_options.py`).

Flash: qFlipper → Update → Install from file → select the `.tgz`.

## Legal

Authorized vehicles and lab rigs only. See Project store `docs/project-context.md`.
