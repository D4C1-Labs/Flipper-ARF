# Flux Capacitor Quick Start Guide

## For Users

### Installation (5 minutes)

1. **Physical Setup**
   ```
   1. Power OFF Flipper Zero
   2. Align Flux Capacitor with expansion port
   3. Press firmly until fully seated
   4. Attach SMA antenna (433 MHz recommended)
   5. Power ON Flipper Zero
   6. Wait 2-3 seconds for detection
   ```

2. **Verification**
   - Open any SubGhZ application
   - Should see "Flux Capacitor" in device list
   - Status shows device and power level

### Basic Usage

```
Open App → Select Device → Choose "Flux Capacitor" → Start using!
```

**Default settings (automatic):**
- Mode: Auto-detect (use if present)
- Power: Medium (50 mW)
- Antenna: Tuned to 433 MHz

### Range Comparison

| Mode | Flipper Standard | Flux Capacitor |
|:---|:---:|:---:|
| Short Range | 10-30m | Same |
| Medium Range | 30-50m | 70-100m |
| Long Range | N/A | 100-150m |

### Safety Reminder

⚠️ **ALWAYS attach antenna before transmitting!**  
Operating without antenna = **permanent RF module damage**

---

## For Developers

### Integration (Copy-Paste Ready)

```c
#include "lib/subghz/flux_capacitor_api.h"
#include "lib/subghz/devices/devices.h"

// One-time initialization
flux_capacitor_init();

// Get device
const SubGhzDevice* device = 
    subghz_devices_get_by_name(flux_capacitor_get_device_name());

// Use like any SubGhZ device
subghz_devices_set_frequency(device, 433920000);
subghz_devices_set_tx(device);
// ... transmission code ...
```

### API Reference (Most Common)

```c
// Check if present
bool is_present = flux_capacitor_is_present();

// Set mode (Internal, External, Auto)
flux_capacitor_set_mode(FluxCapacitorModeAuto);

// Set power (Low, Medium, High, VeryHigh)
flux_capacitor_set_power(FluxCapacitorPowerHigh);

// Get device name
const char* name = flux_capacitor_get_device_name();
// Returns: "Flux Capacitor" or "CC1101 Internal"

// Get UI status
char status[64];
flux_capacitor_get_status_string(status, sizeof(status));
// Output: "Flux Capacitor @ 100mW"
```

### Build

```bash
# Already enabled by default!
./fbt COMPACT=1 DEBUG=0 updater_package
./fbt COMPACT=1 DEBUG=0 flash_usb_full
```

### File Locations

**Driver Code:**
- `/lib/subghz/devices/cc1101_ext/flux_capacitor_interconnect.h` - Interface
- `/lib/subghz/devices/cc1101_ext/flux_capacitor_interconnect.c` - Implementation

**High-Level API:**
- `/lib/subghz/flux_capacitor_api.h` - Public API
- `/lib/subghz/flux_capacitor_api.c` - Implementation

**Documentation:**
- `FLUX_CAPACITOR.md` - Complete guide (13,500+ lines)
- `FLUX_CAPACITOR_IMPLEMENTATION.md` - Technical summary
- `README.md` - Quick overview section

### Common Tasks

#### Select Flux Capacitor for all SubGhZ operations
```c
flux_capacitor_set_mode(FluxCapacitorModeExternal);
```

#### Maximize range for long-distance testing
```c
flux_capacitor_set_power(FluxCapacitorPowerHigh);  // 100 mW
```

#### Use only internal radio (Flux not required)
```c
flux_capacitor_set_mode(FluxCapacitorModeInternal);
```

#### Add device selection to your app
```c
const char* device = flux_capacitor_get_device_name();
SubGhzDevice* device_ptr = 
    subghz_devices_get_by_name(device);
// Now use device_ptr for all operations
```

---

## Specifications

### Hardware
- **RF Chip:** CC1101 (external, amplified)
- **TX Power:** 10-120 mW (configurable)
- **RX Sensitivity:** ~-105 dBm
- **Frequency Bands:** 315 MHz, 433 MHz, 868 MHz
- **Antenna:** SMA tuned external

### Performance
- **TX Range @ 100mW:** 100-150m+ (line-of-sight)
- **Power Draw @ 100mW TX:** ~600 mA
- **Battery Life @ 50mW:** 2-3 hours continuous TX
- **Modulations:** OOK, FSK, GFSK, MSK (all supported)

### Compatibility
- ✅ All Flipper-ARF protocols
- ✅ All automotive protocols (100+ variants)
- ✅ All gate/access protocols (50+ variants)
- ✅ Custom protocols
- ✅ Research tools

---

## Troubleshooting

| Problem | Solution |
|:---|:---|
| **Device not detected** | Reseat firmly, check antenna, power off/on |
| **Short range** | Use correct frequency antenna, increase power level |
| **No transmission** | Check antenna attached, verify device selection |
| **Crashes on TX** | Check power level, ensure antenna present |
| **RF module damaged** | Must have operated without antenna |

**Full troubleshooting:** See `FLUX_CAPACITOR.md` Troubleshooting section

---

## Documentation

### Quick Reads (5-10 min)
- This file (Quick Start)
- `README.md` - Flux Capacitor section

### Comprehensive (30-60 min)
- `FLUX_CAPACITOR.md` - Complete guide with examples
- `FLUX_CAPACITOR_IMPLEMENTATION.md` - Technical details

### References
- `lib/subghz/flux_capacitor_api.h` - API documentation
- Protocol implementations in `/lib/subghz/protocols/`

---

## Support

1. **Check documentation** → Most answers in `FLUX_CAPACITOR.md`
2. **Review examples** → Look at existing SubGhZ apps
3. **Enable logging** → Set `enable_logging = true`
4. **Report issues** → Open GitHub issue with logs

---

## Next: Deep Dive

Ready for more? Read the full documentation:

```bash
# In your favorite text editor
less FLUX_CAPACITOR.md
less FLUX_CAPACITOR_IMPLEMENTATION.md
```

---

**Last Updated:** November 2026  
**For:** Flipper-ARF with Flux Capacitor Support  
**Duration:** ~5 minutes to get started  

