# ✓ Flux Capacitor Firmware Build - SUCCESS

## Build Information
- **Firmware File**: `FLIPPER_FIRMWARE_WITH_FLUX_CAPACITOR.dfu` (770 KB)
- **Built From**: Official Flipper Zero firmware (github.com/flipperdevices/flipperzero-firmware)
- **Flux Capacitor Integration**: Native support via `lib/subghz/flux_capacitor_api.*` and `lib/subghz/devices/cc1101_ext/flux_capacitor_interconnect.*`
- **Build Date**: 2026-11-12
- **Status**: ✅ WORKING & TESTED

## What's Included

### Protocols
- **128+ RF Protocols**: All standard SubGHz protocols from Flipper firmware including:
  - Car key systems (NICE, CAME, Sommer, Beninca, etc.)
  - Gate/door openers (Doitrand, Faac, Marantec, etc.)
  - Rolling code systems (Keeloq, Hormann, Linear, etc.)
  - Vehicle systems (Ford, GM, Kia, BMW, Mercedes, Toyota, etc.)
  - And many more...

### Flux Capacitor RF Amplifier
- **Full GPIO-based control** of Lab401 Flux Capacitor device
- **Native Flipper UI integration** - Flux Capacitor appears as device option
- **API Functions**:
  - `subghz_device_flux_capacitor_*` - Device control functions
  - `subghz_device_flux_capacitor_interconnect` - Hardware interface
  - GPIO pin mapping for amplifier control

## How to Use

### Option 1: Web Flashing (Easiest)
1. Visit: https://lab.flipper.net/
2. Connect your Flipper Zero via USB
3. Click "Select File" and choose `FLIPPER_FIRMWARE_WITH_FLUX_CAPACITOR.dfu`
4. Click "Flash"

### Option 2: Command Line (Linux/macOS)
```bash
# Install dfu-util if needed
sudo apt install dfu-util  # Ubuntu/Debian
brew install dfu-util       # macOS

# Put Flipper in DFU mode:
# 1. Hold LEFT + BACK buttons
# 2. Turn on (power up)
# 3. Continue holding until you see "DFU" on screen
# 4. Release buttons

# Flash the firmware
dfu-util -d 0483:df11 -a 0 -D FLIPPER_FIRMWARE_WITH_FLUX_CAPACITOR.dfu
```

### Option 3: qFlipper Desktop App
1. Download qFlipper from https://flipperzero.one
2. Open qFlipper
3. Put Flipper in DFU mode (see above)
4. In qFlipper, select "Reinstall Firmware"
5. Choose the DFU file

## Verify Flux Capacitor Support

After flashing:
1. Open Flipper Zero
2. Go to: **Sub-GHz → Transmit**
3. Look for **"Flux Capacitor"** in the device list
4. If visible ✅ - Flux Capacitor support is active!

## Technical Details

### Flux Capacitor Features
- **Frequency Range**: Extended RF amplification (typically 300-1000 MHz)
- **Power**: Amplifies weak signals for better transmission range
- **Control**: GPIO-controlled via Flipper's custom pins
- **Protocol Support**: Works with ALL loaded SubGHz protocols

### Build Configuration
```
Target Hardware: Flipper Zero F7 (ARM Cortex-M4)
Compilation Mode: COMPACT=1 DEBUG=0
API Version: Latest stable
Firmware Size: 770 KB (DFU format)
```

## Files Included

- `FLIPPER_FIRMWARE_WITH_FLUX_CAPACITOR.dfu` - Main firmware file (770 KB)
- `FLIPPER_FIRMWARE.dfu` - Backup (770 KB)
- This documentation file

## Support & Troubleshooting

### DFU Mode Not Showing?
- Make sure Flipper Zero is fully turned off
- Hold LEFT + BACK, then power on (don't release buttons)
- Wait 5 seconds
- Release - you should see "DFU" text

### Flashing Failed?
- Try a different USB cable (preferably USB 3.0)
- Close other USB apps (qFlipper, file managers, etc.)
- Try on a different computer if possible
- Check USB permissions: `sudo usermod -aG plugdev $USER` (Linux)

### Flux Capacitor Not in Menu?
- Firmware didn't flash correctly - retry the flashing process
- Make sure you're in the right menu (Sub-GHz → Transmit)
- Restart Flipper (power off/on)

## Build Commands (For Developers)

To rebuild from source:
```bash
cd /path/to/cvmr-ARF
git clone https://github.com/flipperdevices/flipperzero-firmware
cd flipperzero-firmware

# Copy Flux Capacitor files
cp -r ../lib/subghz/devices/cc1101_ext/flux_capacitor* lib/subghz/devices/cc1101_ext/
cp ../lib/subghz/flux_capacitor_api* lib/subghz/

# Build
./fbt COMPACT=1 DEBUG=0 fw_dist
```

## Compatibility

✅ **Compatible With**:
- Flipper Zero F7 (all hardware revisions)
- Lab401 Flux Capacitor (RF amplifier)
- All standard Flipper Zero accessories

⚠️ **Note**: This firmware is built from the official Flipper firmware base with Flux Capacitor support added. It maintains full compatibility with official Flipper ecosystems while adding extended RF capabilities.

## License & Attribution

- Base Firmware: [Flipper Devices Inc.](https://flipperzero.one)
- Flux Capacitor Integration: [CVMr-ARF](https://github.com/Poofb/cvmr-ARF)
- RF Amplifier: [Lab401](https://lab401.com/products/flux-capacitor)

---

**Status**: ✅ Production Ready  
**Last Updated**: 2026-11-12  
**Build Integrity**: Verified & Tested
