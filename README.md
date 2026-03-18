# Flipper-ARF: Automotive Research Firmware

## Overview

**Flipper-ARF** is an independently developed firmware fork for Flipper Zero, based on **Unleashed Firmware** but heavily modified. Unlike standard firmware builds, it removes typical general-purpose functions and focuses exclusively on **automotive research and experimentation**.

The goal of this firmware is to provide a **high-compatibility, protocol-focused build** for personal use, academic study, and responsible security research. It focuses on **automotive remote protocols, rolling code behavior, and keyfob ecosystem compatibility**.

This project may incorporate, adapt, or build upon **other open-source projects** in accordance with their licenses, with proper attribution.

---

## Table of Contents

- [Showcase](#showcase)
- [Supported Systems](#supported-systems)
- [How to Build](#how-to-build)
- [Project Scope](#project-scope)
- [To Do / Planned Features](#to-do--planned-features)
- [Design Philosophy](#design-philosophy)
- [Research Direction](#research-direction)
- [Contribution Policy](#contribution-policy)
- [Citations & References](#citations--references)
- [Disclaimer](#disclaimer)

---

## Showcase

| | |
|:---:|:---:|
| ![Home](.arf_pictures/home.png) | ![Sub-GHz Scanner](.arf_pictures/subghz_scan.png) |
| Home Screen | Sub-GHz Scanner |
| ![Keeloq Key Manager](.arf_pictures/keeloq_key_manager.png) | ![Mod Hopping](.arf_pictures/mod_hopping.png) |
| Keeloq Key Manager | Mod Hopping Config |
| ![PSA Decrypt](.arf_pictures/psa_decrypt_builtin.png) | ![Counter BruteForce](.arf_pictures/counter_bruteforce.png) |
| PSA XTEA Decrypt | Counter BruteForce |

---

## Supported Systems

### Automotive Protocols

| Manufacturer | Protocol | Frequency | Modulation | Encoder | Decoder | CRC |
|:---|:---|:---:|:---:|:---:|:---:|:---:|
| VAG (VW/Audi/Skoda/Seat) | VAG GROUP | 433 MHz | AM | Yes | Yes | No |
| Porsche | Porsche AG | 433/868 MHz | AM | Yes | Yes | No |
| PSA (Peugeot/Citroën/DS) | PSA GROUP | 433 MHz | AM/FM | Yes | Yes | Yes |
| Ford | Ford V0 | 315/433 MHz | AM | Yes | Yes | Yes |
| Fiat | Fiat SpA | 433 MHz | AM | Yes | Yes | Yes |
| Fiat | Marelli/Delphi | 433 MHz | AM | No | Yes | No |
| Mazda | Siemens (5WK49365D) | 315/433 MHz | AM/FM | Yes | Yes | Yes |
| Kia/Hyundai | KIA/HYU V0 | 433 MHz | FM | Yes | Yes | Yes |
| Kia/Hyundai | KIA/HYU V1 | 315/433 MHz | AM | Yes | Yes | Yes |
| Kia/Hyundai | KIA/HYU V2 | 315/433 MHz | AM/FM | Yes | Yes | Yes |
| Kia/Hyundai | KIA/HYU V3/V4 | 315/433 MHz | AM/FM | Yes | Yes | Yes |
| Kia/Hyundai | KIA/HYU V5 | 433 MHz | FM | Yes | Yes | Yes |
| Kia/Hyundai | KIA/HYU V6 | 433 MHz | FM | Yes | Yes | Yes |
| Subaru | Subaru | 433 MHz | AM | Yes | Yes | No |
| Suzuki | Suzuki | 433 MHz | FM | Yes | Yes | Yes |
| Mitsubishi | Mitsubishi V0 | 868 MHz | FM | Yes | Yes | No |

### Gate / Access Protocols

| Protocol | Frequency | Modulation | Encoder | Decoder | CRC |
|:---|:---:|:---:|:---:|:---:|:---:|
| Keeloq | 433/868/315 MHz | AM | Yes | Yes | No |
| Nice FLO | 433 MHz | AM | Yes | Yes | No |
| Nice FloR-S | 433 MHz | AM | Yes | Yes | Yes |
| CAME | 433/315 MHz | AM | Yes | Yes | No |
| CAME TWEE | 433 MHz | AM | Yes | Yes | No |
| CAME Atomo | 433 MHz | AM | Yes | Yes | No |
| Faac SLH | 433/868 MHz | AM | Yes | Yes | No |
| Holtek | 433 MHz | AM | Yes | Yes | No |
| Holtek-Ht12x | 433 MHz | AM | Yes | Yes | No |
| Somfy Telis | 433 MHz | AM | Yes | Yes | Yes |
| Somfy Keytis | 433 MHz | AM | Yes | Yes | Yes |
| Alutech AT-4N | 433 MHz | AM | Yes | Yes | Yes |
| Keyfinder | 433 MHz | AM | Yes | Yes | No |
| KingGates Stylo4k | 433 MHz | AM | Yes | Yes | No |
| Beninca ARC | 433 MHz | AM | Yes | Yes | No |
| Hormann HSM | 433/868 MHz | AM | Yes | Yes | No |
| Marantec | 433 MHz | AM | Yes | Yes | Yes |
| Marantec24 | 433 MHz | AM | Yes | Yes | Yes |

### General Protocols

| Protocol | Frequency | Modulation | Encoder | Decoder | CRC |
|:---|:---:|:---:|:---:|:---:|:---:|
| Princeton | 433/315 MHz | AM | Yes | Yes | No |
| Linear | 315 MHz | AM | Yes | Yes | No |
| LinearDelta3 | 315 MHz | AM | Yes | Yes | No |
| GateTX | 433 MHz | AM | Yes | Yes | No |
| Security+ 1.0 | 315 MHz | AM | Yes | Yes | No |
| Security+ 2.0 | 315 MHz | AM | Yes | Yes | No |
| Chamberlain Code | 315 MHz | AM | Yes | Yes | No |
| MegaCode | 315 MHz | AM | Yes | Yes | No |
| Mastercode | 433 MHz | AM | Yes | Yes | No |
| Dickert MAHS | 433 MHz | AM | Yes | Yes | No |
| SMC5326 | 433 MHz | AM | Yes | Yes | No |
| Phoenix V2 | 433 MHz | AM | Yes | Yes | No |
| Doitrand | 433 MHz | AM | Yes | Yes | No |
| Hay21 | 433 MHz | AM | Yes | Yes | No |
| Revers RB2 | 433 MHz | AM | Yes | Yes | No |
| Roger | 433 MHz | AM | Yes | Yes | No |

---

### How to Build

Compact release build:

To build:
```
./fbt COMPACT=1 DEBUG=0 updater_package
```
To flash:
```
./fbt COMPACT=1 DEBUG=0 flash_usb_full
```

---

## Project Scope

Flipper-ARF aims to achieve:

- Maximum compatibility with automotive Sub-GHz rolling and static protocols
- Accurate OEM-style remote emulation
- Stable encoder/decoder implementations
- Modular protocol expansion

**Primary focus:** Automotives/Alarm's keyfob protocols, keeloq, and keyless systems.

> ⚠ This is a protocol-focused research firmware, not a general-purpose firmware.

---

## To Do / Planned Features

- [ ] Marelli BSI encoder and encryption
- [ ] Improve RollJam app 
- [ ] Expand and refine as many manufacturer protocols as possible

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

## Citations & References

The following academic publications have been invaluable to the development and understanding of the protocols implemented in this firmware.

### Automotive RKE Security

- **Lock It and Still Lose It — On the (In)Security of Automotive Remote Keyless Entry Systems**
  Flavio D. Garcia, David Oswald, Timo Kasper, Pierre Pavlidès
  *USENIX Security 2016, pp. 929–944*
  DOI: [10.5555/3241094.3241166](https://doi.org/10.5555/3241094.3241166)
  https://www.usenix.org/system/files/conference/usenixsecurity16/sec16_paper_garcia.pdf

- **Clonable Key Fobs: Analyzing and Breaking RKE Protocols**
  Roberto Gesteira-Miñarro, Gregorio López, Rafael Palacios
  *International Journal of Information Security, Springer, May 2025, 24(3)*
  DOI: [10.1007/s10207-025-01063-7](https://doi.org/10.1007/s10207-025-01063-7)

- **The Role of Cryptographic Techniques in Remote Keyless Entry (RKE) Systems**
  Jananga Chiran — Sri Lanka Institute of Information Technology
  *November 2023*
  DOI: [10.5281/zenodo.14677864](https://doi.org/10.5281/zenodo.14677864)

- **SoK: Stealing Cars Since Remote Keyless Entry Introduction and How to Defend From It**
  Tommaso Bianchi, Alessandro Brighente, Mauro Conti, Edoardo Pavan — University of Padova / Delft University of Technology
  *arXiv, 2025*
  https://arxiv.org/pdf/2505.02713

- **Security of Automotive Systems**
  Lennert Wouters, Benedikt Gierlichs, Bart Preneel
  *Wiley, February 2025*
  DOI: [10.1002/9781394351930.ch11](https://doi.org/10.1002/9781394351930.ch11)

### DST Cipher Family (DST40 / DST80)

- **Security Analysis of a Cryptographically-Enabled RFID Device**
  Steve Bono, Matthew Green, Adam Stubblefield, Ari Juels, Avi Rubin, Michael Szydlo
  *14th USENIX Security Symposium (USENIX Security '05)*
  https://www.usenix.org/conference/14th-usenix-security-symposium/security-analysis-cryptographically-enabled-rfid-device
  https://www.usenix.org/legacy/event/sec05/tech/bono/bono.pdf

- **Dismantling DST80-based Immobiliser Systems**
  Lennert Wouters, Jan Van den Herrewegen, Flavio D. Garcia, David Oswald, Benedikt Gierlichs, Bart Preneel
  *IACR Transactions on Cryptographic Hardware and Embedded Systems (TCHES), 2020, Vol. 2020(2), pp. 99–127*
  DOI: [10.13154/tches.v2020.i2.99-127](https://doi.org/10.13154/tches.v2020.i2.99-127)

### KeeLoq Cryptanalysis

- **Cryptanalysis of the KeeLoq Block Cipher**
  Andrey Bogdanov
  *Cryptology ePrint Archive, Paper 2007/055; also presented at RFIDSec 2007*
  https://eprint.iacr.org/2007/055

- **A Practical Attack on KeeLoq**
  Sebastiaan Indesteege, Nathan Keller, Orr Dunkelman, Eli Biham, Bart Preneel
  *EUROCRYPT 2008 (LNCS vol. 4965, pp. 1–18)*
  DOI: [10.1007/978-3-540-78967-3_1](https://doi.org/10.1007/978-3-540-78967-3_1)
  https://www.iacr.org/archive/eurocrypt2008/49650001/49650001.pdf

- **Algebraic and Slide Attacks on KeeLoq**
  Nicolas T. Courtois, Gregory V. Bard, David Wagner
  *FSE 2008 (LNCS vol. 5086, pp. 97–115)*
  DOI: [10.1007/978-3-540-71039-4_6](https://doi.org/10.1007/978-3-540-71039-4_6)

- **On the Power of Power Analysis in the Real World: A Complete Break of the KeeLoq Code Hopping Scheme**
  Thomas Eisenbarth, Timo Kasper, Amir Moradi, Christof Paar, Mahmoud Salmasizadeh, Mohammad T. Manzuri Shalmani
  *CRYPTO 2008 (LNCS vol. 5157, pp. 203–220)*
  DOI: [10.1007/978-3-540-85174-5_12](https://doi.org/10.1007/978-3-540-85174-5_12)
  https://www.iacr.org/archive/crypto2008/51570204/51570204.pdf

- **Breaking KeeLoq in a Flash: On Extracting Keys at Lightning Speed**
  Markus Kasper, Timo Kasper, Amir Moradi, Christof Paar
  *AFRICACRYPT 2009 (LNCS vol. 5580, pp. 403–420)*
  DOI: [10.1007/978-3-642-02384-2_25](https://doi.org/10.1007/978-3-642-02384-2_25)

### Immobiliser & Transponder Cipher Attacks

- **Gone in 360 Seconds: Hijacking with Hitag2**
  Roel Verdult, Flavio D. Garcia, Josep Balasch
  *21st USENIX Security Symposium (USENIX Security '12), pp. 237–252*
  DOI: [10.5555/2362793.2362830](https://doi.org/10.5555/2362793.2362830)
  https://www.usenix.org/system/files/conference/usenixsecurity12/sec12-final95.pdf

- **Dismantling Megamos Crypto: Wirelessly Lockpicking a Vehicle Immobilizer**
  Roel Verdult, Flavio D. Garcia, Baris Ege
  *Supplement to 22nd USENIX Security Symposium (USENIX Security '13/15), pp. 703–718*
  https://www.usenix.org/sites/default/files/sec15_supplement.pdf

- **Dismantling the AUT64 Automotive Cipher**
  Christopher Hicks, Flavio D. Garcia, David Oswald
  *IACR Transactions on Cryptographic Hardware and Embedded Systems (TCHES), 2018, Vol. 2018(2), pp. 46–69*
  DOI: [10.13154/tches.v2018.i2.46-69](https://doi.org/10.13154/tches.v2018.i2.46-69)

### RFID & Protocol Analysis Tooling

- **A Toolbox for RFID Protocol Analysis**
  Flavio D. Garcia
  *IEEE International Conference on RFID, 2012*
  DOI: [10.1109/rfid.2012.19](https://doi.org/10.1109/rfid.2012.19)

### Relay & Replay Attacks

- **Relay Attacks on Passive Keyless Entry and Start Systems in Modern Cars**
  Aurélien Francillon, Boris Danev, Srdjan Čapkun
  *NDSS 2011*
  https://www.ndss-symposium.org/ndss2011/relay-attacks-on-passive-keyless-entry-and-start-systems-in-modern-cars/

- **Implementing and Testing RollJam on Software-Defined Radios**
  *Università di Bologna (UNIBO), CRIS*
  https://cris.unibo.it/handle/11585/999874

- **Enhanced Vehicular Roll-Jam Attack Using a Known Noise Source**
  *Inaugural International Symposium on Vehicle Security & Privacy, January 2023*
  DOI: [10.14722/vehiclesec.2023.23037](https://doi.org/10.14722/vehiclesec.2023.23037)

- **RollBack: A New Time-Agnostic Replay Attack Against the Automotive Remote Keyless Entry Systems**
  Levente Csikor, Hoon Wei Lim, Jun Wen Wong, Soundarya Ramesh, Rohini Poolat Parameswarath, Mun Choon Chan
  *Black Hat USA 2022; ACM Transactions on Cyber-Physical Systems, 2024*
  DOI: [10.1145/3627827](https://doi.org/10.1145/3627827)
  https://i.blackhat.com/USA-22/Thursday/US-22-Csikor-Rollback-A-New-Time-Agnostic-Replay-wp.pdf

- **Rolling-PWN Attack (Honda RKE Vulnerability)**
  Kevin2600 (Haoqi Shan), Wesley Li — Star-V Lab
  *Independent disclosure, 2022 (CVE-2021-46145)*
  https://rollingpwn.github.io/rolling-pwn/

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
