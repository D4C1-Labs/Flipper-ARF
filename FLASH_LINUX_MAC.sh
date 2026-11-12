#!/bin/bash

# Flipper Zero Firmware Flashing Script for Linux/macOS
# This script will flash your Flipper Zero with the custom firmware

echo ""
echo "╔═══════════════════════════════════════════════════════════╗"
echo "║  FLIPPER ZERO FIRMWARE FLASHER - LINUX/macOS              ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

# Check if firmware file exists
if [ ! -f "FIRMWARE_BUILT_BY_YOU.dfu" ]; then
    echo "ERROR: FIRMWARE_BUILT_BY_YOU.dfu not found!"
    echo ""
    echo "Make sure the firmware file is in the same directory as this script."
    echo ""
    exit 1
fi

echo "✓ Firmware file found: FIRMWARE_BUILT_BY_YOU.dfu"
echo ""

# Check if dfu-util is installed
if ! command -v dfu-util &> /dev/null; then
    echo "ERROR: dfu-util not found!"
    echo ""
    echo "Install it with:"
    echo "  Ubuntu/Debian: sudo apt-get install dfu-util"
    echo "  macOS: brew install dfu-util"
    echo ""
    echo "Or use the web flasher at: https://devtools.flipper.net/"
    echo ""
    exit 1
fi

echo "✓ dfu-util is installed"
echo ""

echo "IMPORTANT: Follow these steps carefully"
echo ""
echo "1. Connect your Flipper Zero to USB"
echo "2. Put Flipper in DFU mode:"
echo "   - Hold LEFT + BACK buttons"
echo "   - Wait until screen shows 'DFU' mode"
echo "3. Press ENTER to start flashing..."
echo ""
read -p "Press ENTER to continue..." 

echo ""
echo "Flashing firmware... DO NOT DISCONNECT!"
echo ""

sudo dfu-util -a 0 -D FIRMWARE_BUILT_BY_YOU.dfu

if [ $? -ne 0 ]; then
    echo ""
    echo "ERROR: Flashing failed!"
    echo ""
    echo "Troubleshooting:"
    echo "- Make sure Flipper is in DFU mode (LEFT + BACK)"
    echo "- Try a different USB port"
    echo "- Try a different USB cable"
    echo "- Run: dfu-util -l (to see devices)"
    echo ""
    exit 1
fi

echo ""
echo "✓ FLASHING COMPLETE!"
echo ""
echo "Your Flipper is now rebooting with the new firmware."
echo ""
