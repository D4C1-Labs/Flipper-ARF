#include "weather_editor_encoders_ext.h"
#include "weather_editor_tx_variant.h"

#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_encoder.h>
#include <stdint.h>
#include <string.h>

static bool protocol_is_ext(const WeatherEditorState* state, const char* name) {
    return strcmp(furi_string_get_cstr(state->protocol_name), name) == 0;
}

static uint64_t patch_bits_ext(uint64_t data, uint8_t lsb, uint8_t width, uint64_t value) {
    uint64_t value_mask = width == 64 ? UINT64_MAX : ((1ULL << width) - 1ULL);
    uint64_t mask = value_mask << lsb;
    return (data & ~mask) | ((value & value_mask) << lsb);
}

static int32_t wrap_signed_ext(int32_t value, uint8_t bits, bool* wrapped) {
    int32_t min = -(1L << (bits - 1));
    int32_t max = (1L << (bits - 1)) - 1;
    if(value < min || value > max) *wrapped = true;
    uint32_t mask = (1UL << bits) - 1UL;
    uint32_t encoded = ((uint32_t)value) & mask;
    if(encoded & (1UL << (bits - 1))) return (int32_t)(encoded | ~mask);
    return (int32_t)encoded;
}

static uint32_t wrap_unsigned_ext(int32_t value, uint8_t bits, bool* wrapped) {
    uint32_t mask = (1UL << bits) - 1UL;
    if(value < 0 || (uint32_t)value > mask) *wrapped = true;
    return ((uint32_t)value) & mask;
}

static uint32_t wrap_decimal_ext(int32_t value, uint32_t modulus, bool* wrapped) {
    if(value < 0 || (uint32_t)value >= modulus) *wrapped = true;
    int64_t v = value;
    v %= (int64_t)modulus;
    if(v < 0) v += modulus;
    return (uint32_t)v;
}

static bool raw_push_merge(WeatherEditorRaw* raw, int32_t duration) {
    if(duration == 0) return true;
    if(raw->count && ((raw->values[raw->count - 1] > 0) == (duration > 0))) {
        int64_t sum = (int64_t)raw->values[raw->count - 1] + duration;
        if(sum > INT32_MAX) sum = INT32_MAX;
        if(sum < INT32_MIN) sum = INT32_MIN;
        raw->values[raw->count - 1] = (int32_t)sum;
        return true;
    }
    return weather_editor_raw_push(raw, duration);
}

static void raw_pair_ext(WeatherEditorRaw* raw, uint32_t high_us, uint32_t low_us) {
    raw_push_merge(raw, (int32_t)high_us);
    raw_push_merge(raw, -(int32_t)low_us);
}

static void raw_bits_ppm_ext(
    WeatherEditorRaw* raw,
    uint64_t data,
    uint8_t bits,
    uint32_t high_us,
    uint32_t low_zero,
    uint32_t low_one) {
    for(int i = bits - 1; i >= 0; i--) {
        raw_pair_ext(raw, high_us, ((data >> i) & 1ULL) ? low_one : low_zero);
    }
}

static void raw_bits_pwm_ext(
    WeatherEditorRaw* raw,
    uint64_t data,
    uint8_t bits,
    uint32_t high_zero,
    uint32_t low_zero,
    uint32_t high_one,
    uint32_t low_one) {
    for(int i = bits - 1; i >= 0; i--) {
        if((data >> i) & 1ULL)
            raw_pair_ext(raw, high_one, low_one);
        else
            raw_pair_ext(raw, high_zero, low_zero);
    }
}

static uint8_t reverse8_ext(uint8_t value) {
    value = (uint8_t)(((value & 0x55U) << 1) | ((value >> 1) & 0x55U));
    value = (uint8_t)(((value & 0x33U) << 2) | ((value >> 2) & 0x33U));
    value = (uint8_t)((value << 4) | (value >> 4));
    return value;
}

static uint64_t reverse_bits_in_nibbles_ext(uint64_t value, uint8_t bits) {
    uint64_t result = 0;
    uint8_t nibbles = (uint8_t)((bits + 3U) / 4U);
    for(uint8_t i = 0; i < nibbles; i++) {
        uint8_t nibble = (value >> (i * 4U)) & 0x0FU;
        uint8_t reversed = (uint8_t)(((nibble & 1U) << 3) | ((nibble & 2U) << 1) |
                                     ((nibble & 4U) >> 1) | ((nibble & 8U) >> 3));
        result |= ((uint64_t)reversed) << (i * 4U);
    }
    if(bits < 64) result &= (1ULL << bits) - 1ULL;
    return result;
}

static void raw_manchester_result_ext(
    WeatherEditorRaw* raw,
    ManchesterEncoderResult result,
    uint32_t half_us) {
    switch(result) {
    case ManchesterEncoderResultShortLow:
        raw_push_merge(raw, -(int32_t)half_us);
        break;
    case ManchesterEncoderResultLongLow:
        raw_push_merge(raw, -(int32_t)(half_us * 2U));
        break;
    case ManchesterEncoderResultLongHigh:
        raw_push_merge(raw, (int32_t)(half_us * 2U));
        break;
    case ManchesterEncoderResultShortHigh:
        raw_push_merge(raw, (int32_t)half_us);
        break;
    }
}

static void raw_manchester_feed_bit_ext(
    WeatherEditorRaw* raw,
    ManchesterEncoderState* encoder,
    bool bit,
    uint32_t half_us) {
    ManchesterEncoderResult result;
    if(!manchester_encoder_advance(encoder, bit, &result)) {
        raw_manchester_result_ext(raw, result, half_us);
        manchester_encoder_advance(encoder, bit, &result);
    }
    raw_manchester_result_ext(raw, result, half_us);
}

static void raw_manchester_segment_ext(
    WeatherEditorRaw* raw,
    ManchesterEncoderState* encoder,
    uint64_t data,
    uint8_t bits,
    uint32_t half_us,
    bool invert_bits) {
    for(int i = bits - 1; i >= 0; i--) {
        bool bit = ((data >> i) & 1ULL) != 0;
        raw_manchester_feed_bit_ext(raw, encoder, invert_bits ? !bit : bit, half_us);
    }
}

#if WEATHER_EDITOR_TX_VARIANT != WEATHER_EDITOR_TX_VARIANT_SDR_RTL433
static void raw_manchester_oregon2_segment_ext(
    WeatherEditorRaw* raw,
    ManchesterEncoderState* encoder,
    uint64_t data,
    uint8_t bits,
    uint32_t half_us) {
    /* Oregon v2 decoder reconstructs one protocol bit from two Manchester bits.
       The RF level is inverted by the decoder, therefore [bit, !bit] is emitted. */
    for(int i = bits - 1; i >= 0; i--) {
        bool bit = ((data >> i) & 1ULL) != 0;
        raw_manchester_feed_bit_ext(raw, encoder, bit, half_us);
        raw_manchester_feed_bit_ext(raw, encoder, !bit, half_us);
    }
}
#endif

static void raw_manchester_finish_ext(
    WeatherEditorRaw* raw,
    ManchesterEncoderState* encoder,
    uint32_t half_us) {
    raw_manchester_result_ext(raw, manchester_encoder_finish(encoder), half_us);
}

/* Build the exact bit stream expected by rtl_433's
   OOK_PULSE_MANCHESTER_ZEROBIT slicer. A logical 1 has a falling centre edge,
   while a logical 0 has a rising centre edge. Adjacent equal RF halves are
   merged by raw_push_merge(). */
static void raw_oregon_target_bit_ext(
    WeatherEditorRaw* raw,
    bool bit,
    uint32_t half_us) {
    if(bit) {
        raw_push_merge(raw, (int32_t)half_us);
        raw_push_merge(raw, -(int32_t)half_us);
    } else {
        raw_push_merge(raw, -(int32_t)half_us);
        raw_push_merge(raw, (int32_t)half_us);
    }
}

static void raw_oregon_target_segment_ext(
    WeatherEditorRaw* raw,
    uint64_t data,
    uint8_t bits,
    uint32_t half_us) {
    for(int i = bits - 1; i >= 0; i--)
        raw_oregon_target_bit_ext(raw, ((data >> i) & 1ULL) != 0, half_us);
}

static void raw_oregon2_payload_segment_ext(
    WeatherEditorRaw* raw,
    uint64_t data,
    uint8_t bits,
    uint32_t half_us) {
    /* v2 payload is Manchester-decoded a second time by rtl_433. Emit target
       pairs [!bit, bit], so the second-stage decoded bit equals bit. */
    for(int i = bits - 1; i >= 0; i--) {
        const bool bit = ((data >> i) & 1ULL) != 0;
        raw_oregon_target_bit_ext(raw, !bit, half_us);
        raw_oregon_target_bit_ext(raw, bit, half_us);
    }
}

static uint8_t channel_to_acurite_raw(uint8_t channel, bool* wrapped) {
    switch(channel) {
    case 1:
        return 3;
    case 2:
        return 2;
    case 3:
        return 0;
    default:
        *wrapped = true;
        return 3;
    }
}

static uint8_t byte_even_parity(uint8_t value_without_parity) {
    uint8_t value = value_without_parity & 0x7FU;
    return value | (subghz_protocol_blocks_parity8(value) << 7);
}

static uint8_t bcd_pair_from_decimal(int32_t value, bool allow_100, bool* wrapped) {
    if(allow_100 && value == 100) return 0xA0;
    uint32_t v = wrap_decimal_ext(value, 100, wrapped);
    return (uint8_t)(((v / 10U) << 4) | (v % 10U));
}

static void encode_infactory_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint64_t d = state->original_data;
    int32_t f_tenths = weather_editor_temperature_to_fahrenheit_tenths(
        state->temperature_tenths);
    uint32_t temp_raw = wrap_unsigned_ext(f_tenths + 900, 12, &wrapped);
    uint8_t humidity_bcd = bcd_pair_from_decimal(state->humidity, true, &wrapped);

    d = patch_bits_ext(d, 26, 1, state->battery ? 1 : 0);
    d = patch_bits_ext(d, 12, 12, temp_raw);
    d = patch_bits_ext(d, 8, 4, humidity_bcd >> 4);
    d = patch_bits_ext(d, 4, 4, humidity_bcd & 0x0F);
    d = patch_bits_ext(d, 0, 2, wrap_unsigned_ext(state->channel, 2, &wrapped));
    d = patch_bits_ext(d, 28, 4, 0);

    uint8_t msg[] = {
        d >> 32,
        (uint8_t)(((d >> 24) & 0x0F) | ((d & 0x0F) << 4)),
        d >> 16,
        d >> 8,
        d};
    uint8_t crc = subghz_protocol_blocks_crc4(msg, 4, 0x13, 0);
    crc ^= msg[4] >> 4;
    d = patch_bits_ext(d, 28, 4, crc);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    for(uint8_t repeat = 0; repeat < 6; repeat++) {
        for(uint8_t p = 0; p < 4; p++) raw_pair_ext(raw, 1000, 1000);
        raw_pair_ext(raw, 500, 8000);
        raw_bits_ppm_ext(raw, d, 40, 500, 2000, 4000);
        raw_pair_ext(raw, 500, 16000);
    }
}

static void encode_gt_wt02_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint64_t d = state->original_data;
    uint32_t temp = (uint32_t)wrap_signed_ext(state->temperature_tenths, 12, &wrapped) & 0xFFFU;
    d = patch_bits_ext(d, 28, 1, state->battery ? 1 : 0);
    if(state->has_button) d = patch_bits_ext(d, 27, 1, state->button ? 1U : 0U);
    d = patch_bits_ext(d, 25, 2, wrap_unsigned_ext((int32_t)state->channel - 1, 2, &wrapped));
    d = patch_bits_ext(d, 13, 12, temp);
    d = patch_bits_ext(d, 6, 7, wrap_unsigned_ext(state->humidity, 7, &wrapped));
    d = patch_bits_ext(d, 0, 6, 0);
    uint8_t sum = (d >> 5) & 0x0E;
    uint64_t temp_data = d >> 9;
    for(uint8_t i = 0; i < 7; i++) sum += (temp_data >> (i * 4)) & 0x0F;
    d = patch_bits_ext(d, 0, 6, sum & 0x3F);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    raw_push_merge(raw, -9000);
    for(uint8_t repeat = 0; repeat < 10; repeat++) {
        raw_bits_ppm_ext(raw, d, state->bit_count == 39 ? 39 : 37, 500, 2000, 4000);
        /* The 9 ms delimiter closes this row and synchronizes the next one. */
        raw_pair_ext(raw, 500, 9000);
    }
}

static uint8_t gt_wt03_checksum(uint64_t d) {
    uint8_t msg[] = {d >> 33, d >> 25, d >> 17, d >> 9};
    uint8_t sum = 0;
    for(size_t k = 0; k < sizeof(msg); k++) {
        uint16_t key = 0x3100;
        for(int i = 7; i >= 0; i--) {
            if((msg[k] >> i) & 1U) sum ^= key & 0xFF;
            key >>= 1;
        }
    }
    return sum ^ 0x2D;
}

static void encode_gt_wt03_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint64_t d = state->original_data;
    uint32_t temp = (uint32_t)wrap_signed_ext(state->temperature_tenths, 12, &wrapped) & 0xFFFU;
    d = patch_bits_ext(d, 25, 8, wrap_unsigned_ext(state->humidity, 8, &wrapped));
    d = patch_bits_ext(d, 24, 1, state->battery ? 1 : 0);
    if(state->has_button) d = patch_bits_ext(d, 23, 1, state->button ? 1U : 0U);
    d = patch_bits_ext(d, 21, 2, wrap_unsigned_ext((int32_t)state->channel - 1, 2, &wrapped));
    d = patch_bits_ext(d, 9, 12, temp);
    d = patch_bits_ext(d, 1, 8, 0);
    d = patch_bits_ext(d, 1, 8, gt_wt03_checksum(d));

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    /* The 02-set sensor sends rows without a 60 ms packet gap. Keeping all
       ten identical rows inside one rtl_433 bitbuffer enables its majority
       vote (more than half must match). A long silence closes only the burst. */
    for(uint8_t repeat = 0; repeat < 10; repeat++) {
        raw_pair_ext(raw, 855, 855);
        raw_bits_pwm_ext(raw, d, 41, 256, 625, 625, 256);
    }
    raw_push_merge(raw, -65000);
}

static void encode_acurite986_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint64_t d = state->original_data;
    int32_t f_tenths = weather_editor_temperature_to_fahrenheit_tenths(
        state->temperature_tenths);
    int32_t f_degrees = f_tenths >= 0 ? (f_tenths + 5) / 10 : (f_tenths - 5) / 10;
    if(f_degrees < -127 || f_degrees > 127) wrapped = true;
    uint8_t magnitude = (uint8_t)(f_degrees < 0 ? -f_degrees : f_degrees) & 0x7F;
    uint8_t temp_byte = magnitude | (f_degrees < 0 ? 0x80 : 0);
    d = patch_bits_ext(d, 32, 8, reverse8_ext(temp_byte));
    d = patch_bits_ext(d, 14, 1, state->battery ? 1 : 0);
    d = patch_bits_ext(d, 15, 1, state->channel > 1 ? 1 : 0);
    d = patch_bits_ext(d, 0, 8, 0);
    uint8_t msg[] = {d >> 32, d >> 24, d >> 16, d >> 8};
    d = patch_bits_ext(d, 0, 8, subghz_protocol_blocks_crc8(msg, 4, 0x07, 0x00));

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    for(uint8_t repeat = 0; repeat < 5; repeat++) {
        for(uint8_t p = 0; p < 4; p++) raw_pair_ext(raw, 1750, 1750);
        for(int i = 39; i >= 0; i--) {
            raw_pair_ext(raw, 500, ((d >> i) & 1ULL) ? 1200 : 500);
        }
        raw_pair_ext(raw, 500, 5000);
    }
}

static uint8_t lacrosse_nibble_sum(uint64_t d) {
    uint8_t sum = 0;
    for(uint8_t shift = 4; shift <= 36; shift += 4) sum += (d >> shift) & 0x0F;
    return sum & 0x0F;
}

static uint64_t lacrosse_make_frame_ext(
    const WeatherEditorState* state,
    uint64_t base,
    uint8_t msg_type,
    bool* wrapped) {
    uint64_t d = patch_bits_ext(base, 32, 4, msg_type);
    int32_t value_tenths = msg_type == 0x0E ? state->humidity * 10 :
                                                state->temperature_tenths + 500;
    uint32_t value = wrap_decimal_ext(value_tenths, 1000, wrapped);
    uint8_t tens = (value / 100) % 10;
    uint8_t ones = (value / 10) % 10;
    uint8_t tenths = value % 10;
    d = patch_bits_ext(d, 20, 4, tens);
    d = patch_bits_ext(d, 16, 4, ones);
    d = patch_bits_ext(d, 12, 4, tenths);
    d = patch_bits_ext(d, 8, 4, tens);
    d = patch_bits_ext(d, 4, 4, ones);
    d = patch_bits_ext(d, 0, 4, 0);
    return patch_bits_ext(d, 0, 4, lacrosse_nibble_sum(d));
}

static void lacrosse_emit_frame_ext(WeatherEditorRaw* raw, uint64_t d) {
    for(uint8_t repeat = 0; repeat < 2; repeat++) {
        raw_push_merge(raw, -1000);
        for(uint8_t p = 0; p < 3; p++) raw_pair_ext(raw, 1000, 1000);
        for(int i = 43; i >= 0; i--)
            raw_pair_ext(raw, ((d >> i) & 1ULL) ? 550 : 1300, i ? 1000 : 4000);
        raw_push_merge(raw, -30000);
    }
}

static void encode_lacrosse_tx_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    const uint64_t base = state->original_data;
    const uint8_t msg_type = (base >> 32) & 0x0F;

    if(msg_type == 0x00 && state->has_temperature) {
        const uint64_t temp_frame = lacrosse_make_frame_ext(state, base, 0x00, &wrapped);
        *out = temp_frame;
        lacrosse_emit_frame_ext(raw, temp_frame);
        /* LaCrosse TX transmits temperature and humidity as separate message
           types. Synthetic TEST has both values, so send the 0xE humidity
           frame directly after the temperature frame. */
        if(state->has_humidity) {
            const uint64_t humidity_frame =
                lacrosse_make_frame_ext(state, base, 0x0E, &wrapped);
            lacrosse_emit_frame_ext(raw, humidity_frame);
        }
    } else if(msg_type == 0x0E && state->has_humidity) {
        const uint64_t humidity_frame = lacrosse_make_frame_ext(state, base, 0x0E, &wrapped);
        *out = humidity_frame;
        lacrosse_emit_frame_ext(raw, humidity_frame);
    } else {
        ((WeatherEditorState*)state)->overflow_wrapped = true;
        *out = base;
        return;
    }

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
}

static uint64_t pack_bytes_ext(const uint8_t* bytes, size_t count) {
    uint64_t d = 0;
    for(size_t i = 0; i < count; i++) d = (d << 8) | bytes[i];
    return d;
}

static void unpack_bytes_ext(uint64_t d, uint8_t* bytes, size_t count) {
    for(size_t i = 0; i < count; i++) {
        bytes[count - 1 - i] = d & 0xFF;
        d >>= 8;
    }
}

static void encode_acurite592_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint8_t b[7];
    unpack_bytes_ext(state->original_data, b, 7);
    uint8_t channel_raw = channel_to_acurite_raw(state->channel, &wrapped);
    b[0] = (b[0] & 0x3F) | (channel_raw << 6);
    b[2] = (b[2] & 0x3F) | ((!state->battery) << 6);
    b[3] = (uint8_t)wrap_unsigned_ext(state->humidity, 7, &wrapped);
    uint32_t temp = wrap_unsigned_ext(state->temperature_tenths + 1000, 12, &wrapped);
    b[4] = (b[4] & 0x60) | ((temp >> 7) & 0x1F);
    b[5] = temp & 0x7F;
    for(uint8_t i = 2; i <= 5; i++) b[i] = byte_even_parity(b[i]);
    b[6] = subghz_protocol_blocks_add_bytes(b, 6);
    uint64_t d = pack_bytes_ext(b, 7);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    for(uint8_t repeat = 0; repeat < 4; repeat++) {
        for(uint8_t p = 0; p < 3; p++) raw_pair_ext(raw, 600, 600);
        raw_bits_pwm_ext(raw, d, 56, 200, 400, 400, 200);
        raw_pair_ext(raw, 200, 2000);
        raw_push_merge(raw, -20000);
    }
}

static void encode_ambient_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint64_t d = state->original_data;
    int32_t f_tenths = weather_editor_temperature_to_fahrenheit_tenths(
        state->temperature_tenths);

    /* F007TH/F012TH payload byte 0 is normally 0x45. rtl_433 searches
       for the 12-bit prefix 0x014 and then starts the 48-bit payload eight
       bits later. Sending prefix byte 0x01 followed by payload byte 0x45
       produces exactly that alignment: 00000001 01000101. */
    d = patch_bits_ext(d, 40, 8, 0x45);
    d = patch_bits_ext(d, 31, 1, state->battery ? 1 : 0);
    d = patch_bits_ext(d, 28, 3, wrap_unsigned_ext((int32_t)state->channel - 1, 3, &wrapped));
    d = patch_bits_ext(d, 16, 12, wrap_unsigned_ext(f_tenths + 400, 12, &wrapped));
    d = patch_bits_ext(d, 8, 8, wrap_unsigned_ext(state->humidity, 8, &wrapped));
    d = patch_bits_ext(d, 0, 8, 0);
    uint8_t msg[] = {d >> 40, d >> 32, d >> 24, d >> 16, d >> 8};
    uint8_t crc = subghz_protocol_blocks_lfsr_digest8(msg, 5, 0x98, 0x3E) ^ 0x64;
    d = patch_bits_ext(d, 0, 8, crc);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    /* Three packets are contiguous. Prefix byte 0x01 + payload byte 0x45
       reproduces rtl_433's 12-bit search pattern without duplicating data. */
    for(uint8_t repeat = 0; repeat < 3; repeat++) {
        ManchesterEncoderState encoder;
        manchester_encoder_reset(&encoder);
        raw_manchester_segment_ext(raw, &encoder, 0x01, 8, 500, true);
        raw_manchester_segment_ext(raw, &encoder, d, 48, 500, true);
        raw_manchester_finish_ext(raw, &encoder, 500);
    }
    raw_push_merge(raw, -10000);
}

static uint8_t tx8300_crc(uint32_t package) {
    uint16_t x = 0;
    uint16_t y = 0;
    for(uint8_t i = 0; i < 32; i += 4) {
        x += (package >> i) & 0x0F;
        y += (package >> i) & 0x05;
    }
    return (uint8_t)(((~x & 0x0F) << 4) | (~y & 0x0F));
}

static void encode_tx8300_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint32_t d = (uint32_t)state->original_data;
    uint32_t humidity = wrap_decimal_ext(state->humidity, 100, &wrapped);
    d = (uint32_t)patch_bits_ext(d, 28, 4, humidity / 10);
    d = (uint32_t)patch_bits_ext(d, 24, 4, humidity % 10);
    d = (uint32_t)patch_bits_ext(d, 22, 2, state->battery ? 1 : 0);
    d = (uint32_t)patch_bits_ext(d, 20, 2, wrap_unsigned_ext(state->channel, 2, &wrapped));

    uint32_t magnitude = wrap_decimal_ext(
        state->temperature_tenths < 0 ? -state->temperature_tenths : state->temperature_tenths,
        1000,
        &wrapped);
    d = (uint32_t)patch_bits_ext(d, 19, 1, state->temperature_tenths < 0 ? 1 : 0);
    d = (uint32_t)patch_bits_ext(d, 8, 4, (magnitude / 100) % 10);
    d = (uint32_t)patch_bits_ext(d, 4, 4, (magnitude / 10) % 10);
    d = (uint32_t)patch_bits_ext(d, 0, 4, magnitude % 10);
    uint32_t inv = ~d;
    uint8_t crc = tx8300_crc(d);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    for(uint8_t repeat = 0; repeat < 3; repeat++) {
        /* Special two-bit/repeat preamble, then the complete 72-bit body:
           32 data + 32 inverted data + 8 checksum. The previous generator
           dropped the data MSB and produced only 71 explicit body bits. */
        raw_pair_ext(raw, 3880, 3880);
        raw_bits_ppm_ext(raw, d, 32, 1940, 1940, 3880);
        raw_bits_ppm_ext(raw, inv, 32, 1940, 1940, 3880);
        raw_bits_ppm_ext(raw, crc, 8, 1940, 1940, 3880);
        raw_pair_ext(raw, 1940, 12000);
    }
}

static void encode_wendox_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    // Wendox transmits exactly 29 bits.  Bits above bit 28 must never take
    // part in the CRC because they are not present on air.  The long/short
    // sync transition is also payload bit 28 and is always logical 1.
    uint64_t d = state->original_data & 0x1FFFFFFFULL;
    d |= 1ULL << 28;
    int32_t t = state->temperature_tenths;
    uint32_t field;
    if(t >= 0) {
        field = wrap_unsigned_ext(t - 12, 9, &wrapped);
        d = patch_bits_ext(d, 23, 1, 1);
    } else {
        uint32_t magnitude = wrap_unsigned_ext(-t + 12, 9, &wrapped);
        field = ((~magnitude) + 1U) & 0x1FFU;
        d = patch_bits_ext(d, 23, 1, 0);
    }
    d = patch_bits_ext(d, 14, 9, field);
    d = patch_bits_ext(d, 6, 1, state->battery ? 1 : 0);
    d = patch_bits_ext(d, 0, 4, 0);
    uint8_t msg[] = {d >> 28, d >> 20, d >> 12, d >> 4};
    d = patch_bits_ext(d, 0, 4, subghz_protocol_blocks_crc4(msg, 4, 0x9, 0xD));

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    for(uint8_t repeat = 0; repeat < 3; repeat++) {
        for(uint8_t p = 0; p < (repeat ? 7 : 11); p++) raw_pair_ext(raw, 1955, 5865);
        /* The long/short transition after the preamble is also payload bit 28
           and the decoder records it immediately. Emit only the remaining
           28 bits here; the old code duplicated the first bit. */
        raw_pair_ext(raw, 5865, 1955);
        for(int i = 27; i >= 0; i--) {
            bool bit = (d >> i) & 1ULL;
            raw_pair_ext(raw, bit ? 5865 : 1955, i ? (bit ? 1955 : 5865) : 12000);
        }
    }
}

static void encode_auriol_ahfl_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint64_t d = state->original_data;
    uint32_t temp = (uint32_t)wrap_signed_ext(state->temperature_tenths, 12, &wrapped) & 0xFFFU;
    d = patch_bits_ext(d, 33, 1, state->battery ? 0 : 1);
    if(state->has_button) d = patch_bits_ext(d, 32, 1, state->button ? 1U : 0U);
    d = patch_bits_ext(d, 30, 2, wrap_unsigned_ext((int32_t)state->channel - 1, 2, &wrapped));
    d = patch_bits_ext(d, 18, 12, temp);
    d = patch_bits_ext(d, 11, 7, wrap_unsigned_ext(state->humidity, 7, &wrapped));
    d = patch_bits_ext(d, 6, 4, 0x4);
    d = patch_bits_ext(d, 0, 6, 0);
    uint64_t payload = d >> 6;
    uint8_t checksum = 0;
    for(uint8_t i = 0; i < 9; i++) checksum += (payload >> (i * 4)) & 0x0F;
    d = patch_bits_ext(d, 0, 6, checksum & 0x3F);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    /* rtl_433 expects at least two repeated 42-bit rows. Eight rows provide
       margin, with both data gaps below 4248 us and 5 ms row delimiters below
       the 9150 us burst reset. */
    raw_push_merge(raw, -10000);
    for(uint8_t repeat = 0; repeat < 8; repeat++) {
        raw_bits_ppm_ext(raw, d, 42, 500, 2050, 4050);
        raw_pair_ext(raw, 500, repeat == 7 ? 10000 : 5000);
    }
}

static void encode_kedsum_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint64_t d = state->original_data;
    int32_t f_tenths = weather_editor_temperature_to_fahrenheit_tenths(
        state->temperature_tenths);
    uint32_t temp = wrap_unsigned_ext(f_tenths + 900, 12, &wrapped);
    d = patch_bits_ext(d, 30, 2, state->battery ? 0 : 2);
    d = patch_bits_ext(d, 28, 2, wrap_unsigned_ext((int32_t)state->channel - 1, 2, &wrapped));
    /* The three temperature nibbles are transmitted least-significant nibble first. */
    d = patch_bits_ext(d, 24, 4, temp & 0x0F);
    d = patch_bits_ext(d, 20, 4, (temp >> 4) & 0x0F);
    d = patch_bits_ext(d, 16, 4, (temp >> 8) & 0x0F);
    uint8_t hum = wrap_unsigned_ext(state->humidity, 8, &wrapped);
    d = patch_bits_ext(d, 8, 4, hum >> 4);
    d = patch_bits_ext(d, 12, 4, hum & 0x0F);
    d = patch_bits_ext(d, 0, 4, 0);
    uint8_t msg[] = {d >> 32, d >> 24, d >> 16, d >> 8, d};
    uint8_t crc = subghz_protocol_blocks_crc4(msg, 4, 0x03, 0);
    crc ^= msg[4] >> 4;
    d = patch_bits_ext(d, 0, 4, crc);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    /* Real Kedsum bursts start with a train of sync pulses, followed by six
       42-bit rows. One 8 ms sync pair separates consecutive rows. */
    for(uint8_t p = 0; p < 15; p++) raw_pair_ext(raw, 500, 8000);
    for(uint8_t repeat = 0; repeat < 6; repeat++) {
        raw_bits_ppm_ext(raw, d, 42, 500, 2000, 4000);
        raw_pair_ext(raw, 500, 8000);
    }
}

static void encode_acurite5n1_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint8_t b[8];
    unpack_bytes_ext(state->original_data, b, 8);
    uint8_t channel_raw = channel_to_acurite_raw(state->channel, &wrapped);
    b[0] = (b[0] & 0x3F) | (channel_raw << 6);
    b[2] = (b[2] & 0x3F) | ((!state->battery) << 6);
    int32_t f_tenths = weather_editor_temperature_to_fahrenheit_tenths(
        state->temperature_tenths);
    uint32_t temp = wrap_unsigned_ext(f_tenths + 400, 11, &wrapped);
    b[4] = (b[4] & 0x70) | ((temp >> 7) & 0x0F);
    b[5] = temp & 0x7F;
    b[6] = wrap_unsigned_ext(state->humidity, 7, &wrapped);
    for(uint8_t i = 2; i <= 6; i++) b[i] = byte_even_parity(b[i]);
    b[7] = subghz_protocol_blocks_add_bytes(b, 7);
    uint64_t d = pack_bytes_ext(b, 8);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    for(uint8_t repeat = 0; repeat < 4; repeat++) {
        for(uint8_t p = 0; p < 3; p++) raw_pair_ext(raw, 600, 600);
        raw_bits_pwm_ext(raw, d, 64, 200, 400, 400, 200);
        raw_pair_ext(raw, 200, 2000);
        raw_push_merge(raw, -20000);
    }
}

static void encode_emos_e601x_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    if(state->frame_bits != 120) return;

    bool wrapped = false;
    uint64_t upper = state->frame_upper;
    uint64_t lower = state->frame_lower;
    uint32_t temp =
        (uint32_t)wrap_signed_ext(state->temperature_tenths, 12, &wrapped) & 0x0FFFU;

    lower = patch_bits_ext(lower, 52, 2, wrap_unsigned_ext(state->channel, 2, &wrapped));
    lower = patch_bits_ext(lower, 40, 12, temp);
    lower = patch_bits_ext(lower, 32, 8, wrap_unsigned_ext(state->humidity, 8, &wrapped));
    lower = patch_bits_ext(lower, 18, 1, state->battery ? 0 : 1);
    lower = patch_bits_ext(lower, 8, 8, 0);

    uint8_t msg[] = {
        upper >> 48, upper >> 40, upper >> 32, upper >> 24, upper >> 16, upper >> 8,
        upper, lower >> 56, lower >> 48, lower >> 40, lower >> 32, lower >> 24,
        lower >> 16};
    lower = patch_bits_ext(lower, 8, 8, subghz_protocol_blocks_add_bytes(msg, 13));
    lower = patch_bits_ext(lower, 0, 8, 0);
    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    ((WeatherEditorState*)state)->frame_lower = lower;
    *out = lower >> 16;

    /* A gap above 850 us makes rtl_433 account the event immediately, so no
       artificial inter-row pause is allowed. Each following 1836 us sync
       pulse starts a new row in the same bitbuffer; the final silence closes
       one six-row event and satisfies the repeated-prefix>=3 check. */
    for(uint8_t repeat = 0; repeat < 6; repeat++) {
        const uint64_t row_lower = patch_bits_ext(lower, 0, 8, repeat);
        raw_pair_ext(raw, 1836, 280);
        raw_bits_pwm_ext(raw, upper, 56, 280, 796, 796, 280);
        raw_bits_pwm_ext(raw, row_lower, 64, 280, 796, 796, 280);
    }
    raw_push_merge(raw, -12000);
}

static uint8_t reverse_nibble_ext(uint8_t nibble) {
    return (uint8_t)(((nibble & 1U) << 3) | ((nibble & 2U) << 1) |
                     ((nibble & 4U) >> 1) | ((nibble & 8U) >> 3));
}

static void encode_bl999_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint8_t nib[9] = {0};
    uint64_t d = state->original_data & 0xFFFFFFFFFULL;
    for(uint8_t i = 0; i < 9; i++) {
        nib[i] = reverse_nibble_ext((d >> ((8 - i) * 4)) & 0x0F);
    }
    nib[2] = (nib[2] & 0x0E) | (state->battery ? 1 : 0);
    uint16_t temp = (uint16_t)((uint32_t)wrap_signed_ext(state->temperature_tenths, 12, &wrapped) & 0xFFFU);
    nib[3] = temp & 0x0F;
    nib[4] = (temp >> 4) & 0x0F;
    nib[5] = (temp >> 8) & 0x0F;
    int32_t hum_delta = 100 - state->humidity;
    uint8_t hum_encoded = (uint8_t)wrap_signed_ext(hum_delta, 8, &wrapped);
    nib[6] = hum_encoded & 0x0F;
    nib[7] = hum_encoded >> 4;
    uint8_t sum = 0;
    for(uint8_t i = 0; i < 8; i++) sum += nib[i];
    nib[8] = sum & 0x0F;
    d = 0;
    for(uint8_t i = 0; i < 9; i++) d = (d << 4) | reverse_nibble_ext(nib[i]);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = d;
    raw_push_merge(raw, -9000);
    for(uint8_t repeat = 0; repeat < 6; repeat++) {
        raw_bits_ppm_ext(raw, d, 36, 550, 1850, 3900);
        /* This high pulse flushes the final bit in the decoder; the following
           9 ms low is simultaneously the start gap for the next repeat. */
        raw_pair_ext(raw, 550, 9000);
    }
}

static uint8_t oregon_checksum_standard_ext(
    uint32_t fixed,
    uint64_t payload,
    uint8_t payload_bits) {
    uint16_t sum = 0;
    for(uint8_t i = 0; i < 8; i++) sum += (fixed >> (i * 4U)) & 0x0FU;
    const uint8_t payload_nibbles = (uint8_t)((payload_bits + 3U) / 4U);
    for(uint8_t i = 0; i < payload_nibbles; i++)
        sum += (payload >> (i * 4U)) & 0x0FU;
    sum &= 0xFFU;
    return (uint8_t)(((sum & 0x0FU) << 4) | ((sum >> 4) & 0x0FU));
}

#if WEATHER_EDITOR_TX_VARIANT == WEATHER_EDITOR_TX_VARIANT_STATION_SAFE
static uint8_t oregon_checksum_goodfaps_v2_ext(uint32_t fixed, uint64_t payload) {
    /* Keep the historical good-faps Oregon2 checksum layout in the Flipper
       reference variant. Its local decoder expects a 64-bit THGR122N body. */
    uint8_t sum = fixed & 0x0F;
    for(uint8_t i = 1; i < 8; i++) {
        fixed >>= 4;
        payload >>= 4;
        sum += (fixed & 0x0F) + (payload & 0x0F);
    }
    return (uint8_t)(((sum >> 4) & 0x0F) | (sum << 4));
}
#endif

static uint16_t oregon_temp_payload_ext(int32_t temperature_tenths, bool* wrapped) {
    uint32_t magnitude = wrap_decimal_ext(
        temperature_tenths < 0 ? -temperature_tenths : temperature_tenths,
        1000,
        wrapped);
    uint16_t value = temperature_tenths < 0 ? 1 : 0;
    value |= ((magnitude / 100) % 10) << 4;
    value |= ((magnitude / 10) % 10) << 8;
    value |= (magnitude % 10) << 12;
    return value;
}

static uint8_t oregon_humidity_payload_ext(int32_t humidity, bool* wrapped) {
    uint32_t value = wrap_decimal_ext(humidity, 100, wrapped);
    return (uint8_t)(((value % 10) << 4) | (value / 10));
}

static void encode_oregon2_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint32_t fixed = (uint32_t)state->original_data;
    uint8_t one_hot =
        state->channel >= 1 && state->channel <= 4 ? (1U << (state->channel - 1)) : 1;
    if(state->channel < 1 || state->channel > 4) wrapped = true;
    fixed = (uint32_t)patch_bits_ext(fixed, 12, 4, one_hot);
    fixed = (uint32_t)patch_bits_ext(fixed, 2, 1, state->battery ? 1 : 0);

    const uint16_t temp = oregon_temp_payload_ext(state->temperature_tenths, &wrapped);
    uint64_t var_data = 0;
    uint8_t total_var_bits = 0;

    if(state->var_bits == 16) {
        const uint64_t payload = temp;
        var_data = (payload << 8) | oregon_checksum_standard_ext(fixed, payload, 16);
        total_var_bits = 24;
    } else if(state->var_bits == 24) {
        const uint8_t hum = oregon_humidity_payload_ext(state->humidity, &wrapped);
#if WEATHER_EDITOR_TX_VARIANT == WEATHER_EDITOR_TX_VARIANT_SDR_RTL433
        const uint64_t payload = ((uint64_t)temp << 12) | ((uint64_t)hum << 4);
        var_data = (payload << 8) | oregon_checksum_standard_ext(fixed, payload, 28);
        total_var_bits = 36;
#else
        const uint64_t payload = ((uint64_t)temp << 8) | hum;
        var_data = (payload << 8) | oregon_checksum_goodfaps_v2_ext(fixed, payload);
        total_var_bits = 32;
#endif
    } else {
        ((WeatherEditorState*)state)->overflow_wrapped = true;
        *out = fixed;
        return;
    }

    const uint32_t tx_fixed = (uint32_t)reverse_bits_in_nibbles_ext(fixed, 32);
    const uint64_t tx_var = reverse_bits_in_nibbles_ext(var_data, total_var_bits);
    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = fixed;
    ((WeatherEditorState*)state)->var_data = var_data;

#if WEATHER_EDITOR_TX_VARIANT == WEATHER_EDITOR_TX_VARIANT_SDR_RTL433
    const uint32_t half_us = 440;
    for(uint8_t repeat = 0; repeat < 4; repeat++) {
        /* Slicer output: 55 55 55 55 99, then doubled Manchester payload.
           This gives rtl_433 its expected b[1]/b[2] preamble, 0x5599 sync at
           b[3], and payload start exactly at bit 40. */
        raw_oregon_target_segment_ext(raw, 0x5555555599ULL, 40, half_us);
        raw_oregon2_payload_segment_ext(raw, tx_fixed, 32, half_us);
        raw_oregon2_payload_segment_ext(raw, tx_var, total_var_bits, half_us);
        raw_push_merge(raw, -12000);
    }
#else
    const uint32_t half_us = 500;
    for(uint8_t repeat = 0; repeat < 3; repeat++) {
        ManchesterEncoderState encoder;
        manchester_encoder_reset(&encoder);
        raw_manchester_oregon2_segment_ext(raw, &encoder, 0x7FFF5, 19, half_us);
        raw_manchester_oregon2_segment_ext(raw, &encoder, tx_fixed, 32, half_us);
        raw_manchester_oregon2_segment_ext(raw, &encoder, tx_var, total_var_bits, half_us);
        raw_manchester_finish_ext(raw, &encoder, half_us);
        raw_push_merge(raw, -12000);
    }
#endif
}

static void encode_oregon3_ext(
    const WeatherEditorState* state,
    WeatherEditorRaw* raw,
    uint64_t* out) {
    bool wrapped = false;
    uint32_t fixed = (uint32_t)state->original_data;
    fixed = (uint32_t)patch_bits_ext(
        fixed, 12, 4, wrap_unsigned_ext(state->channel, 4, &wrapped));
    fixed = (uint32_t)patch_bits_ext(fixed, 2, 1, state->battery ? 1 : 0);
    if(state->var_bits != 28) {
        ((WeatherEditorState*)state)->overflow_wrapped = true;
        *out = fixed;
        return;
    }

    const uint16_t temp = oregon_temp_payload_ext(state->temperature_tenths, &wrapped);
    const uint8_t hum = oregon_humidity_payload_ext(state->humidity, &wrapped);
    const uint64_t payload = ((uint64_t)temp << 12) | ((uint64_t)hum << 4);
    uint64_t var_data = (payload << 8) | oregon_checksum_standard_ext(fixed, payload, 28);
    const uint8_t total_var_bits = 36;
    const uint32_t tx_fixed = (uint32_t)reverse_bits_in_nibbles_ext(fixed, 32);
    const uint64_t tx_var = reverse_bits_in_nibbles_ext(var_data, total_var_bits);

    ((WeatherEditorState*)state)->overflow_wrapped = wrapped;
    *out = fixed;
    ((WeatherEditorState*)state)->var_data = var_data;
#if WEATHER_EDITOR_TX_VARIANT == WEATHER_EDITOR_TX_VARIANT_SDR_RTL433
    const uint32_t half_us = 440;
    for(uint8_t repeat = 0; repeat < 5; repeat++) {
        /* Deterministic slicer stream: 24 zero preamble bits + sync nibble 5,
           followed by the nibble-reflected v3 payload. */
        raw_oregon_target_segment_ext(raw, 0x0000005, 28, half_us);
        raw_oregon_target_segment_ext(raw, tx_fixed, 32, half_us);
        raw_oregon_target_segment_ext(raw, tx_var, total_var_bits, half_us);
        raw_push_merge(raw, -12000);
    }
#else
    const uint32_t half_us = 550;
    for(uint8_t repeat = 0; repeat < 3; repeat++) {
        ManchesterEncoderState encoder;
        manchester_encoder_reset(&encoder);
        raw_manchester_segment_ext(raw, &encoder, 0xFFFFFF5, 28, half_us, true);
        raw_manchester_segment_ext(raw, &encoder, tx_fixed, 32, half_us, true);
        raw_manchester_segment_ext(raw, &encoder, tx_var, total_var_bits, half_us, true);
        raw_manchester_finish_ext(raw, &encoder, half_us);
        raw_push_merge(raw, -12000);
    }
#endif
}

bool weather_editor_encode_extended(
    const WeatherEditorState* state,
    WeatherEditorRaw* out_raw,
    uint64_t* edited_data,
    bool* handled,
    FuriString* status) {
    *handled = true;
    if(protocol_is_ext(state, "inFactory-TH")) encode_infactory_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "GT-WT02")) encode_gt_wt02_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "GT-WT03")) encode_gt_wt03_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "Acurite-986")) encode_acurite986_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "LaCrosse_TX")) encode_lacrosse_tx_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "Acurite 592TXR")) encode_acurite592_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "Ambient_Weather")) encode_ambient_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "TX8300")) encode_tx8300_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "Wendox W6726")) encode_wendox_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "Auriol AHFL")) encode_auriol_ahfl_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "Kedsum-TH")) encode_kedsum_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "Acurite 5n1")) encode_acurite5n1_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "EMOS E601x")) encode_emos_e601x_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "BL999")) encode_bl999_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "Oregon2")) encode_oregon2_ext(state, out_raw, edited_data);
    else if(protocol_is_ext(state, "Oregon3")) encode_oregon3_ext(state, out_raw, edited_data);
    else {
        *handled = false;
        return false;
    }

    if(out_raw->overflowed) {
        if(status) furi_string_set(status, "TX burst too long");
        return false;
    }
    if(out_raw->count == 0) {
        if(status) furi_string_set(status, "Unsupported sensor variant");
        return false;
    }
    if(status) {
        furi_string_set(
            status,
            state->overflow_wrapped ? "Encoded with overflow" : "Edited frame ready");
    }
    return true;
}
