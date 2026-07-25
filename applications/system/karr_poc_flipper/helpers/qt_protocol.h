#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define QT_MARKER 0xAA
#define QT_MAX_DATA_LEN 64
#define QT_MAX_FRAME_LEN (QT_MAX_DATA_LEN + 5)

typedef enum {
    QtCmd_LockDoors = 0x0B,
    QtCmd_UnlockDoors = 0x0C,
    QtCmd_SilentLock = 0x25,
    QtCmd_SilentUnlock = 0x26,

    QtCmd_AuthInit = 0x22,
    QtCmd_AuthChallenge = 0x0E,
    QtCmd_AuthResponse = 0x0F,
    QtCmd_AuthSuccess = 0x10,
} QtCommand;

size_t qt_protocol_generate(
    QtCommand command,
    const uint8_t* data,
    size_t data_len,
    uint8_t* out_buf,
    size_t out_buf_size);

bool qt_protocol_parse(
    const uint8_t* uart,
    size_t uart_len,
    size_t offset,
    uint8_t* command_out,
    uint8_t* data_out,
    size_t data_out_max,
    size_t* data_len_out);
