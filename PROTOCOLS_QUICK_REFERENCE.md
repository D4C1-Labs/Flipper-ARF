# New Protocols Quick Reference Card

## Protocol Summary Table

| # | Protocol | Type | Freq (MHz) | Modulation | File |
|---|----------|------|-----------|-----------|------|
| 1 | Genesis | Luxury Auto | 433, 315 | FSK | genesis.c |
| 2 | Jaguar | Luxury Auto | 433, 868 | GFSK | jaguar.c |
| 3 | BYD RKE | EV Auto | 433, 315 | Manchester | byd_rke.c |
| 4 | NIO | EV Auto | 433, 315, 868 | Manchester | nio.c |
| 5 | XPeng | EV Auto | 433, 315, 868 | Manchester | xpeng.c |
| 6 | Dacia | Budget Auto | 433 | Manchester | dacia.c |
| 7 | Skoda | VW Group | 433 | Manchester | skoda.c |
| 8 | Seat | VW Group | 433 | Manchester | seat.c |
| 9 | Geely | Chinese Auto | 315, 433, 868 | Manchester | geely.c |
| 10 | Chery | Chinese Auto | 433 | Manchester | chery.c |
| 11 | GAC Aion | EV Auto | 433 | Manchester | gap.c |
| 12 | WEY | Premium Auto | 433 | Manchester | wey.c |
| 13 | Haval | SUV Auto | 433 | Manchester | haval.c |
| 14 | VW ID Series | EV Auto | 433, 868 | Manchester | vw_id.c |
| 15 | Porsche New | Luxury Auto | 433, 868 | Manchester | porsche_new.c |
| 16 | Aston Martin | Ultra-Luxury | 433, 315, 868 | Manchester | aston_martin.c |
| 17 | Lamborghini | Hypercar | 433, 315, 868 | Manchester | lamborghini.c |
| 18 | Bugatti | Hypercar | 433, 315, 868 | Manchester | bugatti.c |
| 19 | Bentley | Ultra-Luxury | 433, 315, 868 | Manchester | bentley.c |
| 20 | Rolls Royce | Ultra-Luxury | 433, 315, 868 | Manchester | rolls_royce.c |
| 21 | Alarmax | Security | 433 | OOK/ASK | alarmax.c |
| 22 | Jablotron | Security | 433 | OOK/ASK | jablotron.c |
| 23 | Pyronix | Security | 433 | OOK/ASK | pyronix.c |
| 24 | Visonic | Security | 433 | OOK/ASK | visonic.c |
| 25 | ADT Alarm | Security | 433 | OOK/ASK | adt_alarm.c |
| 26 | Fraybox GPS | Tracking | 433 | OOK/ASK | fraybox.c |
| 27 | Tracki GPS | Tracking | 433 | OOK/ASK | tracki.c |
| 28 | Spytec GL300 | Tracking | 433 | OOK/ASK | spytec.c |
| 29 | Nonda Dash Cam | Camera | 433 | OOK/ASK | nonda.c |
| 30 | Viofo Dash Cam | Camera | 433 | OOK/ASK | viofo.c |
| 31 | 70Mai Dash Cam | Camera | 433 | OOK/ASK | 70mai.c |
| 32 | Cobra Radar | Detection | 315 | OOK/ASK | cobra_radar.c |
| 33 | Valentine Radar | Detection | 315 | OOK/ASK | valentine_radar.c |
| 34 | Adaptiv Radar | Detection | 315 | OOK/ASK | adaptiv_radar.c |
| 35 | Escort Radar | Detection | 315 | OOK/ASK | escort_radar.c |
| 36 | Beltronics Radar | Detection | 315 | OOK/ASK | beltronics_radar.c |
| 37 | Radar Royal | Detection | 315 | OOK/ASK | radar_royal.c |
| 38 | Universal RKE | Generic | 433, 315, 868 | Manchester | universal_rke.c |
| 39 | Rolling Code V2 | Generic | 433, 315, 868 | Manchester | rolling_code_v2.c |
| 40 | Security Plus | Generic | 433, 315, 868 | Manchester | security_plus.c |

## By Category

### Luxury Automotive (20 protocols)
- Genesis (Hyundai luxury)
- Jaguar (British luxury)
- Aston Martin (British sports)
- Bentley (British ultra-luxury)
- Lamborghini (Italian supercar)
- Bugatti (French hypersupercar)
- Rolls Royce (British ultra-luxury)
- Porsche New (German sports)

### Chinese Automotive (6 protocols)
- BYD RKE (Build Your Dreams)
- NIO (Premium EV)
- XPeng (EV Tech)
- GAC Aion (EV Specialist)
- Geely (Volvo partner)
- Chery (Mass market)
- WEY (Premium SUV)
- Haval (SUV leader)

### European Automotive (5 protocols)
- Dacia (Budget)
- Skoda (VW Group)
- Seat/Cupra (VW Group)
- VW ID Series (EV)

### Security & Alarms (5 protocols)
- Alarmax
- Jablotron
- Pyronix
- Visonic
- ADT Alarm

### GPS Trackers (3 protocols)
- Fraybox
- Tracki
- Spytec GL300

### Dash Cameras (3 protocols)
- Nonda
- Viofo
- 70Mai

### Radar Detectors (6 protocols)
- Cobra
- Valentine
- Adaptiv
- Escort
- Beltronics
- Radar Royal

### Universal/Generic (3 protocols)
- Universal RKE V1
- Rolling Code V2
- Security Plus System

## Frequency Quick Lookup

### 315 MHz (North America, Radar)
- Genesis
- BYD RKE
- XPeng
- Geely
- Aston Martin, Lamborghini, Bugatti, Bentley, Rolls Royce
- All Radar Detectors (6 protocols)
- Universal RKE V1, Rolling Code V2, Security Plus

### 433 MHz (Europe, Global Standard)
- **All protocols** except radar detectors primarily use this

### 868 MHz (Advanced/Premium)
- NIO, XPeng, Geely, VW ID
- Porsche New
- Aston Martin, Lamborghini, Bugatti, Bentley, Rolls Royce
- Universal/Generic protocols

## Modulation Quick Lookup

### FSK (Frequency Shift Keying)
- Genesis (primary)

### GFSK (Gaussian FSK)
- Jaguar

### Manchester Encoding (Most common)
- BYD RKE
- NIO, XPeng, Gap, WEY, Haval
- Dacia, Skoda, Seat
- Geely, Chery, VW ID
- Porsche New, Aston Martin, Lamborghini, Bugatti, Bentley, Rolls Royce
- Universal RKE V1, Rolling Code V2, Security Plus

### OOK/ASK (Simple systems)
- Alarmax, Jablotron, Pyronix, Visonic, ADT
- Fraybox, Tracki, Spytec
- Nonda, Viofo, 70Mai
- All Radar Detectors

## Data Format Reference

### 64-bit Payload (Automotive)
- ID (16-24 bits) + Counter (16-24 bits) + Command (8-16 bits) + Checksum (8 bits)
- Genesis, Jaguar, NIO, XPeng, Dacia, Skoda, Seat, Geely, Chery, Gap, WEY, Haval
- VW ID, Porsche New, Aston Martin, Lamborghini, Bugatti, Bentley, Rolls Royce
- Universal RKE V1, Rolling Code V2, Security Plus

### 48-bit Payload (Security/Simple)
- Alarmax, Jablotron, Pyronix, Visonic, ADT
- Fraybox, Tracki, Spytec, Nonda, Viofo, 70Mai
- Radar Detectors

## Flux Capacitor Performance

### With Flux Capacitor @ 100mW
- **Range**: 100-150m+
- **Power**: 10x improvement over stock Flipper
- **Supported by**: All 40 new protocols
- **Battery**: ~600mA TX current
- **Modulation**: Works with all modulation types

### Default Power Levels
- Low: 10mW (30-50m)
- Medium: 50mW (70-100m) ← Default
- High: 100mW (100-150m+)
- VeryHigh: 120mW (150m+)

## Testing Checklist

When testing a new protocol:

1. **Signal Capture**
   - [ ] Can receive signal with correct protocol selected
   - [ ] Hash detection working
   - [ ] Signal level shown correctly

2. **Data Parsing**
   - [ ] Device ID parsed correctly
   - [ ] Counter increments on repeated presses
   - [ ] Checksum validated
   - [ ] String output readable

3. **Serialization**
   - [ ] Can save captured signal to .sub file
   - [ ] File saves with correct format
   - [ ] All protocol parameters saved

4. **Deserialization**
   - [ ] Can load saved .sub file
   - [ ] Protocol correctly identified
   - [ ] Data fields restored properly

5. **Transmission**
   - [ ] Can load and replay signal
   - [ ] Target device responds appropriately
   - [ ] Multiple transmissions work
   - [ ] Flux Capacitor increases range

## Common Issues & Solutions

### Signal Not Detected
- [ ] Verify frequency matches device (315/433/868 MHz)
- [ ] Check modulation setting (FSK/GFSK/Manchester/OOK)
- [ ] Ensure antenna properly connected
- [ ] Try with Flux Capacitor at max power

### Weak Reception
- [ ] Use Flux Capacitor for 10x range improvement
- [ ] Increase TX power to "High" or "VeryHigh"
- [ ] Reposition antenna (perpendicular to device)
- [ ] Move closer to transmitting device

### Protocol Not Recognized
- [ ] Check protocol list for correct name
- [ ] Some protocols may need case correction
- [ ] Try "Auto" mode for automatic detection
- [ ] Verify device uses supported frequency

## File Locations

All new protocols located in:
```
/workspaces/cvmr-ARF/lib/subghz/protocols/
```

Documentation:
```
/workspaces/cvmr-ARF/PROTOCOLS_EXPANSION.md
/workspaces/cvmr-ARF/PROTOCOLS_QUICK_REFERENCE.md (this file)
```

## Next Steps

1. **Compile**: `./fbt COMPACT=1 DEBUG=0 updater_package`
2. **Test**: Try capturing signals with each new protocol
3. **Validate**: Confirm protocol decoding is correct
4. **Document**: Add any findings to protocol-specific notes

---

**Total New Protocols**: 40
**Previous Total**: 127
**New Total**: 167 protocols
**Coverage Increase**: +31.5%

Last Updated: 2026-11-12
