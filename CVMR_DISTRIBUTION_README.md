# CVMR-ARF Enhanced Firmware with Flux Capacitor

## What's Included

This package contains:

1. **Firmware DFU File**: `flipper-z-f7-cvmr-lab.dfu`
   - Full Flipper Zero firmware with 128+ protocols
   - Native Flux Capacitor RF amplifier support
   - Ready to flash immediately

2. **Documentation**: Complete flashing and technical guides
   - FLASHING_GUIDE.md - Step-by-step flashing instructions
   - BUILD_INFO.txt - Build verification and details
   - README_CVMR.md - Original CVMr-ARF project README

3. **Protocol Files**: `protocols/` directory
   - 131+ automotive protocol implementations from CVMr-ARF
   - Includes: Keeloq, PSA, BMW, Ford, GM, Nissan, Toyota, Mercedes, Audi, and more
   - Source code reference for each protocol

4. **SD Card Resources**: `sd-resources/` directory
   - Assets and presets for Flipper device
   - Icons and configuration files
   - Copy to SD card root if needed

5. **Build Scripts**: `build-scripts/` directory
   - FBT build tools (reference only)
   - Build configuration

## Quick Start

### Method 1: Web Flashing (Easiest)
1. Visit https://lab.flipper.net
2. Select "Flipper Zero" → "Custom"
3. Upload `flipper-z-f7-cvmr-lab.dfu`
4. Follow prompts to flash

### Method 2: CLI Flashing (Linux/Mac)
```bash
dfu-util -d 0483:df11 -a 0 -D flipper-z-f7-cvmr-lab.dfu
```

### Method 3: qFlipper (Desktop App)
1. Download qFlipper from https://flipperzero.one
2. Open qFlipper
3. Connect Flipper in DFU mode (LEFT+BACK, power on)
4. Go to "Update" → "Reinstall Firmware"
5. Select `flipper-z-f7-cvmr-lab.dfu`

## Features

✅ **Flux Capacitor RF Amplifier Support**
- Native driver for Lab401 Flux Capacitor device
- GPIO-based hardware control
- Extended transmission range

✅ **131+ SubGHz Protocols**
- Automotive OEM protocols
- Garage door openers
- Car keyfobs
- Rolling code systems
- Keeloq variants
- PSA/Citroën encryption
- And many more

✅ **Verified Build**
- No compilation errors
- All symbols resolved
- DFU file tested and ready
- Full Flipper compatibility

##Verification After Flashing

After flashing, you should see:
1. Flux Capacitor in Sub-GHz → Transmit menu
2. All 128+ standard protocols available
3. Firmware version reflects the build date

If Flux Capacitor doesn't appear, try reflashing or check the flashing guide.

## Need Help?

See FLASHING_GUIDE.md for troubleshooting and detailed instructions for your platform.

## Technical Details

- **Base**: Official Flipper Zero firmware
- **Additions**: CVMr-ARF protocols + Flux Capacitor driver
- **Compatibility**: Flipper Zero F7 (F7 only, not compatible with F6)
- **DFU File Size**: 788 KB
- **Protocols**: 128+ SubGHz protocols

## Legal Notice

This firmware is for authorized research, personal use, and security testing only. Ensure compliance with local regulations regarding RF transmission and vehicular access.

---

**Build Date**: 2026-11-12
**Package Version**: CVMR-Lab Unified
**Flux Capacitor Support**: Active
