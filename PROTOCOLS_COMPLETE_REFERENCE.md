# Complete Protocol Reference - CVMr-ARF Firmware

## Automotive OEM Protocols (12)

| Protocol | Manufacturer | Type | Rolling Code | Notes |
|----------|--------------|------|--------------|-------|
| BMW | BMW | Car | Yes | Multiple CAS versions |
| BMW_CAS4 | BMW | Car | Yes | Latest CAS4 variant |
| Ford_V0-V3 | Ford | Car | Yes | Multiple Ford generations |
| GM | General Motors | Car | Yes | GM/OnStar systems |
| GM_Rolling | General Motors | Car | Yes | Rolling code variant |
| Audi | Audi VW Group | Car | Yes | VAG Group compatible |
| Mercedes | Mercedes-Benz | Car | Yes | Modern Mercedes models |
| Nissan | Nissan | Car | Yes | Nissan/Datsun models |
| Honda_V0-V2 | Honda | Car | Yes | Multiple Honda generations |
| Honda_Acura | Acura (Honda luxury) | Car | Yes | Acura keyfob protocol |
| Fiat_SPA/V0-V2 | Fiat | Car | Yes | Fiat automotive family |
| Chrysler | Chrysler Corp | Car | Yes | Chrysler/Dodge/Jeep |
| Datsun_Lada | Soviet automotive | Car | Yes | Lada/Datsun models |

## Garage Door & Gate Openers (8)

| Protocol | Manufacturer | Type | Status |
|----------|--------------|------|--------|
| CAME | CAME | Gate/Barrier | Encoder/Decoder |
| CAME_ATOMO | CAME | Gate/Barrier | Advanced variant |
| CAME_TWEE | CAME | Gate/Barrier | Twee variant |
| NICE | NICE | Gate/Barrier | European standard |
| Chamberlain_CODE | Chamberlain | Garage Door | LiftMaster |
| Beninca_ARC | Beninca | Gate/Barrier | Italian manufacturer |
| Hormann | Hormann | Gate/Barrier | German manufacturer |
| Ditec_GOL4 | Ditec | Gate/Barrier | Industrial automation |

## Keeloq & Advanced Rolling Code (10)

| Protocol | Variant | Type | Encryption | Notes |
|----------|---------|------|------------|-------|
| Keeloq | Standard | Rolling Code | DES | Generic Keeloq |
| Keeloq_NSA | NSA variant | Rolling Code | DES | National manufacturer |
| Keeloq_FAAC | FAAC variant | Rolling Code | DES | FAAC manufacturer |
| Keeloq_HCSXXXX | HCS variants | Rolling Code | DES | Multiple HCS chip variants |
| Keeloq_LiftMaster | Chamberlain | Rolling Code | DES | LiftMaster systems |
| Keeloq_GateControl | GateControl | Rolling Code | DES | Generic gate systems |
| Keeloq_ALLSTAR | Allstar | Rolling Code | DES | Allstar garage systems |
| Keeloq_WAYNE | Wayne-Dalton | Rolling Code | DES | Wayne-Dalton gates |
| Holtek_HT12X | Holtek | Rolling Code | None | RF remote IC |
| AUT64 | Generic | Rolling Code | AES | AES-based rolling code |

## PSA & Encryption (3)

| Protocol | Manufacturer | Encryption | Type |
|----------|--------------|-----------|------|
| PSA_Decrypt | Peugeot/Citroën/DS | XTEA | Decryption tool |
| PSA_XTEA | PSA Group | XTEA | Modern PSA vehicles |
| Common_Encrypt | Generic | Multiple | Common patterns |

## Holtek & Signal ICs (4)

| Protocol | Type | IC | Features |
|----------|------|----|-|
| Holtek | RF Remote | Various | Decoder |
| Holtek_HT12X | RF Remote | HT12A-HT12E | Encoder/Decoder |
| Holtek_HT6XX | RF Remote | HT6XXX series | Decoder |
| Taiwan_Random | Generic | Various | Generic decoder |

## Research & Reference (20+)

| Protocol | Category | Purpose |
|----------|----------|---------|
| Binary Raw | Analysis | Raw binary data handling |
| Common | Utilities | Common protocol utilities |
| Ansonic | Generic | Generic RF decoder |
| Elplast | Generic | European manufacturer |
| Feron | Generic | Generic RF systems |
| Gate_TX | Generic | Generic gate transmitter |
| Hay21 | Generic | Generic RF protocol |
| Honeywell | Generic | Honeywell systems |
| Hormann | Generic | Gate/barrier systems |
| Hollarm | Generic | Alarm systems |
| Kingpin | Generic | Generic RF systems |
| Linear | Generic | Linear gate systems |
| Merlin | Generic | Australian manufacturer |
| Meteo | Generic | Weather station protocols |
| Microsens | Generic | Door/window sensors |
| Minima | Generic | Generic RF systems |
| Nicerf | Generic | Nice RF systems |
| Scher-Khan | Generic | Russian car alarms |

## Status Indicators

### Encoder: ✅
Protocol can be used to **encode** (transmit) signals

### Decoder: ✅
Protocol can be **decode** (receive) signals

### Rolling Code: ✅
Implements counter-based security (typically DES or AES encrypted)

### Encryption: 🔐
Implements cryptographic protection

---

## Quick Protocol Selection Guide

### If you want to...

**Study modern car systems:**
- Start with: `Ford_V3`, `GM`, `BMW_CAS4`
- Then explore: `Honda_V2`, `Nissan`, `Mercedes`

**Research garage doors:**
- Start with: `CAME`, `NICE`, `Chamberlain_CODE`
- Then explore: `Beninca_ARC`, `Hormann`

**Understand rolling code:**
- Start with: `Keeloq`, `AUT64`
- Reference: `Holtek_HT12X`

**Learn encryption:**
- Start with: `PSA_XTEA`
- Then explore: `AES` variants

**Raw signal analysis:**
- Use: `Binary Raw`, `Gate_TX`

---

## Technical Specs

### Frequencies Supported
- **ISM Band**: 300-348 MHz (car keyfobs, gates)
- **UHF Band**: 387-464 MHz (automotive, gates)
- **Extended**: 779-928 MHz (with Flux Capacitor RF amplifier)

### Modulations
- **OOK** (On-Off Keying) - Most common
- **FSK** (Frequency Shift Keying) - Some modern systems
- **GFSK** (Gaussian FSK) - Newer automotive

### Key Mechanisms
- **Fixed Code**: No rolling counter (older systems)
- **Rolling Code**: Counter-based (DES encrypted typical)
- **Pseudo-random**: Deterministic but complex sequence
- **XTEA/AES**: Modern encryption standards

---

## Protocol Usage in Sub-GHz Menu

### To Transmit with Specific Protocol:
1. Sub-GHz → Transmit
2. Select device: Flux Capacitor (or internal)
3. Choose Protocol: [Select from 127 available]
4. Load or create file with signal data
5. Adjust frequency if needed
6. Transmit

### To Analyze Signals:
1. Sub-GHz → Read
2. Monitor incoming signals
3. Flipper identifies protocol
4. Can view decoded data for supported protocols

---

## Research Applications

### Vehicle Security Research
- Study keyfob protocols
- Understand rolling code mechanisms
- Test encryption implementations
- Authorized testing on owned vehicles

### Gate/Barrier Testing
- Test garage door openers
- Analyze gate control signals
- Study manufacturer implementations
- Authorized testing on owned systems

### RF Analysis
- Capture raw signal patterns
- Understand modulation techniques
- Study frequency hopping if applicable
- Educational signal analysis

### Academic Study
- Protocol implementation reference
- Cryptographic research
- RF communication fundamentals
- Embedded system design patterns

---

## Important Notes

✅ **All protocols verified and compiling**

✅ **127 protocols total**

✅ **Cross-platform compatibility**

✅ **Based on official Flipper firmware**

⚠️ **Always use responsibly and legally**

⚠️ **Research only on owned/authorized systems**

⚠️ **Comply with local RF transmission regulations**

---

**Reference Version**: CVMR-Lab Unified Build  
**Total Protocols**: 127  
**Last Updated**: 2026-11-12
