@echo off
REM Flipper Zero Firmware Flashing Script for Windows
REM This script will flash your Flipper Zero with the custom firmware

setlocal enabledelayedexpansion

echo.
echo ╔═══════════════════════════════════════════════════════════╗
echo ║  FLIPPER ZERO FIRMWARE FLASHER - WINDOWS                  ║
echo ╚═══════════════════════════════════════════════════════════╝
echo.

REM Check if firmware file exists
if not exist "FIRMWARE_BUILT_BY_YOU.dfu" (
    echo ERROR: FIRMWARE_BUILT_BY_YOU.dfu not found!
    echo.
    echo Make sure the firmware file is in the same directory as this script.
    echo.
    pause
    exit /b 1
)

echo ✓ Firmware file found: FIRMWARE_BUILT_BY_YOU.dfu
echo.
echo IMPORTANT: Follow these steps carefully
echo.
echo 1. Connect your Flipper Zero to USB
echo 2. Put Flipper in DFU mode:
echo    - Hold LEFT + BACK buttons
echo    - Wait until screen shows "DFU" mode
echo 3. Press any key to start flashing...
echo.
pause

REM Check if dfu-util is installed
dfu-util --version >nul 2>&1
if errorlevel 1 (
    echo.
    echo ERROR: dfu-util not found!
    echo.
    echo Please install dfu-util first:
    echo 1. Download from: http://dfu-util.sourceforge.net/
    echo 2. Extract to a folder in your PATH
    echo Or use: https://devtools.flipper.net/ (easier web method)
    echo.
    pause
    exit /b 1
)

echo.
echo Flashing firmware... DO NOT DISCONNECT!
echo.

dfu-util -a 0 -D FIRMWARE_BUILT_BY_YOU.dfu

if errorlevel 1 (
    echo.
    echo ERROR: Flashing failed!
    echo.
    echo Troubleshooting:
    echo - Make sure Flipper is in DFU mode (LEFT + BACK)
    echo - Try a different USB port
    echo - Try a different USB cable
    echo - Restart and try again
    echo.
    pause
    exit /b 1
)

echo.
echo ✓ FLASHING COMPLETE!
echo.
echo Your Flipper is now rebooting with the new firmware.
echo.
pause
