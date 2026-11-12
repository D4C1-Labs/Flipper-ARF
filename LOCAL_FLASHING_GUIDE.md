# Local Flashing Guide - Run on Your Machine

Now that you have Copilot running locally, you can use these automated flashing scripts!

## 📦 Files Available

- **FIRMWARE_BUILT_BY_YOU.dfu** - Your firmware (860 KB)
- **FLASH_WINDOWS.bat** - Windows batch script (easiest for Windows)
- **FLASH_WINDOWS.ps1** - Windows PowerShell script (modern, colored output)
- **FLASH_LINUX_MAC.sh** - Linux/macOS bash script

---

## 🚀 How to Flash

### Windows (Batch Script - Easiest)

1. Download/copy these files to your machine:
   - `FIRMWARE_BUILT_BY_YOU.dfu`
   - `FLASH_WINDOWS.bat`

2. **Install dfu-util** (if you don't have it):
   - Download from: http://dfu-util.sourceforge.net/
   - Extract to a folder in your PATH

3. **Double-click FLASH_WINDOWS.bat**
4. Follow the on-screen instructions
5. Put your Flipper in DFU mode when prompted (LEFT + BACK)
6. Wait for flashing to complete ✅

### Windows (PowerShell - Modern)

1. Copy files as above

2. Open PowerShell and run:
   ```powershell
   powershell -ExecutionPolicy Bypass -File FLASH_WINDOWS.ps1
   ```

3. Follow the on-screen instructions

### macOS / Linux

1. Copy files to your machine:
   - `FIRMWARE_BUILT_BY_YOU.dfu`
   - `FLASH_LINUX_MAC.sh`

2. **Install dfu-util**:
   ```bash
   # macOS
   brew install dfu-util
   
   # Ubuntu/Debian
   sudo apt-get install dfu-util
   ```

3. **Run the script**:
   ```bash
   chmod +x FLASH_LINUX_MAC.sh
   ./FLASH_LINUX_MAC.sh
   ```

4. Follow the on-screen instructions

---

## ⚠️ Important: DFU Mode

Before flashing, put your Flipper in **DFU Mode**:

1. **Connect** Flipper to USB
2. **Hold LEFT + BACK** buttons on Flipper
3. **Wait** until screen shows "DFU" or "DFU mode"
4. **Release** buttons
5. **Start** the flashing script

---

## 🆘 Troubleshooting

### "dfu-util not found"
- Install dfu-util (see commands above)
- Or use web flasher: https://devtools.flipper.net/

### "Device not found"
- Make sure Flipper is in DFU mode (LEFT + BACK)
- Try different USB port
- Try different USB cable
- Check: `dfu-util -l` (lists connected DFU devices)

### "Permission denied" (Linux/macOS)
- Run with sudo: `sudo ./FLASH_LINUX_MAC.sh`
- Or add user to dialout group

### Flashing failed halfway
- Don't panic! Original firmware still works
- Try again with the script
- If repeated: use web flasher instead

---

## 📋 Process Summary

1. ✅ Download firmware & script for your OS
2. ✅ Install dfu-util if needed
3. ✅ Connect Flipper to USB
4. ✅ Put Flipper in DFU mode (LEFT + BACK)
5. ✅ Run the flashing script
6. ✅ Wait 2-3 minutes
7. ✅ Done! Flipper reboots automatically

---

## 🎯 What's Inside Your Firmware

- 122+ SubGhZ protocols (NICE, CAME, Sommer, etc.)
- Flux Capacitor RF amplifier support
- All standard Flipper Zero features
- Ready to use!

---

**Enjoy your custom Flipper firmware!** 🚀
