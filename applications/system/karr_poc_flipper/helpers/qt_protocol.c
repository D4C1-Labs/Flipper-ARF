#include "qt_protocol.h"
#include <string.h>

size_t qt_protocol_generate(
    QtCommand command,
    const uint8_t* data,
    size_t data_len,
    uint8_t* out_buf,
    size_t out_buf_size) {
    bool data_is_null = (data == NULL);
    size_t effective_data_len = data_is_null ? 1 : data_len;
    size_t payload_len = effective_data_len + 1;
    size_t frame_len = 5 + effective_data_len;

    if(frame_len > out_buf_size) {
        return 0;
    }

    memset(out_buf, 0, frame_len);

    out_buf[1] = (uint8_t)((payload_len >> 8) & 0xFF);
    out_buf[2] = (uint8_t)(payload_len & 0xFF);
    out_buf[3] = (uint8_t)command;

    if(!data_is_null) {
        memcpy(&out_buf[4], data, data_len);
    }

    uint32_t checksum = 0;
    for(size_t i = 0; i < frame_len; i++) {
        checksum += out_buf[i];
    }
    uint8_t crc = (uint8_t)((~(checksum & 0xFF)) + 1);
    out_buf[frame_len - 1] = crc;

    out_buf[0] = QT_MARKER;

    return frame_len;
}

bool qt_protocol_parse(
    const uint8_t* uart,
    size_t uart_len,
    size_t offset,
    uint8_t* command_out,
    uint8_t* data_out,
    size_t data_out_max,
    size_t* data_len_out) {
    if(offset >= uart_len) return false;
    if(uart[offset] != QT_MARKER) return false;
    if(offset + 3 > uart_len) return false;

    uint16_t length = ((uint16_t)uart[offset + 1] << 8) | uart[offset + 2];

    size_t crc_pos = offset + 3 + length;
    if(crc_pos >= uart_len) return false;
    uint8_t crc_received = uart[crc_pos];

    size_t window_start = offset + 1;
    size_t window_len = (size_t)length + 2;
    if(window_start + window_len > uart_len) return false;

    uint32_t checksum = 0;
    for(size_t i = 0; i < window_len; i++) {
        checksum += uart[window_start + i];
    }
    checksum &= 0xFF;

    if(((crc_received + checksum) & 0xFF) != 0) {
        return false;
    }

    *command_out = uart[offset + 3];

    size_t dlen = (length >= 1) ? (length - 1) : 0;
    if(dlen > data_out_max) return false;

    memcpy(data_out, &uart[offset + 4], dlen);
    *data_len_out = dlen;

    return true;
}
