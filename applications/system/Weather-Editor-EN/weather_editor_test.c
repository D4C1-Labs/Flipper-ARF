#include "weather_editor_test.h"

#include "protocols/ws_generic.h"
#include <string.h>

static const char* const weather_editor_protocol_test_names[WEATHER_EDITOR_PROTOCOL_TEST_COUNT] = {
    "Nexus-TH",
    "ThermoPRO-TX4",
    "Bresser-3CH",
    "Auriol HG06061",
    "Vauno-EN8822C",
    "Solight TE44",
    "TX141THBv2",
    "Acurite-606TX",
    "Acurite-609TXC",
    "inFactory-TH",
    "GT-WT02",
    "GT-WT03",
    "Acurite-986",
    "LaCrosse_TX",
    "Acurite 592TXR",
    "Ambient_Weather",
    "TX8300",
    "Wendox W6726",
    "Auriol AHFL",
    "Kedsum-TH",
    "Acurite 5n1",
    "EMOS E601x",
    "BL999",
    "Oregon2",
    "Oregon3",
};

const char* weather_editor_protocol_test_name(uint8_t index) {
    if(index >= WEATHER_EDITOR_PROTOCOL_TEST_COUNT) return "?";
    return weather_editor_protocol_test_names[index];
}

static void weather_editor_protocol_test_reset(WeatherEditorState* state) {
    if(!state || !state->protocol_name) return;
    furi_string_reset(state->protocol_name);
    state->original_data = 0;
    state->edited_data = 0;
    state->bit_count = 0;
    state->var_bits = 0;
    state->var_data = 0;
    state->frame_bits = 0;
    state->frame_upper = 0;
    state->frame_lower = 0;
    /* Synthetic test frames intentionally use a random over-the-air ID.
       WS_NO_ID disables only the semantic ID comparison in the local
       loopback; the random ID bits remain present in the transmitted frame. */
    state->id = WS_NO_ID;
    /* Every protocol test uses one common value that is exactly representable
       across all supported temperature encodings, including whole-degree
       Fahrenheit sensors and LaCrosse's +50.0 C decimal offset. 69% also
       avoids the TX8300 >79% pulse-width special case. */
    state->temperature_tenths = 450;
    state->humidity = 69;
    state->battery = 1;
    state->channel = 1;
    state->button = 0;
    state->battery_kind = WeatherEditorBatteryFlag;
    state->has_temperature = true;
    state->has_humidity = true;
    state->has_channel = true;
    state->has_button = false;
    state->encoder_available = true;
    state->overflow_wrapped = false;
}

bool weather_editor_protocol_test_prepare(
    WeatherEditorState* state,
    uint8_t index,
    uint32_t random_value) {
    if(!state || index >= WEATHER_EDITOR_PROTOCOL_TEST_COUNT) return false;

    weather_editor_protocol_test_reset(state);
    furi_string_set(state->protocol_name, weather_editor_protocol_test_names[index]);

    /* Keep IDs unique and recognisable in SDR#: the protocol number is in
       the low bits while the PRNG still changes the remaining bits per sweep. */
    const uint8_t id8 = (uint8_t)(((random_value >> 8) & 0x60U) | (0x80U + index));
    const uint16_t id14 = (uint16_t)(((random_value >> 3) & 0x3F00U) | (0x120U + index));
    const uint16_t id16 = (uint16_t)(((random_value ^ (random_value >> 16)) & 0xFF00U) |
                                     (0x40U + index));

    switch(index) {
    case 0: /* Nexus-TH */
        state->bit_count = 36;
        state->original_data = (uint64_t)id8 << 28;
        break;
    case 1: /* ThermoPRO-TX4 */
        state->bit_count = 37;
        state->original_data = ((uint64_t)0x9U << 33) | ((uint64_t)id8 << 25);
        break;
    case 2: /* Bresser-3CH */
        state->bit_count = 40;
        state->original_data = (uint64_t)id8 << 32;
        break;
    case 3: /* Auriol HG06061 */
        state->bit_count = 37;
        state->original_data = (uint64_t)id8 << 31;
        break;
    case 4: /* Vauno-EN8822C */
        state->bit_count = 42;
        state->original_data = (uint64_t)id8 << 34;
        state->channel = 1;
        break;
    case 5: /* Solight TE44: no humidity field. */
        state->bit_count = 36;
        state->original_data = (uint64_t)id8 << 28;
        state->has_humidity = false;
        break;
    case 6: /* TX141THBv2 */
        state->bit_count = 40;
        state->original_data = (uint64_t)id8 << 32;
        break;
    case 7: /* Acurite-606TX */
        state->bit_count = 32;
        state->original_data = (uint64_t)id8 << 24;
        state->has_humidity = false;
        state->has_channel = false;
        break;
    case 8: /* Acurite-609TXC */
        state->bit_count = 40;
        state->original_data = (uint64_t)id8 << 32;
        state->has_channel = false;
        break;
    case 9: /* inFactory-TH */
        state->bit_count = 40;
        state->original_data = (uint64_t)id8 << 32;
        state->channel = 1;
        break;
    case 10: /* GT-WT02 */
        state->bit_count = 37;
        state->original_data = (uint64_t)id8 << 29;
        break;
    case 11: /* GT-WT03 */
        state->bit_count = 41;
        state->original_data = (uint64_t)id8 << 33;
        break;
    case 12: /* Acurite-986: 45.0 C is exactly 113 F. */
        state->bit_count = 40;
        /* The decoder bit-reverses two identifier bytes. Any random bytes are
           valid and therefore still produce a random 16-bit displayed ID. */
        state->original_data = (uint64_t)id16 << 16;
        state->has_humidity = false;
        state->channel = 1;
        break;
    case 13: /* LaCrosse_TX: separate temperature (0x0) and humidity (0xE) frames. */
        state->bit_count = 44;
        state->original_data = ((uint64_t)0xAU << 36) | ((uint64_t)id8 << 24);
        state->has_channel = false;
        state->battery_kind = WeatherEditorBatteryNone;
        break;
    case 14: /* Acurite 592TXR */
        state->bit_count = 56;
        state->original_data = (uint64_t)id14 << 40;
        break;
    case 15: /* Ambient_Weather: byte 0 is 0x45. */
        state->bit_count = 48;
        state->original_data = ((uint64_t)0x45U << 40) | ((uint64_t)id8 << 32);
        break;
    case 16: /* TX8300: common 69% avoids the >79% pulse quirk. */
        state->bit_count = 72;
        state->original_data = ((uint64_t)(id8 & 0x7FU)) << 12;
        state->channel = 1;
        break;
    case 17: /* Wendox: common 45.0 C stays inside its compact field. */
        state->bit_count = 29;
        /* Only five ID bits exist in the 29-bit Wendox frame.  Bit 4 is
           also the mandatory first payload/sync bit, so keep it set. */
        state->original_data = (uint64_t)(0x10U | (id8 & 0x0FU)) << 24;
        state->has_humidity = false;
        state->has_channel = false;
        break;
    case 18: /* Auriol AHFL */
        state->bit_count = 42;
        state->original_data = (uint64_t)id8 << 34;
        break;
    case 19: /* Kedsum-TH */
        state->bit_count = 42;
        state->original_data = (uint64_t)id8 << 32;
        break;
    case 20: /* Acurite 5n1: message type 0x38 is temperature/humidity */
        state->bit_count = 64;
        state->original_data = ((uint64_t)id14 << 48) | ((uint64_t)0x38U << 40);
        break;
    case 21: /* EMOS E601x: mandatory 0xAAA583 header. */
        state->bit_count = 120;
        state->frame_bits = 120;
        state->frame_upper = ((uint64_t)0xAAA583U << 32) | ((uint64_t)id8 << 24) |
                             (random_value & 0x00FFFFFFU);
        state->frame_lower = ((uint64_t)random_value << 16);
        state->original_data = state->frame_lower >> 16;
        state->channel = 1;
        break;
    case 22: /* BL999 accepts the common 45.0 C / 69% values. */
        state->bit_count = 36;
        state->original_data = ((uint64_t)random_value << 4) & 0xFFFFFFFFFULL;
        /* BL999 channel is encoded inside its random identifier nibbles and is
           not independently editable by the encoder. */
        state->has_channel = false;
        break;
    case 23: /* Oregon2 THGR122N */
        state->bit_count = 32;
        state->var_bits = 24;
        state->original_data = ((uint64_t)0x1D20U << 16) | ((uint64_t)id8 << 4);
        break;
    case 24: /* Oregon3 THGR810-family F024 */
        state->bit_count = 32;
        state->var_bits = 28;
        state->original_data = ((uint64_t)0xF024U << 16) | ((uint64_t)id8 << 4);
        break;
    default:
        return false;
    }

    switch(index) {
    case 1:
    case 2:
    case 6:
    case 10:
    case 11:
    case 18:
        state->has_button = true;
        state->button = 0U;
        break;
    default:
        break;
    }

    state->edited_data = state->original_data;
    return true;
}


static uint64_t weather_editor_sim_patch_bits(
    uint64_t value, uint8_t offset, uint8_t width, uint64_t field) {
    if(width == 0U || width > 64U || offset >= 64U) return value;
    uint64_t mask = width == 64U ? UINT64_MAX : ((1ULL << width) - 1ULL);
    mask <<= offset;
    return (value & ~mask) | ((field << offset) & mask);
}

static uint8_t weather_editor_sim_reverse8(uint8_t value) {
    value = (uint8_t)(((value & 0xF0U) >> 4) | ((value & 0x0FU) << 4));
    value = (uint8_t)(((value & 0xCCU) >> 2) | ((value & 0x33U) << 2));
    return (uint8_t)(((value & 0xAAU) >> 1) | ((value & 0x55U) << 1));
}

static uint8_t weather_editor_sim_reverse_nibble(uint8_t value) {
    value &= 0x0FU;
    return (uint8_t)(((value & 1U) << 3) | ((value & 2U) << 1) |
                     ((value & 4U) >> 1) | ((value & 8U) >> 3));
}

bool weather_editor_simulation_id_editable(uint8_t index) {
    return index < WEATHER_EDITOR_PROTOCOL_TEST_COUNT && index != 23U && index != 24U;
}

uint32_t weather_editor_simulation_id_max(uint8_t index) {
    switch(index) {
    case 12: return 0xFFFFU; /* Acurite-986 */
    case 13: return 0x7FU;   /* LaCrosse TX */
    case 14: return 0x3FFFU; /* Acurite 592TXR */
    case 16: return 0x7FU;   /* TX8300 */
    case 17: return 0x1FU;   /* Wendox: 5 bits */
    case 19: return 0x3FFU;  /* Kedsum: 10 bits */
    case 20: return 0x3FFFU; /* Acurite 5n1 */
    case 23: return 0x1D20U; /* Oregon2 model id */
    case 24: return 0xF024U; /* Oregon3 model id */
    default: return 0xFFU;
    }
}

bool weather_editor_simulation_set_id(
    WeatherEditorState* state, uint8_t index, uint32_t sensor_id) {
    if(!state || index >= WEATHER_EDITOR_PROTOCOL_TEST_COUNT) return false;

    uint64_t d = state->original_data;
    uint32_t effective = sensor_id;
    switch(index) {
    case 0:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 28, 8, effective); break;
    case 1:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 25, 8, effective); break;
    case 2:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 32, 8, effective); break;
    case 3:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 31, 8, effective); break;
    case 4:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 34, 8, effective); break;
    case 5:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 28, 8, effective); break;
    case 6:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 32, 8, effective); break;
    case 7:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 24, 8, effective); break;
    case 8:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 32, 8, effective); break;
    case 9:  effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 32, 8, effective); break;
    case 10: effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 29, 8, effective); break;
    case 11: effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 33, 8, effective); break;
    case 12: {
        effective &= 0xFFFFU;
        const uint8_t hi = weather_editor_sim_reverse8((uint8_t)(effective >> 8));
        const uint8_t lo = weather_editor_sim_reverse8((uint8_t)effective);
        d = weather_editor_sim_patch_bits(d, 24, 8, hi);
        d = weather_editor_sim_patch_bits(d, 16, 8, lo);
        break;
    }
    case 13:
        effective &= 0x7FU;
        d = weather_editor_sim_patch_bits(d, 28, 4, effective >> 3);
        d = weather_editor_sim_patch_bits(d, 24, 4, (effective & 0x07U) << 1);
        break;
    case 14: effective &= 0x3FFFU; d = weather_editor_sim_patch_bits(d, 40, 14, effective); break;
    case 15: effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 32, 8, effective); break;
    case 16: effective &= 0x7FU; d = weather_editor_sim_patch_bits(d, 12, 7, effective); break;
    case 17:
        effective = 0x10U | (effective & 0x0FU);
        d = weather_editor_sim_patch_bits(d, 24, 5, effective);
        break;
    case 18: effective &= 0xFFU; d = weather_editor_sim_patch_bits(d, 34, 8, effective); break;
    case 19: effective &= 0x3FFU; d = weather_editor_sim_patch_bits(d, 32, 10, effective); break;
    case 20: effective &= 0x3FFFU; d = weather_editor_sim_patch_bits(d, 48, 14, effective); break;
    case 21:
        effective &= 0xFFU;
        state->frame_upper = weather_editor_sim_patch_bits(state->frame_upper, 24, 8, effective);
        break;
    case 22: {
        effective &= 0xFFU;
        const uint8_t n0 = weather_editor_sim_reverse_nibble((uint8_t)(effective >> 4));
        const uint8_t n1 = weather_editor_sim_reverse_nibble((uint8_t)effective);
        d = weather_editor_sim_patch_bits(d, 32, 4, n0);
        d = weather_editor_sim_patch_bits(d, 28, 4, n1);
        break;
    }
    case 23:
        effective = 0x1D20U;
        d = weather_editor_sim_patch_bits(d, 16, 16, effective);
        break;
    case 24:
        effective = 0xF024U;
        d = weather_editor_sim_patch_bits(d, 16, 16, effective);
        break;
    default:
        return false;
    }

    state->original_data = d;
    state->edited_data = d;
    state->id = effective;
    return true;
}

bool weather_editor_simulation_prepare(
    WeatherEditorState* state, uint8_t index, uint32_t sensor_id) {
    if(!state || index >= WEATHER_EDITOR_PROTOCOL_TEST_COUNT) return false;
    if(!weather_editor_protocol_test_prepare(state, index, 0x13579BDFUL ^ sensor_id)) return false;
    if(index == 23U) sensor_id = 0x1D20U;
    if(index == 24U) sensor_id = 0xF024U;
    return weather_editor_simulation_set_id(state, index, sensor_id);
}
