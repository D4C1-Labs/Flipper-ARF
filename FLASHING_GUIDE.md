# Complete Firmware Flashing Guide for Flipper-ARF

## Pre-Flashing Checklist

- [ ] Flipper device fully charged
- [ ] USB cable available (preferably good quality USB-C)
- [ ] Computer running Windows, macOS, or Linux
- [ ] Latest firmware package ready (from `dist/` folder)

## Step 1: Complete the Build (If Not Done)

```bash
cd /workspaces/cvmr-ARF

# Verify git is clean
git status

# Build the complete firmware with all protocols
./fbt COMPACT=1 DEBUG=0 updater_package

# This will create firmware in dist/ folder
```

**Expected output**: Files created in `dist/` folder:
- `f7-firmware-updater_*.zip`
- `f7-firmware-full_*.dfu`
- `f7-firmware-*.elf`

## Step 2: Verify Build Outputs

```bash
# List the generated files
ls -lh dist/

# Check file integrity
file dist/*.dfu dist/*.zip

# Expected results:
# - .dfu file: ~400-500 KB
# - .zip file: ~300-400 KB
# - .elf file: ~600-700 KB
```

## Step 3: Prepare Your Flipper Device

### Using Official Flipper Zero Device Manager (RECOMMENDED)

**Option A: Web-Based (No Installation)**

1. Open browser: https://devtools.flipper.net/
2. Connect Flipper via USB to computer
3. Click "Connect"
4. Select "Install Firmware" or "Update"
5. Choose `.dfu` file from `dist/` folder
6. Follow on-screen prompts
7. Wait for completion (~2-3 minutes)

**Option B: Downloaded Desktop App**

1. Download from: https://flipper.xfr.dev/ or https://flipperzero.one/
2. Launch application
3. Connect Flipper via USB
4. Authorize device if prompted
5. Click "Install Firmware"
6. Select `.dfu` or `.zip` file from `dist/`
7. Wait for completion

### Using qFlipper (Official Flipper Tool)

1. Download qFlipper: https://flipperzero.one/download
2. Install and launch qFlipper
3. Connect Flipper via USB
4. Click "Install firmware" button
5. Select "Choose file" and navigate to `dist/`
6. Select `.dfu` file
7. Click "Install"
8. Wait for completion message
9. Flipper will restart automatically

### Using DFU-Util (Advanced/Linux)

```bash
# Install dfu-util (if not present)
# Ubuntu/Debian:
sudo apt install dfu-util

# macOS:
brew install dfu-util

# Windows: Download from http://dfu-util.sourceforge.net/

# Put Flipper in DFU mode:
# 1. Connect Flipper
# 2. On Flipper, hold DOWN + RIGHT buttons while inserting USB
# 3. Release buttons when screen appears

# Flash the firmware
dfu-util -D dist/f7-firmware-full_*.dfu

# Alternatively, from within repository:
cd /workspaces/cvmr-ARF
./fbt flash
```

## Step 4: Verify Successful Flash

### On-Device Verification

1. Flipper will restart automatically after flashing
2. Wait for bootup (usually 10-30 seconds)
3. You should see the Flipper logo and main menu
4. If stuck on Flipper logo, try:
   - Removing and reinserting battery
   - Holding power button 10 seconds then turning on
   - If problem persists, reflash with SD card reset (see below)

### SubGhZ Protocol Test

1. On Flipper main menu, open "Applications"
2. Go to "RF Analysis" or "SubGhZ"
3. Select "RX" (receive)
4. Look for protocol list
5. Should see 127+ protocols available
6. Try selecting "Audi" or other automotive protocol
7. Choose frequency (usually 433 MHz)
8. Press "RX" to receive signals

### Flux Capacitor Test

If you have a Flux Capacitor RF amplifier:

1. Connect Flux Capacitor to Flipper external pins
2. In SubGhZ, select a protocol
3. Go to "TX Power" or "TX Settings"
4. Set power to "High" (100 mW) or "VeryHigh" (120 mW)
5. Transmission range should be 10x normal
6. Test with device at 150m+ distance

## Step 5: Configure SD Card (Optional but Recommended)

### Prepare SD Card Content

```bash
# If you have SD card with Flipper content:

1. Insert microSD card into computer
2. Copy any custom RF signals to:
   - /subghz/ folder (for RF captures)
   - /nfc/ folder (for NFC captures)

3. Reinsert SD card into Flipper
4. On Flipper, go to Settings > Storage > SD Card
5. Confirm it's detected and shows available space
```

### SD Card Layout

Recommended structure on your microSD:
```
/subghz/
  ├── captures/         # Your captured RF signals
  ├── signals/          # Organized signal library
  └── presets/          # Custom frequency presets

/nfc/                   # NFC tag captures
/infrared/              # Infrared signal captures
/apps/                  # External FAP applications
/firmware/              # Firmware update packages
```

## Step 6: Post-Flash Configuration

### First Boot Setup

1. Go to "Settings" on main menu
2. Configure:
   - Time & Date (important for logging)
   - Sound volume
   - Screen brightness
   - Display orientation
3. Check "About" to confirm firmware version/date

### RF Protocol Configuration

1. Open "SubGhZ" application
2. Check Settings:
   - Frequency: Usually 433 MHz for Europe, 315 MHz for US
   - Modulation: Auto-detect usually works
   - Power: Start with Low, increase as needed
3. Create custom presets if needed

## Troubleshooting

### Issue: "Failed to Flash" or "Connection Lost"

**Solution**:
- Restart Flipper (hold power 10 seconds)
- Use different USB port or cable
- Close other USB applications
- Try with Device Manager: https://devtools.flipper.net/

### Issue: Flipper Stuck on Logo After Flash

**Solution 1**: Restart Flipper
```
1. Hold power button 10 seconds to force off
2. Press power to turn on
3. Wait 1 minute for full boot
```

**Solution 2**: Clear SD card and try again
```
1. Remove microSD card from Flipper
2. Restart Flipper
3. If it boots, reinsert SD card
4. Go to Settings > Storage > Check SD Card
```

**Solution 3**: Reflash with clean state
```
bash
dfu-util -D dist/f7-firmware-full_*.dfu
# Wait for completion
# Restart Flipper
```

### Issue: "SDK version mismatch" or Protocol Errors

**Solution**: This means build wasn't fully clean
```bash
cd /workspaces/cvmr-ARF
rm -rf build dist/
./fbt COMPACT=1 DEBUG=0 updater_package
# Reflash with new package
```

### Issue: No Protocols Appearing

**Solution**: Check protocol registry
```bash
# Verify protocol_items.c is correct
grep "subghz_protocol_registry_items" lib/subghz/protocols/protocol_items.c | head -5

# Should show protocol list. If empty, rebuild:
./fbt COMPACT=1 DEBUG=0 lib/subghz
./fbt COMPACT=1 DEBUG=0 updater_package
```

## Verification Checklist

After successful flash and boot:

- [ ] Flipper boots without errors
- [ ] Main menu visible and responsive
- [ ] SubGhZ app opens
- [ ] Protocols list shows 127+ entries
- [ ] Can select different frequencies (315/433/868 MHz)
- [ ] RX mode works (LED blinks on RF signals)
- [ ] TX mode responds (can enable/test transmission)
- [ ] SD card (if present) detected in storage
- [ ] Battery shows correct percentage
- [ ] Time displays correctly

## Next Steps: Using Your Device

### Capturing RF Signals

1. Open SubGhZ app
2. Select "RX" (receive mode)
3. Choose appropriate protocol/frequency
4. Point device at RF source (car keyfob, etc.)
5. Press center button when signal captured
6. Signals auto-save to SD card

### Transmitting RF Signals

1. Load captured .sub file in SubGhZ
2. Go to "TX" mode
3. Adjust power if needed (higher = longer range)
4. Press center button to transmit
5. With Flux Capacitor: 10x greater range

### Creating Custom Presets

1. Open SubGhZ
2. Select "Presets" or "Settings"
3. Create new preset with:
   - Name (e.g., "US_315MHz")
   - Frequency (315/433/868 MHz)
   - Modulation (FSK/GFSK/OOK/etc)
4. Save for future use

## Additional Resources

- **Build Status**: See `BUILD_STATUS_AND_INSTRUCTIONS.md`
- **Protocol Info**: See `PROTOCOLS_EXPANSION.md` and `PROTOCOLS_QUICK_REFERENCE.md`
- **Flux Capacitor**: See `FLUX_CAPACITOR.md` and `FLUX_CAPACITOR_QUICKSTART.md`
- **Official Flipper Docs**: https://docs.flipper.net/
- **GitHub Issues**: https://github.com/Poofb/cvmr-ARF/issues

## Safety & Legal Notes

⚠️ **Important**:
- Only transmit on frequencies where you have authorization
- Do not transmit on cellular, aviation, or emergency frequencies
- Respect local RF regulations in your country
- Do not interfere with other devices
- Use for educational and authorized RF analysis only

---

**Last Updated**: 2026-11-12  
**Firmware Version**: Flipper-ARF with Flux Capacitor Support  
**Protocols**: 127 core + full expandability  
**Support**: See documentation files and issues tracker

Good luck with your Flipper-ARF!
