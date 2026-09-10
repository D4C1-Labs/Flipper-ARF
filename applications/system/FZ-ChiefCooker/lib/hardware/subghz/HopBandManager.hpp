#pragma once

#include <cstdint>
#include <cstddef>

#include "lib/hardware/subghz/FrequencyManager.hpp"

// Provides the frequency list used by the automatic hopping scanner.
//
// Two modes:
//   HOP_BAND_PAGERS -> a small curated list of the frequencies restaurant
//                      pagers actually use (fast to sweep, high hit rate).
//   HOP_BAND_ALL    -> every frequency from the firmware setting_user list
//                      (full coverage, slower sweep).
enum HopBand {
    HOP_BAND_PAGERS = 0,
    HOP_BAND_ALL = 1,
};

class HopBandManager {
public:
    // Curated pager frequencies (Hz). These are the common OOK pager bands.
    static const uint32_t* GetPagerFrequencies(size_t* countOut) {
        static const uint32_t pagerFreqs[] = {
            315000000, // 315.00 MHz
            433920000, // 433.92 MHz
            434075000, // 434.07 MHz
            467750000, // 467.75 MHz
        };
        *countOut = sizeof(pagerFreqs) / sizeof(pagerFreqs[0]);
        return pagerFreqs;
    }

    // Fills `out` with the frequency list for the given band and returns the
    // count. `out` must have room for at least GetMaxCount() entries. For
    // HOP_BAND_ALL the list is copied from FrequencyManager (firmware list).
    static size_t FillFrequencies(HopBand band, uint32_t* out, size_t maxCount) {
        if(band == HOP_BAND_PAGERS) {
            size_t count = 0;
            const uint32_t* pagers = GetPagerFrequencies(&count);
            if(count > maxCount) {
                count = maxCount;
            }
            for(size_t i = 0; i < count; i++) {
                out[i] = pagers[i];
            }
            return count;
        }

        // HOP_BAND_ALL: pull the whole firmware frequency list.
        FrequencyManager* fm = FrequencyManager::GetInstance();
        size_t count = fm->GetFrequencyCount();
        if(count > maxCount) {
            count = maxCount;
        }
        for(size_t i = 0; i < count; i++) {
            out[i] = fm->GetFrequency(i);
        }
        return count;
    }

    // Upper bound on the number of hop frequencies (firmware lists are small).
    static size_t GetMaxCount() {
        return 64;
    }

    static const char* GetBandName(HopBand band) {
        switch(band) {
        case HOP_BAND_PAGERS:
            return "Pagers";
        case HOP_BAND_ALL:
            return "All freqs";
        default:
            return "?";
        }
    }

    static uint8_t GetBandCount() {
        return 2;
    }
};
