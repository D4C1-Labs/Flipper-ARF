#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    QtMode_Installer = 0,
    QtMode_Dealer = 1,
    QtMode_User = 2,
    QtMode_NoSale = 3,
    QtMode_BCA = 4,
    QtMode_Valet = 5,
    QtMode_Bootload = 6,
    QtMode_Unconfigured = 7,
} QtMode;

#define QT_HASH_OUT_LEN 32
#define QT_CHALLENGE_LEN 8

bool qt_generate_hash(
    QtMode mode,
    const uint8_t challenge[QT_CHALLENGE_LEN],
    const char* unit_serial,
    uint8_t out[QT_HASH_OUT_LEN]);
