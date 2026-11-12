# Quick Start Guide

## Download & Flash Firmware

### Step 1: Get the Firmware
- Download: `doctor-firmware-integrated-flux-capacitor.tgz` (1.2 MB)
- Extract the `.tgz` file to access firmware files

### Step 2: Choose Your Flashing Method

#### Method A: qFlipper (Recommended - Easiest)
1. Install [qFlipper](https://flips.flipperdevices.com/)
2. Connect Flipper Zero via USB
3. Open qFlipper → Install → Choose `firmware.dfu`
4. Click "Start" and wait for completion
5. Flipper will reboot automatically

#### Method B: Official Flipper Web Updater
1. Go to [Flipper Web Updater](https://update.flipperzero.one/)
2. Connect Flipper via USB
3. Click "Select Firmware"
4. Choose `firmware.bin`
5. Click "Flash"

#### Method C: SD Card Update
1. Copy `firmware.bin` to Flipper's SD card
2. Place in folder: `/updates/`
3. On Flipper: Settings → System → Firmware Update
4. Select the firmware file
5. Follow on-screen instructions

### Step 3: Verify Installation
- Firmware should boot successfully
- Check SubGhz app loads without errors
- Verify settings show the correct firmware version

---

## Using the Integrated Features

### Car Protocols (New)
Navigate to: SubGhz → Saved/Raw → Vehicles (if available)

Supported manufacturers:
- Audi / VW Group
- Chrysler / Dodge / Jeep
- General Motors
- Honda / Acura
- Mercedes-Benz
- Nissan / Infiniti
- Toyota / Lexus

### Flux Capacitor RF Amplifier
1. **Connection**: Connect Flux Capacitor to Flipper's expansion port
2. **Navigation**: SubGhz → Settings → Device
3. **Selection**: Choose "Flux Capacitor" or "CC1101 External"
4. **Usage**: Use any protocol for TX with RF amplification

### TX (Transmit) Operations
1. Select a protocol
2. Load/create a signal
3. Tap "Transmit" button
4. Device will send RF signal
5. With Flux Capacitor: Signal will be amplified for extended range

---

## Troubleshooting

### Firmware Won't Flash
- Try different USB cable
- Update computer drivers
- Use qFlipper v0.10.0 or newer
- Check firmware format (.dfu vs .bin)

### Protocols Not Showing
- Firmware needs to finish booting
- Restart the SubGhz app
- Reboot Flipper if needed
- Check SD card has enough space

### Flux Capacitor Not Detected
- Verify physical connection
- Check expansion port pins
- Reboot Flipper
- Try re-seating the device

### TX Not Working
- Verify legal authorization in your area
- Check frequency is supported
- Ensure device has power
- Try in a controlled test first

---

## Safety & Legal

⚠️ **IMPORTANT**: 
- Check local regulations before using TX features
- Unauthorized transmission may be illegal
- RF signals can interfere with systems
- Always use in controlled, authorized environments
- Flux Capacitor requires proper setup

---

## File Descriptions

- **firmware.dfu** - Recommended format (use with qFlipper)
- **firmware.bin** - Alternative binary format
- **firmware.elf** - Debug symbols (for developers)
- **firmware.hex** - Intel hex format (for programmers)
- **firmware.lst** - Disassembly listing (for analysis)

---

## Support & Resources

- **Flipper Zero**: https://flipperzero.one/
- **Flux Capacitor**: https://lab401.com/products/flux-capacitor
- **Community**: https://github.com/DarkFlippers/unleashed-firmware
- **SubGhz Guides**: Check Flipper documentation

---

## Key Statistics

- **Total Protocols**: 101
- **New Automotive**: 7
- **TX Support**: All protocols
- **Flash Used**: 71.19% (1180 bytes free)
- **Build Date**: November 12, 2026

---

## Next Steps

1. ✅ Flash firmware using qFlipper
2. ✅ Verify boot and basic functionality
3. ✅ Connect Flux Capacitor (if available)
4. ✅ Test automotive protocols safely
5. ✅ Explore SubGhz app features

---

**Ready to flash? Start with qFlipper - it's the easiest method!**
