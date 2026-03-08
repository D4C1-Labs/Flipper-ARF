# Flipper-ARF: Automotive Research Firmware

## Overview

**Flipper-ARF** is an independently developed firmware fork for Flipper Zero, based on **Unleashed Firmware** but heavily modified. Unlike standard firmware builds, it removes typical general-purpose functions and focuses exclusively on **automotive research and experimentation**.

The goal of this firmware is to provide a **high-compatibility, protocol-focused build** for personal use, academic study, and responsible security research. It focuses on **automotive remote protocols, rolling code behavior, and keyfob ecosystem compatibility**.

This project may incorporate, adapt, or build upon **other open-source projects** in accordance with their licenses, with proper attribution.

---

## Showcase

| | |
|:---:|:---:|
| ![Home](arf_pictures/home.png) | ![Sub-GHz Scanner](arf_pictures/subghz_scan.png) |
| Home Screen | Sub-GHz Scanner |
| ![Keeloq Key Manager](arf_pictures/keeloq_key_manager.png) | ![Mod Hopping](arf_pictures/mod_hopping.png) |
| Keeloq Key Manager | Mod Hopping Config |
| ![PSA Decrypt](arf_pictures/psa_decrypt_builtin.png) | ![Counter BruteForce](arf_pictures/counter_bruteforce.png) |
| PSA XTEA Decrypt | Counter BruteForce |

---

## Supported Systems

### Automotive Protocols

| Manufacturer | Protocol | Frequency | Modulation | Encoder | Decoder |
|:---|:---|:---:|:---:|:---:|:---:|
| VAG (VW/Audi/Skoda/Seat) | VAG GROUP | 433 MHz | AM | Yes | Yes |
| Porsche | Cayenne | 433/868 MHz | AM | Yes | Yes |
| PSA (Peugeot/Citroën/DS) | PSA GROUP | 433 MHz | FM | Yes | Yes |
| Ford | Ford V0 | 433 MHz | FM | Yes | Yes |
| Fiat | Fiat SpA | 433 MHz | FM | Yes | Yes |
| Fiat | Fiat Mystery | 433 MHz | FM | No | Yes |
| Subaru | Subaru | 433 MHz | AM | Yes | Yes |
| Mazda | Siemens (5WK49365D) | 433 MHz | FM | Yes | Yes |
| Kia/Hyundai | Kia V0 | 433 MHz | FM | Yes | Yes |
| Kia/Hyundai | Kia V1 | 315/433 MHz | AM | Yes | Yes |
| Kia/Hyundai | Kia V2 | 315/433 MHz | FM | Yes | Yes |
| Kia/Hyundai | Kia V3/V4 | 315/433 MHz | AM/FM | Yes | Yes |
| Kia/Hyundai | Kia V5 | 433 MHz | FM | Yes | Yes |
| Kia/Hyundai | Kia V6 | 433 MHz | FM | Yes | Yes |
| Suzuki | Suzuki | 433 MHz | AM | Yes | Yes |
| Mitsubishi | Mitsubishi V0 | 868 MHz | FM | Yes | Yes |

### Gate / Access Protocols

| Protocol | Frequency | Modulation | Encoder | Decoder |
|:---|:---:|:---:|:---:|:---:|
| Keeloq | 433/868/315 MHz | AM | Yes | Yes |
| Nice FLO | 433 MHz | AM | Yes | Yes |
| Nice FloR-S | 433 MHz | AM | Yes | Yes |
| CAME | 433/315 MHz | AM | Yes | Yes |
| CAME TWEE | 433 MHz | AM | Yes | Yes |
| CAME Atomo | 433 MHz | AM | Yes | Yes |
| Faac SLH | 433/868 MHz | AM | Yes | Yes |
| Somfy Telis | 433 MHz | AM | Yes | Yes |
| Somfy Keytis | 433 MHz | AM | Yes | Yes |
| Alutech AT-4N | 433 MHz | AM | Yes | Yes |
| KingGates Stylo4k | 433 MHz | AM | Yes | Yes |
| Beninca ARC | 433 MHz | AM | Yes | Yes |
| Hormann HSM | 433/868 MHz | AM | Yes | Yes |
| Marantec | 433 MHz | AM | Yes | Yes |
| Marantec24 | 433 MHz | AM | Yes | Yes |

### General Static Protocols

| Protocol | Frequency | Modulation | Encoder | Decoder |
|:---|:---:|:---:|:---:|:---:|
| Princeton | 433/315 MHz | AM | Yes | Yes |
| Linear | 315 MHz | AM | Yes | Yes |
| LinearDelta3 | 315 MHz | AM | Yes | Yes |
| GateTX | 433 MHz | AM | Yes | Yes |
| Security+ 1.0 | 315 MHz | AM | Yes | Yes |
| Security+ 2.0 | 315 MHz | AM | Yes | Yes |
| Chamberlain Code | 315 MHz | AM | Yes | Yes |
| MegaCode | 315 MHz | AM | Yes | Yes |
| Mastercode | 433 MHz | AM | Yes | Yes |
| Dickert MAHS | 433 MHz | AM | Yes | Yes |
| SMC5326 | 433 MHz | AM | Yes | Yes |
| Phoenix V2 | 433 MHz | AM | Yes | Yes |
| Doitrand | 433 MHz | AM | Yes | Yes |
| Hay21 | 433 MHz | AM | Yes | Yes |
| Revers RB2 | 433 MHz | AM | Yes | Yes |
| Roger | 433 MHz | AM | Yes | Yes |
| BinRAW | 433/315/868 MHz | AM/FM | Yes | Yes |
| RAW | All | All | Yes | Yes |

---

### How to Build

Compact release build:

```
./fbt COMPACT=1 DEBUG=0 updater_package
```

---

## Project Scope

Flipper-ARF aims to achieve:

- Maximum compatibility with automotive Sub-GHz rolling and static protocols
- Accurate OEM-style remote emulation
- Stable encoder/decoder implementations
- Modular protocol expansion

**Primary focus:** VAG, PSA, Fiat, Ford, Asian platforms, and aftermarket alarm systems.

> ⚠ This is a protocol-focused research firmware, not a general-purpose firmware.

---

## Implemented Protocols

- [x] Mazda Siemens Protocol (5WK49365D) — ported from open-source references (testing required)
- [x] Full VAG, Fiat, Ford, Subaru, Kia, PSA support
- [x] D-Pad mapping (Lock / Unlock / Boot / Trunk) during emulation
- [x] VAG MFKey support and updated Keeloq codes
- [x] PSA XTEA brute force for saved → emulation workflow
- [x] Brute force of counter in saved → emulation scene for smoother keyfob emulation
- [x] RollJam app (Internal CC1101 for RX & TX captured signal; External CC1101 for jamming) — requires more real-world testing

---

## To Do / Planned Features

- [ ] Keeloq Key Manager inside firmware
- [ ] Add Scher Khan & Starline protocols
- [ ] Fix and reintegrate RollJam Pro app
- [ ] Expand and refine Subaru, Kia, PSA, and other manufacturer protocols
- [ ] Improve collaboration workflow to avoid overlapping work

---

## Design Philosophy

Flipper-ARF is built around:

- Clean and accurate protocol implementation
- Controlled rolling counter handling
- Automotive-specific encoder accuracy
- Minimal UI changes
- Stability over feature bloat

All modifications remain compatible with the **Flipper Zero open-source API structure** where possible.

---

## Research Direction

Future development focuses on:

- Rolling code synchronization behavior
- Manufacturer-specific signal modulation quirks
- Frame validation differences between OEM and aftermarket systems
- Encoder stability and replay consistency

---

## Contribution Policy

Contributions are welcome if they:

- Improve protocol stability
- Add automotive protocol support
- Improve parsing/encoding reliability
- Maintain structural and modular cleanliness

> Non-automotive features are considered out-of-scope for now.

### This code is a mess!
![Talk is cheap, submit patches](arf_pictures/send_patches.jpeg) 
---

# Disclaimer

This project is provided solely for **educational, research, and interoperability purposes**.  

**Flipper-ARF** is an independently developed firmware platform for studying and experimenting with automotive and embedded systems. It supports academic research, security experimentation, and exploration of vehicle technologies in a responsible and ethical manner.  

This project **may incorporate, adapt, or build upon code from other open-source projects**, following their respective licenses. Proper credit is given where necessary, and all included code remains under its original license terms.  

References to other **publicly available firmware, technical materials, or research resources** are included **only for academic comparison, compatibility testing, and responsible security research**. This project **does not include proprietary, confidential, or closed-source code** from third parties.  

The maintainers and contributors of this project **do not support, promote, or enable any illegal activity**, including unauthorized access to devices, vehicles, systems, infrastructure, or other property.  

By accessing, using, modifying, compiling, or distributing this software, you agree that:

- You are fully responsible for your use of this code.  
- You will follow all relevant local, national, and international laws.  
- You will use this software **only for lawful, ethical, and research purposes**.  
- You will not use this software to harm others, breach security, or compromise property without explicit permission.  

The authors, maintainers, and contributors **bear no responsibility or liability** for any misuse, damages, legal consequences, or violations that result from the use, modification, or distribution of this code.  

THIS SOFTWARE IS PROVIDED **"AS IS,"** WITHOUT ANY WARRANTIES OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.  

IN NO EVENT SHALL THE AUTHORS, COPYRIGHT HOLDERS, OR CONTRIBUTORS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION WITH THE SOFTWARE OR ITS USE.  

**ALL RISKS FROM THE USE OR PERFORMANCE OF THIS SOFTWARE REMAIN WITH THE USER.**
