#pragma once

#include <furi_hal.h>
#include <lib/subghz/devices/preset.h>

// Small helper mapping the modulation presets Chief Cooker exposes in Settings
// to their FuriHalSubGhzPreset value and a short display name.
//
// NOTE: the restaurant pager protocols (Princeton / SMC5326) are OOK/AM, so
// "AM650" is the only preset that decodes pagers. The other presets are useful
// only if the receiver is used as a generic signal scanner.
class ModulationManager {
public:
    struct Entry {
        FuriHalSubGhzPreset preset;
        const char* name;
    };

    static const Entry* GetEntries() {
        static const Entry entries[] = {
            {FuriHalSubGhzPresetOok650Async, "AM650"},
            {FuriHalSubGhzPresetOok270Async, "AM270"},
            {FuriHalSubGhzPreset2FSKDev238Async, "FM238"},
            {FuriHalSubGhzPreset2FSKDev476Async, "FM476"},
        };
        return entries;
    }

    static uint8_t GetCount() {
        return 4;
    }

    static FuriHalSubGhzPreset GetPreset(uint8_t index) {
        if(index >= GetCount()) {
            index = 0;
        }
        return GetEntries()[index].preset;
    }

    static const char* GetName(uint8_t index) {
        if(index >= GetCount()) {
            index = 0;
        }
        return GetEntries()[index].name;
    }

    static uint8_t GetIndex(FuriHalSubGhzPreset preset) {
        for(uint8_t i = 0; i < GetCount(); i++) {
            if(GetEntries()[i].preset == preset) {
                return i;
            }
        }
        return 0;
    }
};
