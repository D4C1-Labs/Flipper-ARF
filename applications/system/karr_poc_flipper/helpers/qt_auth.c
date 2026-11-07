#include "qt_auth.h"
#include <string.h>

static const uint8_t DEALER_KEY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t CUSTOMER_KEY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

bool qt_generate_hash(
    QtMode mode,
    const uint8_t challenge[QT_CHALLENGE_LEN],
    const char* unit_serial,
    uint8_t out[QT_HASH_OUT_LEN]) {
    uint8_t key[16];

    if(mode == QtMode_User || mode == QtMode_Valet) {
        if(unit_serial == NULL || strlen(unit_serial) < 10) {
            return false;
        }
        memcpy(key, CUSTOMER_KEY, 16);
        key[3] = (uint8_t)unit_serial[7];
        key[6] = (uint8_t)unit_serial[8];
        key[9] = (uint8_t)unit_serial[9];
    } else {
        memcpy(key, DEALER_KEY, 16);
    }

    for(int i = 0; i < 16; i++) {
        uint32_t acc = key[i];
        for(int j = 0; j < QT_CHALLENGE_LEN; j++) {
            acc = (uint32_t)(acc * 0x21u + challenge[j]);
        }
        out[2 * i] = (uint8_t)(acc & 0xFF);
        out[2 * i + 1] = (uint8_t)((acc >> 8) & 0xFF);
    }

    return true;
}
