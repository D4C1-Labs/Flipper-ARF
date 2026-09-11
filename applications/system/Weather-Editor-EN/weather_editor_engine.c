#include "weather_editor_engine.h"
#include "weather_editor_encoders_ext.h"
#include "weather_editor_tx_variant.h"

#include <storage/storage.h>
#include <lib/subghz/blocks/math.h>
#include <lib/toolbox/manchester_encoder.h>
#include <lib/flipper_format/flipper_format_i.h>
#include <lib/toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>
#include <stdint.h>
#include <string.h>

#define TAG "WeatherEditorEngine"

int32_t weather_editor_temperature_to_fahrenheit_tenths(int32_t celsius_tenths) {
    int64_t scaled = (int64_t)celsius_tenths * 9;
    if(scaled >= 0) scaled += 2;
    else scaled -= 2;
    scaled = scaled / 5 + 320;
    if(scaled > INT32_MAX) return INT32_MAX;
    if(scaled < INT32_MIN) return INT32_MIN;
    return (int32_t)scaled;
}

int32_t weather_editor_temperature_to_celsius_tenths(int32_t fahrenheit_tenths) {
    int64_t scaled = ((int64_t)fahrenheit_tenths - 320) * 5;
    if(scaled >= 0) scaled += 4;
    else scaled -= 4;
    scaled /= 9;
    if(scaled > INT32_MAX) return INT32_MAX;
    if(scaled < INT32_MIN) return INT32_MIN;
    return (int32_t)scaled;
}

int32_t weather_editor_abs_saturated(int32_t value) {
    if(value == INT32_MIN) return INT32_MAX;
    return value < 0 ? -value : value;
}

static int32_t wrap_signed(int32_t value, uint8_t bits, bool* wrapped) {
    const int32_t min = -(1L << (bits - 1));
    const int32_t max = (1L << (bits - 1)) - 1;
    if(value < min || value > max) *wrapped = true;
    const uint32_t mask = (1UL << bits) - 1UL;
    uint32_t u = ((uint32_t)value) & mask;
    if(u & (1UL << (bits - 1))) return (int32_t)(u | ~mask);
    return (int32_t)u;
}

static uint32_t wrap_unsigned(int32_t value, uint8_t bits, bool* wrapped) {
    const uint32_t mask = (1UL << bits) - 1UL;
    if(value < 0 || (uint32_t)value > mask) *wrapped = true;
    return ((uint32_t)value) & mask;
}

WeatherEditorState* weather_editor_state_alloc(void) {
    WeatherEditorState* state = malloc(sizeof(WeatherEditorState));
    if(!state) return NULL;
    memset(state, 0, sizeof(WeatherEditorState));
    state->protocol_name = furi_string_alloc();
    if(!state->protocol_name) {
        free(state);
        return NULL;
    }
    state->original_data = 0;
    state->edited_data = 0;
    state->bit_count = 0;
    state->var_bits = 0;
    state->var_data = 0;
    state->frame_bits = 0;
    state->frame_upper = 0;
    state->frame_lower = 0;
    state->id = 0;
    state->temperature_tenths = 0;
    state->humidity = 0;
    state->battery = 0;
    state->channel = 1;
    state->button = 0;
    state->battery_kind = WeatherEditorBatteryNone;
    state->has_temperature = false;
    state->has_humidity = false;
    state->has_channel = false;
    state->has_button = false;
    state->encoder_available = false;
    state->overflow_wrapped = false;
    return state;
}

void weather_editor_state_free(WeatherEditorState* state) {
    if(!state) return;
    if(state->protocol_name) furi_string_free(state->protocol_name);
    state->protocol_name = NULL;
    free(state);
}

static bool read_u32_optional(FlipperFormat* fff, const char* key, uint32_t* value) {
    if(!fff || !key || !value) return false;
    if(!flipper_format_rewind(fff)) return false;
    return flipper_format_read_uint32(fff, key, value, 1);
}

static bool protocol_is(const WeatherEditorState* state, const char* name) {
    if(!state || !state->protocol_name || !name) return false;
    const char* protocol = furi_string_get_cstr(state->protocol_name);
    return protocol && strcmp(protocol, name) == 0;
}

void weather_editor_get_channel_limits(
    const WeatherEditorState* state,
    int32_t* min_channel,
    int32_t* max_channel) {
    if(min_channel) *min_channel = 0;
    if(max_channel) *max_channel = 0;
    if(!state || !state->protocol_name || !min_channel || !max_channel) return;

    int32_t min_value = 0;
    int32_t max_value = 255;
    const char* protocol = furi_string_get_cstr(state->protocol_name);
    if(!protocol) return;

    if(!state->has_channel) {
        min_value = 0;
        max_value = 0;
    } else if(
        !strcmp(protocol, "Nexus-TH") || !strcmp(protocol, "ThermoPRO-TX4") ||
        !strcmp(protocol, "Auriol HG06061") || !strcmp(protocol, "Solight TE44") ||
        !strcmp(protocol, "TX141THBv2") || !strcmp(protocol, "GT-WT02") ||
        !strcmp(protocol, "GT-WT03") || !strcmp(protocol, "Auriol AHFL") ||
        !strcmp(protocol, "Kedsum-TH") || !strcmp(protocol, "Oregon2")) {
        min_value = 1;
        max_value = 4;
    } else if(
        !strcmp(protocol, "Bresser-3CH") || !strcmp(protocol, "Vauno-EN8822C") ||
        !strcmp(protocol, "inFactory-TH") || !strcmp(protocol, "Acurite 592TXR") ||
        !strcmp(protocol, "TX8300") || !strcmp(protocol, "Acurite 5n1") ||
        !strcmp(protocol, "BL999") || !strcmp(protocol, "EMOS E601x")) {
        min_value = 0;
        max_value = 3;
    } else if(!strcmp(protocol, "Acurite-986")) {
        min_value = 1;
        max_value = 2;
    } else if(!strcmp(protocol, "Ambient_Weather")) {
        min_value = 1;
        max_value = 8;
    } else if(!strcmp(protocol, "Oregon3")) {
        min_value = 0;
        max_value = 15;
    }

    *min_channel = min_value;
    *max_channel = max_value;
}

uint8_t weather_editor_sanitize_channel(const WeatherEditorState* state, int32_t channel) {
    if(!state) return 0U;
    int32_t min_channel = 0;
    int32_t max_channel = 255;
    weather_editor_get_channel_limits(state, &min_channel, &max_channel);
    if(channel < min_channel) channel = min_channel;
    if(channel > max_channel) channel = max_channel;
    return (uint8_t)channel;
}

bool weather_editor_state_load(WeatherEditorState* state, FlipperFormat* format) {
    if(!state || !state->protocol_name || !format) return false;

    furi_string_reset(state->protocol_name);
    state->original_data = 0;
    state->edited_data = 0;
    state->bit_count = 0;
    state->var_bits = 0;
    state->var_data = 0;
    state->frame_bits = 0;
    state->frame_upper = 0;
    state->frame_lower = 0;
    state->id = 0;
    state->temperature_tenths = -2730;
    state->humidity = 0xFF;
    state->battery = 0xFF;
    state->channel = 0xFF;
    state->button = 0;
    state->battery_kind = WeatherEditorBatteryNone;
    state->has_temperature = false;
    state->has_humidity = false;
    state->has_channel = false;
    state->has_button = false;
    state->encoder_available = false;
    state->overflow_wrapped = false;

    uint32_t tmp = 0;
    float temp = 0.0f;
    uint8_t data_bytes[8] = {0};

    flipper_format_rewind(format);
    if(!flipper_format_read_string(format, "Protocol", state->protocol_name)) return false;

    if(read_u32_optional(format, "Bit", &tmp)) state->bit_count = (uint8_t)tmp;
    if(read_u32_optional(format, "Id", &tmp)) state->id = tmp;
    if(read_u32_optional(format, "Ch", &tmp)) state->channel = (uint8_t)tmp;
    if(read_u32_optional(format, "Btn", &tmp) && tmp != 0xFFU) {
        state->button = tmp ? 1U : 0U;
        state->has_button = true;
    }
    if(read_u32_optional(format, "Hum", &tmp)) state->humidity = (int32_t)tmp;
    if(read_u32_optional(format, "Batt", &tmp)) state->battery = (int32_t)tmp;

    flipper_format_rewind(format);
    if(flipper_format_read_float(format, "Temp", &temp, 1)) {
        state->temperature_tenths = (int32_t)(temp * 10.0f + (temp >= 0 ? 0.5f : -0.5f));
    }

    flipper_format_rewind(format);
    if(!flipper_format_read_hex(format, "Data", data_bytes, sizeof(data_bytes))) return false;
    state->original_data = 0;
    for(size_t i = 0; i < sizeof(data_bytes); i++) {
        state->original_data = (state->original_data << 8) | data_bytes[i];
    }
    state->edited_data = state->original_data;

    if(read_u32_optional(format, "VarBits", &tmp)) {
        state->var_bits = (uint8_t)tmp;
        flipper_format_rewind(format);
        if(protocol_is(state, "Oregon2")) {
            uint32_t var32 = 0;
            if(flipper_format_read_hex(format, "VarData", (uint8_t*)&var32, sizeof(var32))) {
                state->var_data = var32;
            }
        } else if(protocol_is(state, "Oregon3")) {
            uint64_t var64 = 0;
            if(flipper_format_read_hex(format, "VarData", (uint8_t*)&var64, sizeof(var64))) {
                state->var_data = var64;
            }
        }
    }

    if(read_u32_optional(format, "FrameBits", &tmp)) {
        state->frame_bits = (uint8_t)tmp;
        flipper_format_rewind(format);
        flipper_format_read_hex(
            format, "UpperData", (uint8_t*)&state->frame_upper, sizeof(state->frame_upper));
        flipper_format_rewind(format);
        flipper_format_read_hex(
            format, "LowerData", (uint8_t*)&state->frame_lower, sizeof(state->frame_lower));
    }

    const char* p = furi_string_get_cstr(state->protocol_name);
    state->has_temperature = state->temperature_tenths > -2730;
    state->has_humidity = state->humidity != 0xFF;
    state->has_channel = state->channel != 0xFF;
    state->battery_kind = state->battery == 0xFF ? WeatherEditorBatteryNone : WeatherEditorBatteryFlag;

    state->encoder_available =
        !strcmp(p, "Nexus-TH") || !strcmp(p, "ThermoPRO-TX4") ||
        !strcmp(p, "Bresser-3CH") || !strcmp(p, "Auriol HG06061") ||
        !strcmp(p, "Vauno-EN8822C") || !strcmp(p, "Solight TE44") ||
        !strcmp(p, "TX141THBv2") || !strcmp(p, "Acurite-606TX") ||
        !strcmp(p, "Acurite-609TXC") || !strcmp(p, "inFactory-TH") ||
        !strcmp(p, "GT-WT02") || !strcmp(p, "GT-WT03") ||
        !strcmp(p, "Acurite-986") || !strcmp(p, "LaCrosse_TX") ||
        !strcmp(p, "Acurite 592TXR") || !strcmp(p, "Ambient_Weather") ||
        !strcmp(p, "TX8300") || !strcmp(p, "Wendox W6726") ||
        !strcmp(p, "Auriol AHFL") || !strcmp(p, "Kedsum-TH") ||
        !strcmp(p, "Acurite 5n1") || !strcmp(p, "BL999") ||
        (!strcmp(p, "EMOS E601x") && state->frame_bits == 120) ||
        (!strcmp(p, "Oregon2") && (state->var_bits == 16 || state->var_bits == 24)) ||
        (!strcmp(p, "Oregon3") && state->var_bits == 28);

    return true;
}

bool weather_editor_state_copy(WeatherEditorState* dst, const WeatherEditorState* src) {
    if(!dst || !src || !dst->protocol_name || !src->protocol_name) return false;
    furi_string_set(dst->protocol_name, src->protocol_name);
    dst->original_data = src->original_data;
    dst->edited_data = src->edited_data;
    dst->bit_count = src->bit_count;
    dst->var_bits = src->var_bits;
    dst->var_data = src->var_data;
    dst->frame_bits = src->frame_bits;
    dst->frame_upper = src->frame_upper;
    dst->frame_lower = src->frame_lower;
    dst->id = src->id;
    dst->temperature_tenths = src->temperature_tenths;
    dst->humidity = src->humidity;
    dst->battery = src->battery;
    dst->channel = src->channel;
    dst->button = src->button;
    dst->battery_kind = src->battery_kind;
    dst->has_temperature = src->has_temperature;
    dst->has_humidity = src->has_humidity;
    dst->has_channel = src->has_channel;
    dst->has_button = src->has_button;
    dst->encoder_available = src->encoder_available;
    dst->overflow_wrapped = src->overflow_wrapped;
    return true;
}

WeatherEditorRaw* weather_editor_raw_alloc(size_t capacity) {
    if(capacity == 0) return NULL;
    WeatherEditorRaw* raw = malloc(sizeof(WeatherEditorRaw));
    if(!raw) return NULL;
    raw->values = malloc(sizeof(int32_t) * capacity);
    if(!raw->values) {
        free(raw);
        return NULL;
    }
    raw->count = 0;
    raw->capacity = capacity;
    raw->overflowed = false;
    return raw;
}

void weather_editor_raw_free(WeatherEditorRaw* raw) {
    if(!raw) return;
    free(raw->values);
    free(raw);
}

void weather_editor_raw_reset(WeatherEditorRaw* raw) {
    if(!raw) return;
    raw->count = 0;
    raw->overflowed = false;
}

bool weather_editor_raw_push(WeatherEditorRaw* raw, int32_t duration) {
    if(!raw || !raw->values || raw->capacity == 0U) return false;
    if(raw->count >= raw->capacity) {
        raw->overflowed = true;
        return false;
    }
    raw->values[raw->count++] = duration;
    return true;
}

static void raw_pair(WeatherEditorRaw* raw, uint32_t high_us, uint32_t low_us) {
    weather_editor_raw_push(raw, (int32_t)high_us);
    weather_editor_raw_push(raw, -(int32_t)low_us);
}

static void raw_bits_ppm(
    WeatherEditorRaw* raw,
    uint64_t data,
    uint8_t bits,
    uint32_t high_us,
    uint32_t low_zero,
    uint32_t low_one) {
    for(int i = bits - 1; i >= 0; i--) {
        raw_pair(raw, high_us, ((data >> i) & 1ULL) ? low_one : low_zero);
    }
}

static void raw_bits_pwm(
    WeatherEditorRaw* raw,
    uint64_t data,
    uint8_t bits,
    uint32_t high_zero,
    uint32_t low_zero,
    uint32_t high_one,
    uint32_t low_one) {
    for(int i = bits - 1; i >= 0; i--) {
        if((data >> i) & 1ULL)
            raw_pair(raw, high_one, low_one);
        else
            raw_pair(raw, high_zero, low_zero);
    }
}

static uint64_t patch_bits(uint64_t data, uint8_t lsb, uint8_t width, uint64_t value) {
    uint64_t mask = ((width == 64 ? UINT64_MAX : ((1ULL << width) - 1ULL)) << lsb);
    return (data & ~mask) | ((value << lsb) & mask);
}

static void encode_nexus(WeatherEditorState const* s, WeatherEditorRaw* raw, uint64_t* out) {
    bool wrapped = false;
    uint64_t d = s->original_data;
    uint32_t t = wrap_unsigned(wrap_signed(s->temperature_tenths, 12, &wrapped), 12, &wrapped);
    d = patch_bits(d, 27, 1, s->battery ? 0 : 1);
    d = patch_bits(
        d, 24, 2, wrap_unsigned(s->channel ? s->channel - 1 : 0, 2, &wrapped));
    d = patch_bits(d, 12, 12, t);
    d = patch_bits(d, 8, 4, 0xF);
    d = patch_bits(d, 0, 8, wrap_unsigned(s->humidity, 8, &wrapped));
    ((WeatherEditorState*)s)->overflow_wrapped = wrapped;
    *out = d;
    /* One 4 ms delimiter is both the end of the current row and the
       synchronization gap for the next row. Never duplicate it. */
    weather_editor_raw_push(raw, -4000);
    for(uint8_t r = 0; r < 12; r++) {
        raw_bits_ppm(raw, d, 36, 500, 1000, 2000);
        raw_pair(raw, 500, 4000);
    }
}

static void encode_thermopro(WeatherEditorState const* s, WeatherEditorRaw* raw, uint64_t* out) {
    bool wrapped = false;
    uint64_t d = s->original_data;
    uint32_t t = wrap_unsigned(wrap_signed(s->temperature_tenths, 12, &wrapped), 12, &wrapped);
    /* Prologue/ThermoPRO bit means battery OK, while editor state stores
       battery LOW. Invert it so TEST=LOW is displayed consistently. */
    d = patch_bits(d, 24, 1, s->battery ? 0 : 1);
    if(s->has_button) d = patch_bits(d, 23, 1, s->button ? 1U : 0U);
    d = patch_bits(
        d, 21, 2, wrap_unsigned(s->channel ? s->channel - 1 : 0, 2, &wrapped));
    d = patch_bits(d, 9, 12, t);
    d = patch_bits(d, 1, 8, wrap_unsigned(s->humidity, 8, &wrapped));
    d = patch_bits(d, 0, 1, 0); /* normalized storage: optional trailing bit */
    ((WeatherEditorState*)s)->overflow_wrapped = wrapped;
    *out = d;

    /* Current rtl_433 reference: exactly 36 payload bits, repeated seven times.
       The editor keeps the historical 37-bit normalized value internally, so
       shift away only the optional trailing separator when generating RF. */
    const uint64_t payload36 = d >> 1;
    weather_editor_raw_push(raw, -9000);
    for(uint8_t r = 0; r < 7; r++) {
        raw_bits_ppm(raw, payload36, 36, 500, 2000, 4000);
        raw_pair(raw, 500, 9000);
    }
}

static void encode_bresser(WeatherEditorState const* s, WeatherEditorRaw* raw, uint64_t* out) {
    bool wrapped = false;
    uint64_t d = s->original_data;

    /* Bresser stores unsigned Fahrenheit x10 with a +90.0 F offset.
       Keep the user value internally in Celsius x10, convert explicitly to
       Fahrenheit x10, then add the protocol offset. This avoids hidden unit
       assumptions and makes the encoded field directly auditable. */
    const int32_t fahrenheit_tenths =
        weather_editor_temperature_to_fahrenheit_tenths(s->temperature_tenths);
    const int32_t temperature_raw = fahrenheit_tenths + 900;

    d = patch_bits(d, 31, 1, s->battery ? 1 : 0);
    if(s->has_button) d = patch_bits(d, 30, 1, s->button ? 1U : 0U);
    d = patch_bits(d, 28, 2, wrap_unsigned(s->channel, 2, &wrapped));
    d = patch_bits(d, 16, 12, wrap_unsigned(temperature_raw, 12, &wrapped));
    d = patch_bits(d, 8, 8, wrap_unsigned(s->humidity, 8, &wrapped));
    uint8_t sum = (uint8_t)((d >> 32) + (d >> 24) + (d >> 16) + (d >> 8));
    d = patch_bits(d, 0, 8, sum);
    ((WeatherEditorState*)s)->overflow_wrapped = wrapped;
    *out = d;
    /* Bresser sends 15 identical 40-bit rows. Never extend the low part of
       the final data bit: doing so changes that bit timing and decoders reject
       the row. A separate short pulse + 1000 us row gap closes the row and
       lets the next repetition start with a complete 4-pair preamble. */
    for(uint8_t r = 0; r < 15; r++) {
        for(uint8_t p = 0; p < 4; p++) raw_pair(raw, 750, 750);
        raw_bits_pwm(raw, d, 40, 250, 500, 500, 250);
        raw_pair(raw, 250, 1000);
    }
}

static void encode_auriol(WeatherEditorState const* s, WeatherEditorRaw* raw, uint64_t* out) {
    bool wrapped = false;
    uint64_t d = s->original_data;
    uint32_t t = wrap_unsigned(wrap_signed(s->temperature_tenths, 12, &wrapped), 12, &wrapped);
    /* HG06061A uses 1=LOW directly. The previous inversion encoded OK. */
    d = patch_bits(d, 30, 1, s->battery ? 1 : 0);
    d = patch_bits(
        d, 25, 2, wrap_unsigned(s->channel ? s->channel - 1 : 0, 2, &wrapped));
    d = patch_bits(d, 13, 12, t);
    /* Original Flipper protocol constant is 1110 at bits 8..11. */
    d = patch_bits(d, 8, 4, 0xE);
    d = patch_bits(d, 1, 7, wrap_unsigned(s->humidity, 7, &wrapped));
    ((WeatherEditorState*)s)->overflow_wrapped = wrapped;
    *out = d;
    weather_editor_raw_push(raw, -4000);
    for(uint8_t r = 0; r < 10; r++) {
        raw_bits_ppm(raw, d, 37, 500, 1000, 2000);
        raw_pair(raw, 500, 4000);
    }
    /* Preserve the exact 4 ms row delimiters and terminate the whole burst. */
    raw_pair(raw, 500, 12000);
}

static uint8_t solight_crc_byte(uint64_t d) {
    for(uint16_t c = 0; c < 256; c++) {
        uint64_t t = patch_bits(d, 0, 8, c);
        uint8_t msg[] = {t >> 28, t >> 20, t >> 12, 0xF0, (uint8_t)t};
        if(subghz_protocol_blocks_crc8(msg, 5, 0x31, 0x6c) == 0) return (uint8_t)c;
    }
    return 0;
}

static void encode_solight(WeatherEditorState const* s, WeatherEditorRaw* raw, uint64_t* out) {
    bool wrapped = false;
    uint64_t d = s->original_data;
    uint32_t t = wrap_unsigned(wrap_signed(s->temperature_tenths, 12, &wrapped), 12, &wrapped);
    d = patch_bits(d, 27, 1, s->battery ? 0 : 1);
    d = patch_bits(
        d, 24, 2, wrap_unsigned(s->channel ? s->channel - 1 : 0, 2, &wrapped));
    d = patch_bits(d, 12, 12, t);
    d = patch_bits(d, 8, 4, 0xF);
    d = patch_bits(d, 0, 8, solight_crc_byte(d));
    ((WeatherEditorState*)s)->overflow_wrapped = wrapped;
    *out = d;
    /* 3.50 ms is still a valid rtl_433 row boundary (>3 ms) and remains
       inside the local Solight sync tolerance. It is deliberately outside
       Auriol HG06061A's narrow 4.00 ms sync window, preventing the old false
       Auriol row made from the Solight checksum byte. */
    raw_pair(raw, 500, 3500);
    for(uint8_t r = 0; r < 12; r++) {
        raw_bits_ppm(raw, d, 36, 500, 972, 1932);
        raw_pair(raw, 500, 972); /* documented 37th separator bit = 0 */
        raw_pair(raw, 500, 3500);
    }
}

static void encode_vauno(WeatherEditorState const* s, WeatherEditorRaw* raw, uint64_t* out) {
    bool wrapped = false;
    uint64_t d = s->original_data;
    uint32_t t = wrap_unsigned(wrap_signed(s->temperature_tenths, 12, &wrapped), 12, &wrapped);
    d = patch_bits(d, 33, 1, s->battery ? 1 : 0);
    d = patch_bits(d, 30, 2, wrap_unsigned(s->channel, 2, &wrapped));
    d = patch_bits(d, 18, 12, t);
    d = patch_bits(d, 11, 7, wrap_unsigned(s->humidity, 7, &wrapped));
    d = patch_bits(d, 0, 6, 0);
    uint8_t sum = 0;
    for(uint8_t i = 6; i <= 38; i += 4) sum += (d >> i) & 0x0F;
    d = patch_bits(d, 0, 6, sum & 0x3F);
    ((WeatherEditorState*)s)->overflow_wrapped = wrapped;
    *out = d;
    weather_editor_raw_push(raw, -(1940 * 4));
    for(uint8_t r = 0; r < 8; r++) {
        raw_bits_ppm(raw, d, 42, 500, 1940, 3880);
        raw_pair(raw, 500, 1940 * 4);
    }
}

static void encode_acurite606(WeatherEditorState const* s, WeatherEditorRaw* raw, uint64_t* out) {
    bool wrapped = false;
    uint64_t d = s->original_data;
    uint32_t t = wrap_unsigned(wrap_signed(s->temperature_tenths, 12, &wrapped), 12, &wrapped);
    d = patch_bits(d, 23, 1, s->battery ? 1 : 0);
    d = patch_bits(d, 8, 12, t);
    uint8_t msg[] = {d >> 24, d >> 16, d >> 8};
    d = patch_bits(d, 0, 8, subghz_protocol_blocks_lfsr_digest8(msg, 3, 0x98, 0xF1));
    ((WeatherEditorState*)s)->overflow_wrapped = wrapped;
    *out = d;
    weather_editor_raw_push(raw, -8500);
    for(uint8_t r = 0; r < 8; r++) {
        raw_bits_ppm(raw, d, 32, 500, 2000, 4000);
        raw_pair(raw, 500, 8500);
    }
}

static void encode_acurite609(WeatherEditorState const* s, WeatherEditorRaw* raw, uint64_t* out) {
    bool wrapped = false;
    uint64_t d = s->original_data;
    uint32_t t = wrap_unsigned(wrap_signed(s->temperature_tenths, 12, &wrapped), 12, &wrapped);
    d = patch_bits(d, 31, 1, s->battery ? 1 : 0);
    d = patch_bits(d, 16, 12, t);
    d = patch_bits(d, 8, 8, wrap_unsigned(s->humidity, 8, &wrapped));
    uint8_t sum = (uint8_t)((d >> 32) + (d >> 24) + (d >> 16) + (d >> 8));
    d = patch_bits(d, 0, 8, sum);
    ((WeatherEditorState*)s)->overflow_wrapped = wrapped;
    *out = d;
    weather_editor_raw_push(raw, -8500);
    for(uint8_t r = 0; r < 8; r++) {
        raw_bits_ppm(raw, d, 40, 500, 1000, 2000);
        raw_pair(raw, 500, 8500);
    }
}

static void encode_lacrosse(WeatherEditorState const* s, WeatherEditorRaw* raw, uint64_t* out) {
    bool wrapped = false;
    bool has_extra = s->bit_count == 41;
    uint64_t d = has_extra ? (s->original_data >> 1) : s->original_data;
    d = patch_bits(d, 31, 1, s->battery ? 1 : 0);
    if(s->has_button) d = patch_bits(d, 30, 1, s->button ? 1U : 0U);
    d = patch_bits(
        d, 28, 2, wrap_unsigned(s->channel ? s->channel - 1 : 0, 2, &wrapped));
    d = patch_bits(d, 16, 12, wrap_unsigned(s->temperature_tenths + 500, 12, &wrapped));
    d = patch_bits(d, 8, 8, wrap_unsigned(s->humidity, 8, &wrapped));
    uint8_t msg[] = {d >> 32, d >> 24, d >> 16, d >> 8};
    d = patch_bits(d, 0, 8, subghz_protocol_blocks_lfsr_digest8_reflect(msg, 4, 0x31, 0xF4));
    uint64_t tx = has_extra ? (d << 1) | (s->original_data & 1ULL) : d;
    ((WeatherEditorState*)s)->overflow_wrapped = wrapped;
    *out = tx;
    /* TX141TH-Bv2 packets are sent directly one after another. The first
       832/832 pair after a payload also starts the next packet preamble. */
    for(uint8_t r = 0; r < 12; r++) {
        for(uint8_t p = 0; p < 4; p++) raw_pair(raw, 832, 832);
        raw_bits_pwm(raw, tx, s->bit_count, 208, 417, 417, 208);
    }
    /* Two trailing preamble-width pairs terminate the final packet. */
    raw_pair(raw, 832, 832);
    raw_pair(raw, 832, 832);
}

bool weather_editor_encode(const WeatherEditorState* state, WeatherEditorRaw* out_raw, FuriString* status) {
    if(status) furi_string_reset(status);
    if(!state || !state->protocol_name || !out_raw || !out_raw->values ||
       out_raw->capacity == 0U) {
        if(status) furi_string_set(status, "No data to encode");
        return false;
    }
    weather_editor_raw_reset(out_raw);
    ((WeatherEditorState*)state)->overflow_wrapped = false;
    uint64_t data = state->original_data;

    if(protocol_is(state, "Nexus-TH")) encode_nexus(state, out_raw, &data);
    else if(protocol_is(state, "ThermoPRO-TX4")) encode_thermopro(state, out_raw, &data);
    else if(protocol_is(state, "Bresser-3CH")) encode_bresser(state, out_raw, &data);
    else if(protocol_is(state, "Auriol HG06061")) encode_auriol(state, out_raw, &data);
    else if(protocol_is(state, "Vauno-EN8822C")) encode_vauno(state, out_raw, &data);
    else if(protocol_is(state, "Solight TE44")) encode_solight(state, out_raw, &data);
    else if(protocol_is(state, "TX141THBv2")) encode_lacrosse(state, out_raw, &data);
    else if(protocol_is(state, "Acurite-606TX")) encode_acurite606(state, out_raw, &data);
    else if(protocol_is(state, "Acurite-609TXC")) encode_acurite609(state, out_raw, &data);
    else {
        bool handled = false;
        if(!weather_editor_encode_extended(state, out_raw, &data, &handled, status)) {
            if(!handled && status)
                furi_string_set(status, "RX decoder available\nNo TX editing");
            return false;
        }
    }

    if(out_raw->overflowed) {
        if(status) furi_string_set(status, "TX burst too long");
        return false;
    }

    ((WeatherEditorState*)state)->edited_data = data;
    if(status) {
        if(state->overflow_wrapped)
            furi_string_set(status, "Encoded with overflow");
        else
            furi_string_set(status, "Edited frame ready");
    }
    return out_raw->count > 0;
}

static void weather_editor_u64_to_bytes(uint64_t value, uint8_t bytes[8]) {
    for(size_t i = 0; i < 8; i++) {
        bytes[7 - i] = (uint8_t)(value & 0xFFU);
        value >>= 8;
    }
}

static const char* weather_editor_sub_preset_name(const char* name) {
    if(!name) return "FuriHalSubGhzPresetOok650Async";
    if(!strcmp(name, "AM270")) return "FuriHalSubGhzPresetOok270Async";
    if(!strcmp(name, "AM650")) return "FuriHalSubGhzPresetOok650Async";
    if(!strcmp(name, "FM238")) return "FuriHalSubGhzPreset2FSKDev238Async";
    if(!strcmp(name, "FM476")) return "FuriHalSubGhzPreset2FSKDev476Async";
    if(!strcmp(name, "FM12K")) return "FuriHalSubGhzPreset2FSKDev12KAsync";
    if(!strcmp(name, "CUSTOM")) return "FuriHalSubGhzPresetCustom";
    if(!strncmp(name, "FuriHalSubGhzPreset", 19U)) return name;
    return "FuriHalSubGhzPresetOok650Async";
}

bool weather_editor_save_raw_sub(
    const char* path,
    const SubGhzRadioPreset* preset,
    const WeatherEditorRaw* raw,
    FuriString* status) {
    if(status) furi_string_reset(status);
    if(!path || !path[0] || !preset || !preset->name || !raw || !raw->values ||
       raw->count == 0U) {
        if(status) furi_string_set(status, "No RAW data to save");
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        if(status) furi_string_set(status, "No memory for Storage");
        return false;
    }
    storage_common_mkdir(storage, EXT_PATH("subghz"));
    storage_common_mkdir(storage, WEATHER_EDITOR_SUB_FOLDER);

    FlipperFormat* fff = flipper_format_file_alloc(storage);
    if(!fff) {
        furi_record_close(RECORD_STORAGE);
        if(status) furi_string_set(status, "No memory for SUB file");
        return false;
    }

    bool ok = false;
    do {
        if(!flipper_format_file_open_always(fff, path)) break;
        if(!flipper_format_write_header_cstr(fff, "Flipper SubGhz RAW File", 1)) break;
        if(!flipper_format_write_uint32(fff, "Frequency", &preset->frequency, 1)) break;

        const char* preset_name =
            weather_editor_sub_preset_name(furi_string_get_cstr(preset->name));
        if(!flipper_format_write_string_cstr(fff, "Preset", preset_name)) break;
        if(!strcmp(preset_name, "FuriHalSubGhzPresetCustom")) {
            if(!preset->data || preset->data_size == 0U) {
                if(status) furi_string_set(status, "No CUSTOM preset data");
                break;
            }
            if(!flipper_format_write_string_cstr(fff, "Custom_preset_module", "CC1101"))
                break;
            if(!flipper_format_write_hex(
                   fff, "Custom_preset_data", preset->data, preset->data_size))
                break;
        }
        if(!flipper_format_write_string_cstr(fff, "Protocol", "RAW")) break;

        size_t offset = 0U;
        while(offset < raw->count) {
            const size_t remaining = raw->count - offset;
            const size_t chunk = remaining > 512U ? 512U : remaining;
            if(!flipper_format_write_int32(fff, "RAW_Data", raw->values + offset, chunk))
                break;
            offset += chunk;
        }
        if(offset != raw->count) break;
        ok = true;
    } while(false);

    flipper_format_file_close(fff);
    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);

    if(status) {
        if(ok) furi_string_set(status, "RAW .sub file saved");
        else if(furi_string_size(status) == 0U) furi_string_set(status, ".sub save error");
    }
    return ok;
}

static const char* weather_editor_key_preset_name(const char* preset_name) {
    if(!preset_name) return NULL;
    if(!strcmp(preset_name, "AM270")) return "FuriHalSubGhzPresetOok270Async";
    if(!strcmp(preset_name, "AM650")) return "FuriHalSubGhzPresetOok650Async";
    if(!strcmp(preset_name, "FM238")) return "FuriHalSubGhzPreset2FSKDev238Async";
    if(!strcmp(preset_name, "FM12K")) return "FuriHalSubGhzPreset2FSKDev12KAsync";
    if(!strcmp(preset_name, "FM476")) return "FuriHalSubGhzPreset2FSKDev476Async";
    return preset_name;
}

static const char* weather_editor_short_preset_name(const char* preset_name) {
    if(!preset_name) return NULL;
    if(!strcmp(preset_name, "FuriHalSubGhzPresetOok270Async")) return "AM270";
    if(!strcmp(preset_name, "FuriHalSubGhzPresetOok650Async")) return "AM650";
    if(!strcmp(preset_name, "FuriHalSubGhzPreset2FSKDev238Async")) return "FM238";
    if(!strcmp(preset_name, "FuriHalSubGhzPreset2FSKDev12KAsync")) return "FM12K";
    if(!strcmp(preset_name, "FuriHalSubGhzPreset2FSKDev476Async")) return "FM476";
    if(!strcmp(preset_name, "FuriHalSubGhzPresetCustom")) return "CUSTOM";
    return preset_name;
}

static void weather_editor_append_hex_bytes(FuriString* text, const uint8_t* data, size_t size) {
    if(!text || !data) return;
    for(size_t i = 0; i < size; i++) {
        if(i) furi_string_cat(text, " ");
        furi_string_cat_printf(text, "%02X", data[i]);
    }
}

static void weather_editor_append_hex_u64_le(FuriString* text, uint64_t value, size_t size) {
    if(!text) return;
    for(size_t i = 0; i < size; i++) {
        if(i) furi_string_cat(text, " ");
        furi_string_cat_printf(text, "%02X", (unsigned int)(value & 0xFFU));
        value >>= 8;
    }
}

static void weather_editor_append_temp_line(FuriString* text, int32_t tenths) {
    if(!text) return;
    const bool negative = tenths < 0;
    const int32_t absolute = weather_editor_abs_saturated(tenths);
    furi_string_cat_printf(
        text,
        "Temp: %s%ld.%ld\n",
        negative ? "-" : "",
        (long)(absolute / 10),
        (long)(absolute % 10));
}

bool weather_editor_save_key_sub(
    const char* path,
    const SubGhzRadioPreset* preset,
    const WeatherEditorState* state,
    bool use_edited_data,
    FuriString* status) {
    if(status) furi_string_reset(status);
    if(!path || !path[0] || !preset || !preset->name || !state || !state->protocol_name) {
        if(status) furi_string_set(status, "No .ws data");
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        if(status) furi_string_set(status, "No memory for Storage");
        return false;
    }
    storage_common_mkdir(storage, EXT_PATH("apps_data/weather_editor"));
    storage_common_mkdir(storage, WEATHER_EDITOR_PROFILE_FOLDER);
    storage_common_mkdir(storage, WEATHER_EDITOR_RX_PROFILE_FOLDER);
    storage_common_mkdir(storage, WEATHER_EDITOR_EDITED_PROFILE_FOLDER);

    Stream* stream = file_stream_alloc(storage);
    FuriString* text = furi_string_alloc();
    if(!stream || !text) {
        if(stream) stream_free(stream);
        if(text) furi_string_free(text);
        furi_record_close(RECORD_STORAGE);
        if(status) furi_string_set(status, "No memory for .ws file");
        return false;
    }

    const char* preset_name = weather_editor_key_preset_name(furi_string_get_cstr(preset->name));
    if(!preset_name) {
        stream_free(stream);
        furi_string_free(text);
        furi_record_close(RECORD_STORAGE);
        if(status) furi_string_set(status, "No .ws preset");
        return false;
    }

    furi_string_cat(text, "Filetype: Flipper Weather Station Key File\n");
    furi_string_cat(text, "Version: 1\n");
    furi_string_cat_printf(text, "Frequency: %lu\n", (unsigned long)preset->frequency);
    furi_string_cat_printf(text, "Preset: %s\n", preset_name);
    if(!strcmp(preset_name, "FuriHalSubGhzPresetCustom")) {
        furi_string_cat(text, "Custom_preset_module: CC1101\n");
        if(preset->data && preset->data_size) {
            furi_string_cat(text, "Custom_preset_data: ");
            weather_editor_append_hex_bytes(text, preset->data, preset->data_size);
            furi_string_cat(text, "\n");
        }
    }
    furi_string_cat_printf(
        text, "Protocol: %s\n", furi_string_get_cstr(state->protocol_name));
    furi_string_cat_printf(text, "Id: %lu\n", (unsigned long)state->id);
    furi_string_cat_printf(text, "Bit: %u\n", (unsigned int)state->bit_count);

    uint8_t bytes[8];
    weather_editor_u64_to_bytes(use_edited_data ? state->edited_data : state->original_data, bytes);
    furi_string_cat(text, "Data: ");
    weather_editor_append_hex_bytes(text, bytes, sizeof(bytes));
    furi_string_cat(text, "\n");

    const uint32_t batt = state->battery_kind == WeatherEditorBatteryNone ? 0xFFU : (uint32_t)state->battery;
    const uint32_t hum = state->has_humidity ? (uint32_t)(state->humidity < 0 ? 0 : state->humidity) : 0xFFU;
    const uint32_t ch = state->has_channel ? (uint32_t)state->channel : 0xFFU;
    const uint32_t btn = state->has_button ? (uint32_t)state->button : 0xFFU;
    furi_string_cat_printf(text, "Batt: %lu\n", (unsigned long)batt);
    furi_string_cat_printf(text, "Hum: %lu\n", (unsigned long)hum);
    furi_string_cat_printf(text, "Ts: %lu\n", (unsigned long)furi_hal_rtc_get_timestamp());
    furi_string_cat_printf(text, "Ch: %lu\n", (unsigned long)ch);
    furi_string_cat_printf(text, "Btn: %lu\n", (unsigned long)btn);
    weather_editor_append_temp_line(text, state->has_temperature ? state->temperature_tenths : -2730);

    if(state->var_bits) {
        furi_string_cat_printf(text, "VarBits: %u\n", (unsigned int)state->var_bits);
        furi_string_cat(text, "VarData: ");
        weather_editor_append_hex_u64_le(
            text,
            state->var_data,
            !strcmp(furi_string_get_cstr(state->protocol_name), "Oregon2") ? 4U : 8U);
        furi_string_cat(text, "\n");
    }
    if(state->frame_bits) {
        furi_string_cat_printf(text, "FrameBits: %u\n", (unsigned int)state->frame_bits);
        furi_string_cat(text, "UpperData: ");
        weather_editor_append_hex_u64_le(text, state->frame_upper, 8U);
        furi_string_cat(text, "\nLowerData: ");
        weather_editor_append_hex_u64_le(text, state->frame_lower, 8U);
        furi_string_cat(text, "\n");
    }

    bool ok = false;
    if(file_stream_open(stream, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        const size_t expected = furi_string_size(text);
        const size_t written = stream_write(
            stream,
            (const uint8_t*)furi_string_get_cstr(text),
            expected);
        ok = written == expected;
        file_stream_close(stream);
    }

    stream_free(stream);
    furi_string_free(text);
    furi_record_close(RECORD_STORAGE);
    if(status) {
        if(ok) furi_string_set(status, "Weather .ws saved");
        else furi_string_set(status, "Weather .ws save error");
    }
    return ok;
}

bool weather_editor_save_profile(
    const char* path,
    const SubGhzRadioPreset* preset,
    const WeatherEditorState* state,
    FuriString* status) {
    if(status) furi_string_reset(status);
    if(!path || !path[0] || !preset || !preset->name || !state || !state->protocol_name) {
        if(status) furi_string_set(status, "No profile data");
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        if(status) furi_string_set(status, "No memory for Storage");
        return false;
    }
    storage_common_mkdir(storage, EXT_PATH("apps_data/weather_editor"));
    storage_common_mkdir(storage, WEATHER_EDITOR_PROFILE_FOLDER);
    storage_common_mkdir(storage, WEATHER_EDITOR_RX_PROFILE_FOLDER);
    storage_common_mkdir(storage, WEATHER_EDITOR_EDITED_PROFILE_FOLDER);

    FlipperFormat* fff = flipper_format_file_alloc(storage);
    if(!fff) {
        furi_record_close(RECORD_STORAGE);
        if(status) furi_string_set(status, "No memory for profile");
        return false;
    }
    bool ok = false;
    do {
        if(!flipper_format_file_open_always(fff, path)) break;
        if(!flipper_format_write_header_cstr(fff, "Weather Lab Profile", 1)) break;
        if(!flipper_format_write_string_cstr(
               fff, "Protocol", furi_string_get_cstr(state->protocol_name)))
            break;
        if(!flipper_format_write_uint32(fff, "Frequency", &preset->frequency, 1)) break;
        if(!flipper_format_write_string_cstr(
               fff, "Preset", furi_string_get_cstr(preset->name)))
            break;

        uint32_t value = state->id;
        if(!flipper_format_write_uint32(fff, "Id", &value, 1)) break;
        value = state->bit_count;
        if(!flipper_format_write_uint32(fff, "BitCount", &value, 1)) break;
        value = state->channel;
        if(!flipper_format_write_uint32(fff, "Channel", &value, 1)) break;
        value = (uint32_t)state->battery_kind;
        if(!flipper_format_write_uint32(fff, "BatteryKind", &value, 1)) break;
        value = state->button ? 1U : 0U;
        if(!flipper_format_write_uint32(fff, "Button", &value, 1)) break;

        int32_t signed_value = state->temperature_tenths;
        if(!flipper_format_write_int32(fff, "TemperatureTenths", &signed_value, 1)) break;
        signed_value = state->humidity;
        if(!flipper_format_write_int32(fff, "Humidity", &signed_value, 1)) break;
        signed_value = state->battery;
        if(!flipper_format_write_int32(fff, "Battery", &signed_value, 1)) break;

        uint32_t flags = 0;
        if(state->has_temperature) flags |= 1U << 0;
        if(state->has_humidity) flags |= 1U << 1;
        if(state->has_channel) flags |= 1U << 2;
        if(state->encoder_available) flags |= 1U << 3;
        if(state->has_button) flags |= 1U << 4;
        if(!flipper_format_write_uint32(fff, "Flags", &flags, 1)) break;

        uint8_t bytes[8];
        weather_editor_u64_to_bytes(state->original_data, bytes);
        if(!flipper_format_write_hex(fff, "OriginalData", bytes, sizeof(bytes))) break;
        weather_editor_u64_to_bytes(state->edited_data, bytes);
        if(!flipper_format_write_hex(fff, "EditedData", bytes, sizeof(bytes))) break;

        value = state->var_bits;
        if(!flipper_format_write_uint32(fff, "VarBits", &value, 1)) break;
        weather_editor_u64_to_bytes(state->var_data, bytes);
        if(!flipper_format_write_hex(fff, "VarData", bytes, sizeof(bytes))) break;

        value = state->frame_bits;
        if(!flipper_format_write_uint32(fff, "FrameBits", &value, 1)) break;
        weather_editor_u64_to_bytes(state->frame_upper, bytes);
        if(!flipper_format_write_hex(fff, "FrameUpper", bytes, sizeof(bytes))) break;
        weather_editor_u64_to_bytes(state->frame_lower, bytes);
        if(!flipper_format_write_hex(fff, "FrameLower", bytes, sizeof(bytes))) break;

        if(preset->data && preset->data_size) {
            if(!flipper_format_write_hex(
                   fff, "CustomPresetData", preset->data, preset->data_size))
                break;
        }
        ok = true;
    } while(false);

    flipper_format_file_close(fff);
    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);

    if(status) {
        if(ok) furi_string_set(status, "Data profile saved");
        else if(furi_string_size(status) == 0) furi_string_set(status, "Profile save error");
    }
    return ok;
}


static uint64_t weather_editor_bytes_to_u64(const uint8_t bytes[8]) {
    uint64_t value = 0;
    for(size_t i = 0; i < 8; i++) value = (value << 8) | bytes[i];
    return value;
}

static bool weather_editor_protocol_has_button(const char* protocol) {
    if(!protocol) return false;
    return !strcmp(protocol, "ThermoPRO-TX4") ||
           !strcmp(protocol, "Bresser-3CH") ||
           !strcmp(protocol, "TX141THBv2") ||
           !strcmp(protocol, "GT-WT02") ||
           !strcmp(protocol, "GT-WT03") ||
           !strcmp(protocol, "Auriol AHFL");
}

static uint8_t weather_editor_button_from_original(
    const char* protocol, uint64_t original_data, uint8_t bit_count) {
    if(!protocol) return 0U;
    if(!strcmp(protocol, "ThermoPRO-TX4")) return (uint8_t)((original_data >> 23) & 1ULL);
    if(!strcmp(protocol, "Bresser-3CH")) return (uint8_t)((original_data >> 30) & 1ULL);
    if(!strcmp(protocol, "TX141THBv2")) {
        uint64_t data = bit_count == 41U ? (original_data >> 1) : original_data;
        return (uint8_t)((data >> 30) & 1ULL);
    }
    if(!strcmp(protocol, "GT-WT02")) return (uint8_t)((original_data >> 27) & 1ULL);
    if(!strcmp(protocol, "GT-WT03")) return (uint8_t)((original_data >> 23) & 1ULL);
    if(!strcmp(protocol, "Auriol AHFL")) return (uint8_t)((original_data >> 32) & 1ULL);
    return 0U;
}

bool weather_editor_load_profile(
    const char* path,
    SubGhzRadioPreset* preset,
    WeatherEditorState* state,
    uint8_t** custom_preset_data,
    FuriString* status) {
    if(status) furi_string_reset(status);
    if(!path || !path[0] || !preset || !preset->name || !state || !state->protocol_name ||
       !custom_preset_data) {
        if(status) furi_string_set(status, "No profile data");
        return false;
    }
    *custom_preset_data = NULL;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        if(status) furi_string_set(status, "No memory for Storage");
        return false;
    }
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    FuriString* filetype = furi_string_alloc();
    FuriString* protocol = furi_string_alloc();
    FuriString* preset_name = furi_string_alloc();
    if(!fff || !filetype || !protocol || !preset_name) {
        if(fff) flipper_format_free(fff);
        if(filetype) furi_string_free(filetype);
        if(protocol) furi_string_free(protocol);
        if(preset_name) furi_string_free(preset_name);
        furi_record_close(RECORD_STORAGE);
        if(status) furi_string_set(status, "No memory for profile");
        return false;
    }
    uint8_t* new_preset_data = NULL;
    bool opened = false;
    bool ok = false;

    uint32_t version = 0;
    uint32_t frequency = 0;
    uint32_t id = 0;
    uint32_t bit_count = 0;
    uint32_t channel = 0;
    uint32_t battery_kind = 0;
    uint32_t button = 0;
    bool button_present = false;
    uint32_t flags = 0;
    uint32_t var_bits = 0;
    uint32_t frame_bits = 0;
    int32_t temperature_tenths = 0;
    int32_t humidity = 0;
    int32_t battery = 0;
    uint8_t original_bytes[8] = {0};
    uint8_t edited_bytes[8] = {0};
    uint8_t var_bytes[8] = {0};
    uint8_t upper_bytes[8] = {0};
    uint8_t lower_bytes[8] = {0};

    do {
        opened = flipper_format_file_open_existing(fff, path);
        if(!opened) {
            if(status) furi_string_set(status, "Cannot open profile");
            break;
        }
        if(!flipper_format_read_header(fff, filetype, &version) || version != 1U) {
            if(status) furi_string_set(status, "Invalid Weather file");
            break;
        }

        if(strcmp(furi_string_get_cstr(filetype), "Flipper Weather Station Key File") == 0) {
            flipper_format_rewind(fff);
            if(!flipper_format_read_uint32(fff, "Frequency", &frequency, 1)) break;
            flipper_format_rewind(fff);
            if(!flipper_format_read_string(fff, "Preset", preset_name)) break;
            if(!weather_editor_state_load(state, fff)) break;

            uint32_t preset_count = 0;
            flipper_format_rewind(fff);
            if(flipper_format_get_value_count(fff, "Custom_preset_data", &preset_count) && preset_count) {
                if(preset_count > WEATHER_EDITOR_MAX_PRESET_DATA) {
                    if(status) furi_string_set(status, ".ws preset is too large");
                    break;
                }
                new_preset_data = malloc(preset_count);
                if(!new_preset_data) {
                    if(status) furi_string_set(status, "No memory for preset");
                    break;
                }
                flipper_format_rewind(fff);
                if(!flipper_format_read_hex(fff, "Custom_preset_data", new_preset_data, preset_count)) break;
                preset->data_size = preset_count;
            } else {
                preset->data_size = 0;
            }
            furi_string_set(preset->name, weather_editor_short_preset_name(furi_string_get_cstr(preset_name)));
            preset->frequency = frequency;
            preset->data = new_preset_data;
            *custom_preset_data = new_preset_data;
            new_preset_data = NULL;
            ok = true;
            break;
        }

        if(strcmp(furi_string_get_cstr(filetype), "Weather Lab Profile") != 0) {
            if(status) furi_string_set(status, "Unsupported Weather file");
            break;
        }

        flipper_format_rewind(fff);
        if(!flipper_format_read_string(fff, "Protocol", protocol)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "Frequency", &frequency, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_string(fff, "Preset", preset_name)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "Id", &id, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "BitCount", &bit_count, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "Channel", &channel, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "BatteryKind", &battery_kind, 1)) break;
        flipper_format_rewind(fff);
        button_present = flipper_format_read_uint32(fff, "Button", &button, 1);
        flipper_format_rewind(fff);
        if(!flipper_format_read_int32(fff, "TemperatureTenths", &temperature_tenths, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_int32(fff, "Humidity", &humidity, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_int32(fff, "Battery", &battery, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "Flags", &flags, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_hex(fff, "OriginalData", original_bytes, sizeof(original_bytes))) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_hex(fff, "EditedData", edited_bytes, sizeof(edited_bytes))) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "VarBits", &var_bits, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_hex(fff, "VarData", var_bytes, sizeof(var_bytes))) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_uint32(fff, "FrameBits", &frame_bits, 1)) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_hex(fff, "FrameUpper", upper_bytes, sizeof(upper_bytes))) break;
        flipper_format_rewind(fff);
        if(!flipper_format_read_hex(fff, "FrameLower", lower_bytes, sizeof(lower_bytes))) break;

        uint32_t preset_count = 0;
        flipper_format_rewind(fff);
        if(flipper_format_get_value_count(fff, "CustomPresetData", &preset_count) && preset_count) {
            if(preset_count > WEATHER_EDITOR_MAX_PRESET_DATA) {
                if(status) furi_string_set(status, "Profile preset is too large");
                break;
            }
            new_preset_data = malloc(preset_count);
            if(!new_preset_data) {
                if(status) furi_string_set(status, "No memory for preset");
                break;
            }
            flipper_format_rewind(fff);
            if(!flipper_format_read_hex(fff, "CustomPresetData", new_preset_data, preset_count)) break;
            preset->data_size = preset_count;
        } else {
            preset->data_size = 0;
        }

        furi_string_set(state->protocol_name, protocol);
        state->original_data = weather_editor_bytes_to_u64(original_bytes);
        state->edited_data = weather_editor_bytes_to_u64(edited_bytes);
        state->bit_count = (uint8_t)bit_count;
        state->var_bits = (uint8_t)var_bits;
        state->var_data = weather_editor_bytes_to_u64(var_bytes);
        state->frame_bits = (uint8_t)frame_bits;
        state->frame_upper = weather_editor_bytes_to_u64(upper_bytes);
        state->frame_lower = weather_editor_bytes_to_u64(lower_bytes);
        state->id = id;
        state->temperature_tenths = temperature_tenths;
        /* Starsze profile mogly zawierac ujemna wilgotnosc. Nie koduj jej
           przez zawiniecie pola protokolu - bezpiecznie ustaw 0%. */
        state->humidity = humidity < 0 ? 0 : humidity;
        state->battery = battery;
        state->channel = (uint8_t)channel;
        state->battery_kind = battery_kind <= WeatherEditorBatteryPercent ?
                                  (WeatherEditorBatteryKind)battery_kind :
                                  WeatherEditorBatteryNone;
        state->has_temperature = (flags & (1U << 0)) != 0;
        state->has_humidity = (flags & (1U << 1)) != 0;
        state->has_channel = (flags & (1U << 2)) != 0;
        state->encoder_available = (flags & (1U << 3)) != 0;
        state->has_button = (flags & (1U << 4)) != 0;
        if(button_present) {
            state->button = button ? 1U : 0U;
        } else if(weather_editor_protocol_has_button(furi_string_get_cstr(protocol))) {
            state->has_button = true;
            state->button = weather_editor_button_from_original(
                furi_string_get_cstr(protocol), state->original_data, state->bit_count);
        } else {
            state->button = 0U;
        }
        state->overflow_wrapped = false;

        furi_string_set(preset->name, preset_name);
        preset->frequency = frequency;
        preset->data = new_preset_data;
        *custom_preset_data = new_preset_data;
        new_preset_data = NULL;
        ok = true;
    } while(false);

    if(opened) flipper_format_file_close(fff);
    flipper_format_free(fff);
    furi_string_free(filetype);
    furi_string_free(protocol);
    furi_string_free(preset_name);
    free(new_preset_data);
    furi_record_close(RECORD_STORAGE);

    if(status) {
        if(ok) furi_string_set(status, "Profile loaded");
        else if(furi_string_size(status) == 0) furi_string_set(status, "Profile read error");
    }
    return ok;
}

const char* weather_editor_battery_label(const WeatherEditorState* state) {
    if(!state) return "N/A";
    if(state->battery_kind == WeatherEditorBatteryNone) return "N/A";
    if(state->battery_kind == WeatherEditorBatteryPercent) return "%";
    return state->battery ? "LOW" : "OK";
}
