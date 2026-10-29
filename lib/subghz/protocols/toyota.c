#include "toyota.h"

#include "common.h"

#include "../blocks/const.h"
#include "../blocks/custom_btn_i.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"

#include <string.h>

#define TAG "SubGhzProtocolToyota"

#define TOYOTA_PAYLOAD_BYTES     12U
#define TOYOTA_FRAME_BYTES       15U
#define TOYOTA_COUNT_BIT         (TOYOTA_PAYLOAD_BYTES * 8U)
#define TOYOTA_FRAME_BITS        (TOYOTA_FRAME_BYTES * 8U)
#define TOYOTA_REPEAT_BYTES      3U
#define TOYOTA_BURST_COUNT       10U
#define TOYOTA_UPLOAD_MAX        4096U
#define TOYOTA_PRESS_GAP_US      20000U
#define TOYOTA_FOB_PHASE         3U
#define TOYOTA_TICK_US           32U
#define TOYOTA_SHORT_TICK_MIN    18U
#define TOYOTA_SHORT_TICK_MAX    29U
#define TOYOTA_LONG_TICK_MIN     39U
#define TOYOTA_LONG_TICK_MAX     57U
#define TOYOTA_MASK_STRIDE       16U
#define TOYOTA_ESTIMA_XOR        0x05U
#define TOYOTA_RETOUCH_XOR       0x09U
#define TOYOTA_BASE_BUTTON       0xD1U
#define TOYOTA_PROTOCOL_TYPE     31U
#define TOYOTA_US(count)                                                                     \
    ((uint32_t)((((uint32_t)(count) >> 8) & 0xFFU) * 254U +                                  \
                (((((uint32_t)(count) & 0xFFU) != 0U) ? (((uint32_t)(count) & 0xFFU) - 1U) : \
                                                        0U))))

_Static_assert(TOYOTA_US(0x0640) == 1587U, "Toyota long pulse");
_Static_assert(TOYOTA_US(0x0320) == 793U, "Toyota short pulse");
_Static_assert(TOYOTA_COUNT_BIT <= 255U, "min_count_bit_for_found is uint8_t");

static const SubGhzBlockConst subghz_protocol_toyota_const = {
    .te_short = TOYOTA_US(0x0320),
    .te_long = TOYOTA_US(0x0640),
    .te_delta = 185,
    .min_count_bit_for_found = TOYOTA_COUNT_BIT,
};

static const uint8_t toyota_masks[] = {
    0x00, 0x32, 0x41, 0x11, 0x32, 0x42, 0x2D, 0x42, 0x18, 0x41, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x19, 0x41, 0x50, 0x0A, 0x12, 0x00, 0x13, 0x12, 0x51, 0x44, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x4E, 0x00, 0x41, 0x10, 0x06, 0x0D, 0x41, 0x00, 0x19, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2C, 0x41, 0x00, 0x3A, 0x40, 0x08, 0x12, 0x08, 0x4D, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x16, 0x41, 0x41, 0x1A, 0x50, 0x04, 0x03, 0x02, 0x15, 0x4C, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x09, 0x41, 0x41, 0x38, 0x16, 0x2C, 0x43, 0x08, 0x08, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x2B, 0x41, 0x50, 0x1A, 0x06, 0x2D, 0x11, 0x12, 0x04, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x21, 0x41, 0x00, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

typedef struct {
    uint8_t data[TOYOTA_PAYLOAD_BYTES];
    uint8_t selector;
} ToyotaBlock;

typedef struct {
    LevelDuration* samples;
    size_t count;
    size_t max;
    bool level;
    bool have_level;
    uint8_t pause_count;
    uint8_t phase;
    bool carry_phase;
} ToyotaUpload;

struct SubGhzProtocolDecoderToyota {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t frame[TOYOTA_FRAME_BYTES];
    uint8_t bit_count;
    bool short_pending;

    uint8_t reported[TOYOTA_PAYLOAD_BYTES];
    bool have_reported;
};
SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(SubGhzProtocolDecoderToyota);

struct SubGhzProtocolEncoderToyota {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t key[TOYOTA_PAYLOAD_BYTES];
    uint8_t copy[TOYOTA_PAYLOAD_BYTES];
    uint8_t command;
};
SUBGHZ_ASSERT_ENCODER_GENERIC_LAYOUT(SubGhzProtocolEncoderToyota);

static uint8_t toyota_selector(const uint8_t* block) {
    const uint8_t b7 = block[7];
    const uint8_t b8 = block[8];
    const uint8_t b9 = block[9];
    const uint8_t b10 = block[10];
    const uint8_t b11 = block[11];
    uint8_t selector = 0;
    if((b11 & 0x20U) == 0) selector ^= 0x01U;
    if((b11 & 0x04U) != 0) selector ^= 0x02U;
    if((b11 & 0x20U) != 0) selector ^= 0x04U;
    if((b9 & 0x08U) != 0) selector ^= 0x04U;
    if((b8 & 0x08U) != 0) selector ^= 0x04U;
    if((b8 & 0x08U) != 0) selector ^= 0x04U;
    if((b11 & 0x20U) != 0) selector ^= 0x08U;
    if((b11 & 0x04U) != 0) selector ^= 0x08U;
    if((b9 & 0x40U) != 0) selector ^= 0x08U;
    if((b11 & 0x04U) == 0) selector ^= 0x10U;
    if((b10 & 0x40U) != 0) selector ^= 0x10U;
    if((b9 & 0x08U) != 0) selector ^= 0x10U;
    if((b11 & 0x04U) != 0) selector ^= 0x20U;
    if((b9 & 0x40U) != 0) selector ^= 0x20U;
    if((b8 & 0x08U) != 0) selector ^= 0x20U;
    if((b11 & 0x20U) != 0) selector ^= 0x40U;
    if((b9 & 0x40U) != 0) selector ^= 0x40U;
    if((b7 & 0x10U) != 0) selector ^= 0x40U;
    return selector;
}

static void toyota_apply_masks(uint8_t* block, uint8_t selector) {
    for(uint8_t bit = 0; bit < 8U; bit++) {
        if((selector & (uint8_t)(1U << bit)) == 0) continue;
        const uint8_t* mask = &toyota_masks[(size_t)bit * TOYOTA_MASK_STRIDE];
        for(uint8_t index = 0; index < TOYOTA_PAYLOAD_BYTES; index++) {
            block[index] ^= mask[index];
        }
    }
}

static void toyota_transform(ToyotaBlock* block) {
    block->selector = toyota_selector(block->data);
    toyota_apply_masks(block->data, block->selector);
}

static uint32_t toyota_fixed_id(const uint8_t* data) {
    return ((uint32_t)(data[4] & 0x0FU) << 20) | ((uint32_t)(data[6] >> 4) << 16) |
           ((uint32_t)data[8] << 8) | data[11];
}

static uint8_t toyota_counter_mix(uint8_t counter, bool second_half) {
    uint8_t mix = (counter & 1U) ? TOYOTA_RETOUCH_XOR : 0U;
    if(second_half) mix ^= TOYOTA_ESTIMA_XOR;
    return mix;
}

static void toyota_apply_mix(uint8_t* data, uint8_t mix) {
    data[1] ^= mix;
    data[3] ^= mix;
}

static bool toyota_recover(
    const uint8_t* wire,
    uint8_t plain[TOYOTA_PAYLOAD_BYTES],
    uint8_t* counter,
    bool* second_half) {
    ToyotaBlock standard;
    memcpy(standard.data, wire, TOYOTA_PAYLOAD_BYTES);
    toyota_transform(&standard);
    const uint32_t fixed = toyota_fixed_id(standard.data);
    bool found = false;

    for(uint8_t candidate = 0; candidate < 128U; candidate++) {
        for(uint8_t half = 0; half < 2U; half++) {
            uint8_t block[TOYOTA_PAYLOAD_BYTES];
            memcpy(block, wire, sizeof(block));
            toyota_apply_masks(block, candidate);
            toyota_apply_mix(block, toyota_counter_mix(candidate, half != 0U));
            if(toyota_fixed_id(block) != fixed || block[3] != TOYOTA_BASE_BUTTON) continue;
            if(found) return false;
            found = true;
            memcpy(plain, block, sizeof(block));
            *counter = candidate;
            *second_half = half != 0U;
        }
    }
    return found;
}

static void toyota_build_frame(
    uint8_t out[TOYOTA_PAYLOAD_BYTES],
    const uint8_t plain[TOYOTA_PAYLOAD_BYTES],
    uint8_t counter,
    bool second_half) {
    memcpy(out, plain, TOYOTA_PAYLOAD_BYTES);
    toyota_apply_mix(out, toyota_counter_mix(counter, second_half));
    toyota_apply_masks(out, counter);
}

static bool toyota_button_is_estima(uint8_t button) {
    return button == 0xD1U || button == 0xD4U || button == 0x99U || button == 0x9CU;
}

static const char* toyota_button_name(uint8_t button) {
    if(toyota_button_is_estima(button)) return "Estima";
    if(button >= 1U && button <= 3U) return "Cmd";
    return "Unknown";
}

static uint8_t toyota_button_from_payload(const uint8_t* payload) {
    uint8_t plain[TOYOTA_PAYLOAD_BYTES];
    uint8_t counter = 0;
    bool second_half = false;
    if(toyota_recover(payload, plain, &counter, &second_half)) return plain[3];

    ToyotaBlock block;
    memcpy(block.data, payload, TOYOTA_PAYLOAD_BYTES);
    toyota_transform(&block);
    const uint8_t mask = block.data[2] & 0x0EU;
    if(mask & 0x08U) return 3U;
    if(mask & 0x04U) return 2U;
    if(mask & 0x02U) return 1U;
    return toyota_button_is_estima(block.data[3]) ? block.data[3] : 0U;
}

static bool toyota_button_change_valid(uint8_t previous, uint8_t button) {

    if(button >= 1U && button <= 3U) return true;
    if(toyota_button_is_estima(previous)) {
        return button == previous || button == (uint8_t)(previous ^ TOYOTA_ESTIMA_XOR);
    }
    if(previous == 0U && button == 0U) return true;
    return previous <= 3U && button >= 1U && button <= 3U;
}

static bool toyota_change_button(uint8_t* key, uint8_t* copy, uint8_t previous, uint8_t button) {

    UNUSED(key);
    UNUSED(copy);
    return toyota_button_change_valid(previous, button);
}

static bool toyota_upload_add(ToyotaUpload* upload, bool level, uint32_t duration) {
    if(duration == 0) {
        upload->have_level = true;
        upload->level = level;
        return true;
    }
    if(upload->count > 0 &&
       level_duration_get_level(upload->samples[upload->count - 1]) == level) {
        const uint32_t sum =
            level_duration_get_duration(upload->samples[upload->count - 1]) + duration;
        upload->samples[upload->count - 1] = level_duration_make(level, sum);
    } else {
        if(upload->count >= upload->max) {
            FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
            return false;
        }
        upload->samples[upload->count] = level_duration_make(level, duration);
        upload->count++;
    }
    upload->have_level = true;
    upload->level = level;
    return true;
}

static bool toyota_upload_high_low(ToyotaUpload* upload, uint32_t high_us, uint32_t low_us) {
    return toyota_upload_add(upload, true, high_us) && toyota_upload_add(upload, false, low_us);
}

static bool toyota_upload_low_high(ToyotaUpload* upload, uint32_t high_us, uint32_t low_us) {
    return toyota_upload_add(upload, false, low_us) && toyota_upload_add(upload, true, high_us);
}

static bool toyota_encode_payload(ToyotaUpload* upload, const uint8_t* payload, uint8_t framing) {
    uint8_t flag = framing;
    if(!upload->carry_phase) upload->phase = framing == 0U ? TOYOTA_FOB_PHASE : 0U;
    uint8_t state = upload->phase;
    const uint32_t long_us = subghz_protocol_toyota_const.te_long;
    const uint32_t short_us = subghz_protocol_toyota_const.te_short;
    for(uint8_t pass = 0; pass < TOYOTA_BURST_COUNT; pass++) {
        if(flag == 1U) {
            if(!toyota_upload_high_low(upload, long_us, short_us)) return false;
            flag = 2U;
        } else if(flag == 2U) {
            if(!toyota_upload_high_low(upload, long_us, 0)) return false;
            flag = 1U;
        }
        for(uint8_t index = 0; index < TOYOTA_PAYLOAD_BYTES; index++) {
            const uint8_t byte = payload[index];
            for(int bit = 7; bit >= 0; bit--) {
                const bool one = ((byte >> bit) & 1U) != 0;
                if(state == 1U) state = 3U;
                if(state == 0U) {
                    if(one) {
                        if(!toyota_upload_high_low(upload, long_us, 0)) return false;
                        state = 1U;
                    } else {
                        if(!toyota_upload_high_low(upload, short_us, short_us)) return false;
                    }
                }
                if(state == 3U) {
                    if(one) {
                        if(!toyota_upload_low_high(upload, 0, long_us)) return false;
                        state = 0U;
                    } else {
                        if(!toyota_upload_low_high(upload, short_us, short_us)) return false;
                    }
                }
            }
        }
    }
    upload->phase = state;
    return true;
}

static bool toyota_encode_action(
    uint8_t key[TOYOTA_PAYLOAD_BYTES],
    uint8_t copy[TOYOTA_PAYLOAD_BYTES],
    uint8_t command,
    ToyotaUpload* upload) {
    uint8_t plain[TOYOTA_PAYLOAD_BYTES];
    uint8_t primary[TOYOTA_PAYLOAD_BYTES];
    uint8_t second[TOYOTA_PAYLOAD_BYTES];
    uint8_t counter = 0;
    bool second_half = false;
    if(command < 1U || command > 3U) return false;
    if(!toyota_recover(key, plain, &counter, &second_half)) return false;

    counter = (uint8_t)((counter - 1U) & 0x7FU);
    toyota_build_frame(primary, plain, counter, false);
    toyota_build_frame(second, plain, counter, true);

    upload->phase = TOYOTA_FOB_PHASE;
    upload->carry_phase = true;
    const bool encoded = toyota_encode_payload(upload, primary, 0) && upload->count > 0 &&
                         toyota_encode_payload(upload, second, 0);
    upload->carry_phase = false;
    if(!encoded) return false;
    memcpy(key, primary, sizeof(primary));
    memcpy(copy, primary, sizeof(primary));
    return true;
}

static void toyota_publish(SubGhzBlockGeneric* generic, const uint8_t* payload) {
    generic->data = 0;
    generic->data_2 = 0;
    for(size_t i = 0; i < 8U; i++) {
        generic->data = (generic->data << 8) | payload[i];
    }
    for(size_t i = 0; i < 4U; i++) {
        generic->data_2 = (generic->data_2 << 8) | payload[8U + i];
    }
    generic->data_count_bit = subghz_protocol_toyota_const.min_count_bit_for_found;
    generic->type = TOYOTA_PROTOCOL_TYPE;
}

static void toyota_bytes_from_generic(
    const SubGhzBlockGeneric* generic,
    uint8_t out[TOYOTA_PAYLOAD_BYTES]) {
    for(size_t i = 0; i < 8U; i++) {
        out[i] = (uint8_t)(generic->data >> (56U - (8U * i)));
    }
    for(size_t i = 0; i < 4U; i++) {
        out[8U + i] = (uint8_t)(generic->data_2 >> (24U - (8U * i)));
    }
}

static void toyota_generic_from_bytes(SubGhzBlockGeneric* generic, const uint8_t* payload) {
    uint64_t data = 0;
    uint64_t tail = 0;
    for(size_t i = 0; i < 8U; i++) {
        data = (data << 8) | payload[i];
    }
    for(size_t i = 0; i < 4U; i++) {
        tail = (tail << 8) | payload[8U + i];
    }
    generic->data = data;
    generic->data_2 = tail;
    generic->data_count_bit = subghz_protocol_toyota_const.min_count_bit_for_found;
}

static bool toyota_read_button(
    FlipperFormat* format,
    const uint8_t* payload,
    bool have_copy,
    uint8_t* previous,
    uint8_t* button) {
    uint32_t stored = toyota_button_from_payload(payload);
    uint32_t saved = 0;
    if(!flipper_format_rewind(format)) return false;
    if(flipper_format_read_uint32(format, "ToyotaBtn", &saved, 1)) {
        if(saved > 255U || (saved > 3U && !toyota_button_is_estima((uint8_t)saved))) {
            return false;
        }
        if(!have_copy && stored != saved) return false;
        stored = saved;
    }
    *previous = (uint8_t)stored;
    uint32_t selected = stored;
    if(!flipper_format_rewind(format)) return false;
    if(!flipper_format_read_uint32(format, "Btn", &selected, 1)) selected = stored;
    if(selected > 255U || !toyota_button_change_valid(*previous, (uint8_t)selected)) {
        return false;
    }
    *button = (uint8_t)selected;
    return true;
}

static void toyota_setup_editor(uint8_t button) {
    const uint8_t command = (button >= 1U && button <= 3U) ? button : 1U;
    subghz_custom_btn_set_max(4);
    if(subghz_custom_btn_get_original() == 0) subghz_custom_btn_set_original(command);
    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.btn_length_bit = 8;
    subghz_block_generic_global.current_btn = command;
    subghz_block_generic_global.cnt_is_available = false;
}

static bool toyota_write_display(FlipperFormat* flipper_format, uint8_t button) {
    FuriString* display = furi_string_alloc();
    if(!display) return false;
    furi_string_printf(
        display, "%s - %s", SUBGHZ_PROTOCOL_TOYOTA_NAME, toyota_button_name(button));
    const bool ok = flipper_format_rewind(flipper_format) &&
                    flipper_format_insert_or_update_string_cstr(
                        flipper_format, "Disp", furi_string_get_cstr(display));
    furi_string_free(display);
    return ok;
}

static void toyota_accept(SubGhzProtocolDecoderToyota* instance) {
    uint8_t plain[TOYOTA_PAYLOAD_BYTES];
    uint8_t primary[TOYOTA_PAYLOAD_BYTES];
    uint8_t counter = 0;
    bool second_half = false;
    const uint8_t* reported = instance->frame;

    if(toyota_recover(instance->frame, plain, &counter, &second_half)) {
        toyota_build_frame(primary, plain, counter, false);
        toyota_publish(&instance->generic, primary);
        instance->generic.btn = plain[3];
        instance->generic.cnt = counter;
        instance->generic.serial = toyota_fixed_id(plain);
        reported = primary;
    } else {
        toyota_publish(&instance->generic, instance->frame);
        instance->generic.btn = toyota_button_from_payload(instance->frame);
        instance->generic.cnt = 0;
        instance->generic.serial = 0;
    }
    if(instance->have_reported &&
       memcmp(instance->reported, reported, TOYOTA_PAYLOAD_BYTES) == 0) {
        return;
    }
    memcpy(instance->reported, reported, TOYOTA_PAYLOAD_BYTES);
    instance->have_reported = true;
    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

static bool
    toyota_write_state(FlipperFormat* flipper_format, SubGhzProtocolEncoderToyota* instance) {
    uint8_t key[8];
    uint8_t key2[8] = {0};
    memcpy(key, instance->key, sizeof(key));
    memcpy(key2 + 4, instance->key + 8, 4);
    toyota_generic_from_bytes(&instance->generic, instance->key);
    if(!flipper_format_rewind(flipper_format) ||
       !flipper_format_insert_or_update_hex(flipper_format, "Key", key, sizeof(key))) {
        return false;
    }
    if(!flipper_format_rewind(flipper_format) ||
       !flipper_format_insert_or_update_hex(flipper_format, "Key_2", key2, sizeof(key2))) {
        return false;
    }
    if(!flipper_format_rewind(flipper_format) ||
       !flipper_format_insert_or_update_hex(
           flipper_format, "Copy", instance->copy, TOYOTA_PAYLOAD_BYTES)) {
        return false;
    }
    uint32_t button = instance->generic.btn;
    if(!flipper_format_rewind(flipper_format) ||
       !flipper_format_insert_or_update_uint32(flipper_format, "Btn", &button, 1) ||
       !flipper_format_rewind(flipper_format) ||
       !flipper_format_insert_or_update_uint32(flipper_format, "ToyotaBtn", &button, 1)) {
        return false;
    }
    return toyota_write_display(flipper_format, instance->generic.btn);
}

static bool toyota_get_upload(SubGhzProtocolEncoderToyota* instance) {
    furi_check(instance);
    furi_check(instance->encoder.upload);

    ToyotaUpload upload = {
        .samples = instance->encoder.upload,
        .max = TOYOTA_UPLOAD_MAX,
    };
    uint8_t key[TOYOTA_PAYLOAD_BYTES];
    uint8_t copy[TOYOTA_PAYLOAD_BYTES];
    memcpy(key, instance->key, sizeof(key));
    memcpy(copy, instance->copy, sizeof(copy));
    if(!toyota_encode_action(key, copy, instance->command, &upload) || upload.count == 0) {
        return false;
    }
    memcpy(instance->key, key, sizeof(key));
    memcpy(instance->copy, copy, sizeof(copy));
    instance->encoder.size_upload = upload.count;
    instance->encoder.front = 0;
    return true;
}

static LevelDuration subghz_protocol_encoder_toyota_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderToyota* instance = context;
    if(instance->encoder.repeat == 0 || !instance->encoder.is_running ||
       instance->encoder.size_upload == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];
    if(++instance->encoder.front == instance->encoder.size_upload) {
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0;
    }
    return ret;
}

const SubGhzProtocolDecoder subghz_protocol_toyota_decoder = {
    .alloc = subghz_protocol_decoder_toyota_alloc,
    .free = subghz_protocol_decoder_common_free,
    .reset = subghz_protocol_decoder_toyota_reset,
    .feed = subghz_protocol_decoder_toyota_feed,
    .get_hash_data = subghz_protocol_decoder_toyota_get_hash_data,
    .serialize = subghz_protocol_decoder_toyota_serialize,
    .deserialize = subghz_protocol_decoder_toyota_deserialize,
    .get_string = subghz_protocol_decoder_toyota_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_toyota_encoder = {
    .alloc = subghz_protocol_encoder_toyota_alloc,
    .free = subghz_protocol_encoder_common_free,
    .deserialize = subghz_protocol_encoder_toyota_deserialize,
    .stop = subghz_protocol_encoder_common_stop,
    .yield = subghz_protocol_encoder_toyota_yield,
};

const SubGhzProtocol subghz_protocol_toyota = {
    .name = SUBGHZ_PROTOCOL_TOYOTA_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_toyota_decoder,
    .encoder = &subghz_protocol_toyota_encoder,
};

void* subghz_protocol_decoder_toyota_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderToyota* instance = calloc(1, sizeof(SubGhzProtocolDecoderToyota));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_toyota;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_toyota_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderToyota* instance = context;
    memset(&instance->decoder, 0, sizeof(instance->decoder));
    memset(instance->frame, 0, sizeof(instance->frame));
    instance->bit_count = 0;
    instance->short_pending = false;
}

static void toyota_add_bit(SubGhzProtocolDecoderToyota* instance, bool bit) {
    if(instance->bit_count == TOYOTA_FRAME_BITS) {
        for(size_t i = 0; i < TOYOTA_FRAME_BYTES - 1U; i++) {
            instance->frame[i] =
                (uint8_t)((instance->frame[i] << 1) | (instance->frame[i + 1U] >> 7));
        }
        instance->frame[TOYOTA_FRAME_BYTES - 1U] <<= 1;
        instance->bit_count--;
    }
    subghz_protocol_blocks_set_bit_array(
        bit, instance->frame, instance->bit_count++, TOYOTA_FRAME_BYTES);
    if(instance->bit_count == TOYOTA_FRAME_BITS && instance->frame[0] == 0U &&
       (instance->frame[1] & 0x80U) != 0U &&
       memcmp(instance->frame, instance->frame + TOYOTA_PAYLOAD_BYTES, TOYOTA_REPEAT_BYTES) == 0) {
        toyota_accept(instance);
    }
}

static bool toyota_merged_preamble(uint32_t duration) {
    const uint32_t short_us = subghz_protocol_toyota_const.te_short;
    const uint32_t long_us = subghz_protocol_toyota_const.te_long;
    const uint32_t delta = subghz_protocol_toyota_const.te_delta;
    const uint32_t merged[] = {long_us + short_us, 2U * long_us, 2U * long_us + short_us};
    for(size_t i = 0; i < COUNT_OF(merged); i++) {
        if(duration >= merged[i] - delta && duration <= merged[i] + delta) return true;
    }
    return false;
}

static bool toyota_zero_prefix(const SubGhzProtocolDecoderToyota* instance) {
    if(instance->bit_count < 8U) return false;
    for(size_t bit = instance->bit_count - 8U; bit < instance->bit_count; bit++) {
        if((instance->frame[bit / 8U] >> (7U - (bit % 8U))) & 1U) return false;
    }
    return true;
}

void subghz_protocol_decoder_toyota_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    UNUSED(level);
    SubGhzProtocolDecoderToyota* instance = context;

    const uint32_t ticks = duration / TOYOTA_TICK_US;
    const bool short_pulse = ticks > TOYOTA_SHORT_TICK_MIN && ticks <= TOYOTA_SHORT_TICK_MAX;
    const bool long_pulse = (ticks > TOYOTA_LONG_TICK_MIN && ticks <= TOYOTA_LONG_TICK_MAX) ||
                            toyota_merged_preamble(duration);

    if(!short_pulse && !long_pulse) {

        if(duration >= TOYOTA_PRESS_GAP_US) instance->have_reported = false;
        subghz_protocol_decoder_toyota_reset(instance);
        return;
    }
    if(short_pulse) {
        if(instance->short_pending) toyota_add_bit(instance, false);
        instance->short_pending = !instance->short_pending;
    } else {
        if(instance->short_pending) {

            if(!toyota_zero_prefix(instance)) subghz_protocol_decoder_toyota_reset(instance);
            instance->short_pending = false;
        }
        toyota_add_bit(instance, true);
    }
}

uint8_t subghz_protocol_decoder_toyota_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderToyota* instance = context;

    uint8_t hash = 0;
    uint64_t data = instance->generic.data;
    uint32_t tail = (uint32_t)instance->generic.data_2;
    for(size_t i = 0; i < 8U; i++, data >>= 8) {
        hash = (uint8_t)(hash * 33U) ^ (uint8_t)data;
    }
    for(size_t i = 0; i < 4U; i++, tail >>= 8) {
        hash = (uint8_t)(hash * 33U) ^ (uint8_t)tail;
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_toyota_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderToyota* instance = context;
    if(instance->generic.data_count_bit != subghz_protocol_toyota_const.min_count_bit_for_found) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    SubGhzProtocolStatus status =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(status != SubGhzProtocolStatusOk) return status;

    if(instance->generic.data_2 == 0) {
        const uint8_t tail[8] = {0};
        if(!flipper_format_rewind(flipper_format) ||
           !flipper_format_insert_or_update_hex(flipper_format, "Key_2", tail, sizeof(tail))) {
            return SubGhzProtocolStatusErrorParserKey;
        }
    }

    uint32_t button = instance->generic.btn;
    if(!flipper_format_rewind(flipper_format) ||
       !flipper_format_insert_or_update_uint32(flipper_format, "Btn", &button, 1) ||
       !flipper_format_rewind(flipper_format) ||
       !flipper_format_insert_or_update_uint32(flipper_format, "ToyotaBtn", &button, 1) ||
       !toyota_write_display(flipper_format, instance->generic.btn)) {
        return SubGhzProtocolStatusErrorParserKey;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_toyota_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderToyota* instance = context;
    uint8_t tail[8];
    uint8_t copy[TOYOTA_PAYLOAD_BYTES];
    if(!flipper_format_rewind(flipper_format)) return SubGhzProtocolStatusErrorParserKey;
    const bool have_copy = flipper_format_read_hex(flipper_format, "Copy", copy, sizeof(copy));
    if(!flipper_format_rewind(flipper_format) ||
       !flipper_format_read_hex(flipper_format, "Key_2", tail, sizeof(tail))) {
        return SubGhzProtocolStatusErrorParserKey;
    }
    if(tail[0] || tail[1] || tail[2] || tail[3]) return SubGhzProtocolStatusErrorParserKey;

    SubGhzBlockGeneric generic = {.protocol_name = SUBGHZ_PROTOCOL_TOYOTA_NAME};
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &generic, flipper_format, subghz_protocol_toyota_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;
    if(!have_copy && ((generic.data >> 56) != 0 || !(generic.data & (1ULL << 55)))) {
        return SubGhzProtocolStatusErrorParserKey;
    }

    uint8_t payload[TOYOTA_PAYLOAD_BYTES];
    uint8_t previous = 0;
    uint8_t button = 0;
    toyota_bytes_from_generic(&generic, payload);
    if(!toyota_read_button(flipper_format, payload, have_copy, &previous, &button)) {
        return SubGhzProtocolStatusErrorParserKey;
    }
    instance->generic = (SubGhzBlockGeneric){
        .protocol_name = SUBGHZ_PROTOCOL_TOYOTA_NAME,
        .data = generic.data,
        .data_2 = generic.data_2,
        .data_count_bit = subghz_protocol_toyota_const.min_count_bit_for_found,
        .type = TOYOTA_PROTOCOL_TYPE,
        .btn = button,
    };
    subghz_protocol_decoder_toyota_reset(instance);
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_toyota_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderToyota* instance = context;

    uint8_t wire[TOYOTA_PAYLOAD_BYTES];
    uint8_t plain[TOYOTA_PAYLOAD_BYTES];
    uint8_t counter = 0;
    bool second_half = false;
    toyota_bytes_from_generic(&instance->generic, wire);
    const bool known = toyota_recover(wire, plain, &counter, &second_half);

    ToyotaBlock block;
    memcpy(block.data, wire, sizeof(wire));
    toyota_transform(&block);

    const uint32_t fixed = toyota_fixed_id(known ? plain : block.data);
    const uint8_t button = known ? plain[3] : block.data[3];
    toyota_setup_editor(instance->generic.btn);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "Key:%08lX%08lX%08lX\r\n"
        "Sn:%06lX Btn:%02X",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (unsigned long)(uint32_t)(instance->generic.data >> 32),
        (unsigned long)(uint32_t)instance->generic.data,
        (unsigned long)(uint32_t)instance->generic.data_2,
        (unsigned long)fixed,
        button);
    if(known) {
        furi_string_cat_printf(output, "\r\nCnt:%02X", counter);
    }
}

void* subghz_protocol_encoder_toyota_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderToyota* instance = subghz_protocol_encoder_common_alloc(
        sizeof(SubGhzProtocolEncoderToyota), &subghz_protocol_toyota, 1, TOYOTA_UPLOAD_MAX);
    furi_check(instance);
    memset(instance->key, 0, sizeof(instance->key));
    memset(instance->copy, 0, sizeof(instance->copy));
    instance->command = 0;
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_toyota_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderToyota* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.repeat = 1;
    instance->encoder.size_upload = 0;

    uint8_t tail[8];
    if(!flipper_format_rewind(flipper_format) ||
       !flipper_format_read_hex(flipper_format, "Key_2", tail, sizeof(tail))) {
        return SubGhzProtocolStatusErrorParserKey;
    }
    if(tail[0] || tail[1] || tail[2] || tail[3]) return SubGhzProtocolStatusErrorParserKey;

    SubGhzBlockGeneric generic = {.protocol_name = SUBGHZ_PROTOCOL_TOYOTA_NAME};
    SubGhzProtocolStatus status = subghz_block_generic_deserialize_check_count_bit(
        &generic, flipper_format, subghz_protocol_toyota_const.min_count_bit_for_found);
    if(status != SubGhzProtocolStatusOk) return status;

    instance->generic = generic;
    instance->generic.protocol_name = SUBGHZ_PROTOCOL_TOYOTA_NAME;
    instance->generic.type = TOYOTA_PROTOCOL_TYPE;
    toyota_bytes_from_generic(&instance->generic, instance->key);
    if(!flipper_format_rewind(flipper_format)) return SubGhzProtocolStatusErrorParserKey;
    const bool have_copy =
        flipper_format_read_hex(flipper_format, "Copy", instance->copy, TOYOTA_PAYLOAD_BYTES);
    if(!have_copy) {
        memcpy(instance->copy, instance->key, TOYOTA_PAYLOAD_BYTES);
    }

    uint8_t previous = 0;
    uint8_t button = 0;
    if(!toyota_read_button(flipper_format, instance->key, have_copy, &previous, &button)) {
        return SubGhzProtocolStatusErrorParserKey;
    }
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_UP:
        button = 1U;
        break;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        button = 2U;
        break;
    case SUBGHZ_CUSTOM_BTN_LEFT:
        button = 3U;
        break;
    case SUBGHZ_CUSTOM_BTN_RIGHT:
        button = 3U;
        break;
    default:
        break; /* Keep the original stored command */
    }
    subghz_block_generic_global_button_override_get(&button);
    if(!toyota_change_button(instance->key, instance->copy, previous, button)) {
        return SubGhzProtocolStatusErrorParserKey;
    }
    instance->generic.btn = button;
    instance->command = (toyota_button_is_estima(button) || button == 0U) ? 1U : button;

    uint32_t repeat = 1;
    if(flipper_format_rewind(flipper_format)) {
        flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    }
    instance->encoder.repeat = repeat ? repeat : 1;

    if(!toyota_get_upload(instance) || !toyota_write_state(flipper_format, instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}
