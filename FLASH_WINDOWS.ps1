# Flipper Zero Firmware Flashing Script for Windows (PowerShell)
# Run with: powershell -ExecutionPolicy Bypass -File FLASH_WINDOWS.ps1

Write-Host ""
Write-Host "╔═══════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║  FLIPPER ZERO FIRMWARE FLASHER - WINDOWS (PowerShell)     ║" -ForegroundColor Cyan
Write-Host "╚═══════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# Check if firmware file exists
if (-not (Test-Path "FIRMWARE_BUILT_BY_YOU.dfu")) {
    Write-Host "ERROR: FIRMWARE_BUILT_BY_YOU.dfu not found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Make sure the firmware file is in the same directory as this script." -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press ENTER to exit"
    exit 1
}

Write-Host "✓ Firmware file found: FIRMWARE_BUILT_BY_YOU.dfu" -ForegroundColor Green
Write-Host ""

# Check if dfu-util is in PATH
$dfuUtil = Get-Command dfu-util -ErrorAction SilentlyContinue
if (-not $dfuUtil) {
    Write-Host "ERROR: dfu-util not found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Install dfu-util from: http://dfu-util.sourceforge.net/" -ForegroundColor Yellow
    Write-Host "Or use the web flasher: https://devtools.flipper.net/" -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press ENTER to exit"
    exit 1
}

Write-Host "✓ dfu-util is installed" -ForegroundColor Green
Write-Host ""

Write-Host "IMPORTANT: Follow these steps carefully" -ForegroundColor Yellow
Write-Host ""
Write-Host "1. Connect your Flipper Zero to USB"
Write-Host "2. Put Flipper in DFU mode:"
Write-Host "   - Hold LEFT + BACK buttons"
Write-Host "   - Wait until screen shows 'DFU' mode"
Write-Host "3. Press ENTER to start flashing..."
Write-Host ""
Read-Host "Press ENTER to continue"

Write-Host ""
Write-Host "Flashing firmware... DO NOT DISCONNECT!" -ForegroundColor Yellow
Write-Host ""

dfu-util -a 0 -D FIRMWARE_BUILT_BY_YOU.dfu

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "ERROR: Flashing failed!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Troubleshooting:" -ForegroundColor Yellow
    Write-Host "- Make sure Flipper is in DFU mode (LEFT + BACK)"
    Write-Host "- Try a different USB port"
    Write-Host "- Try a different USB cable"
    Write-Host ""
    Read-Host "Press ENTER to exit"
    exit 1
}

Write-Host ""
Write-Host "✓ FLASHING COMPLETE!" -ForegroundColor Green
Write-Host ""
Write-Host "Your Flipper is now rebooting with the new firmware." -ForegroundColor Green
Write-Host ""
Read-Host "Press ENTER to exit"
