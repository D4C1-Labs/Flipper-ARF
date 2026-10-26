#include "mercedes_v0.h"

#include "../blocks/custom_btn_i.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"

#include <stdlib.h>
#include <string.h>

#define MERCEDES_V0_TE_US              500
#define MERCEDES_V0_BITS               112
#define MERCEDES_V0_PREAMBLE_MIN_EDGES 108
#define MERCEDES_V0_PREAMBLE_MAX_EDGES 113
#define MERCEDES_V0_TX_PREAMBLE_BITS   55
#define MERCEDES_V0_TX_START_US        3700
#define MERCEDES_V0_COMMAND_GAP_US     300000
#define MERCEDES_V0_UPLOAD_CAPACITY \
    (2 * (1 + 2 * MERCEDES_V0_TX_PREAMBLE_BITS + 2 + 2 * MERCEDES_V0_BITS) + 1)

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;
    uint64_t hi;
    uint64_t lo;
    uint16_t preamble_edges;
    uint8_t nbits;
    int8_t pair_chip;
    bool active;
    bool sync_pending;
    bool invert;
    bool have_level;
    bool last_level;
} MercedesV0Decoder;

typedef struct {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
} MercedesV0Encoder;

static void mercedes_v0_reset(void* context) {
    MercedesV0Decoder* instance = context;
    instance->hi = 0;
    instance->lo = 0;
    instance->preamble_edges = 0;
    instance->nbits = 0;
    instance->pair_chip = -1;
    instance->active = false;
    instance->sync_pending = false;
    instance->invert = false;
    instance->have_level = false;
}

static void* mercedes_v0_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    MercedesV0Decoder* instance = calloc(1, sizeof(MercedesV0Decoder));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mercedes_v0;
    instance->generic.protocol_name = SUBGHZ_PROTOCOL_MERCEDES_V0_NAME;
    mercedes_v0_reset(instance);
    return instance;
}

static void mercedes_v0_free(void* context) {
    free(context);
}

static bool mercedes_v0_prefix_valid(uint64_t hi) {
    /* Nibble whitelist from the PIC receive classifier, before storage. */
    const uint8_t prefix = hi >> 60;
    return prefix == 0xC || prefix == 0x8 || prefix == 0x0 || prefix == 0xA || prefix == 0x4;
}

static void mercedes_v0_parse_command(SubGhzBlockGeneric* generic) {
    generic->btn = generic->data >> 60;
}

static const char* mercedes_v0_command_name(uint8_t command) {
    switch(command) {
    case 0xC:
        return "Lock";
    case 0x8:
        return "Unlock";
    case 0xA:
        return "??";
    case 0x0:
        return "Follow-up";
    default:
        return "Raw";
    }
}

static bool mercedes_v0_data_valid(const SubGhzBlockGeneric* generic) {
    return generic->data_count_bit == MERCEDES_V0_BITS && (generic->data_2 & 0xFFFF) == 0 &&
           mercedes_v0_prefix_valid(generic->data);
}

static void mercedes_v0_feed(void* context, bool level, uint32_t duration) {
    MercedesV0Decoder* instance = context;
    const uint8_t chips = (duration > 350 && duration < 650)  ? 1 :
                          (duration > 850 && duration < 1150) ? 2 :
                                                                0;
    if(!chips) {
        mercedes_v0_reset(instance);
        return;
    }
    if(instance->have_level && instance->last_level == level) {
        mercedes_v0_reset(instance);
    }
    instance->last_level = level;
    instance->have_level = true;

    if(!instance->active) {
        if(chips == 1) {
            if(instance->preamble_edges <= MERCEDES_V0_PREAMBLE_MAX_EDGES) {
                instance->preamble_edges++;
            }
        } else if(
            instance->preamble_edges >= MERCEDES_V0_PREAMBLE_MIN_EDGES &&
            instance->preamble_edges <= MERCEDES_V0_PREAMBLE_MAX_EDGES) {
            instance->active = true;
            instance->sync_pending = true;
            instance->pair_chip = level;
        } else {
            mercedes_v0_reset(instance);
        }
        return;
    }

    for(uint8_t i = 0; i < chips; i++) {
        if(instance->pair_chip < 0) {
            instance->pair_chip = level;
            continue;
        }
        const bool first = instance->pair_chip;
        instance->pair_chip = -1;
        if(first == level) {
            mercedes_v0_reset(instance);
            return;
        }
        const bool bit = !first && level;
        if(instance->sync_pending) {
            instance->invert = !bit;
            instance->sync_pending = false;
            continue;
        }
        if(bit ^ instance->invert) {
            if(instance->nbits < 64) {
                instance->hi |= 1ULL << (63 - instance->nbits);
            } else {
                instance->lo |= 1ULL << (127 - instance->nbits);
            }
        }
        instance->nbits++;
        if(instance->nbits == MERCEDES_V0_BITS) {
            if(mercedes_v0_prefix_valid(instance->hi)) {
                instance->generic.data = instance->hi;
                instance->generic.data_2 = instance->lo;
                instance->generic.data_count_bit = MERCEDES_V0_BITS;
                mercedes_v0_parse_command(&instance->generic);
                /* [ARF_PORT] Upstream also stores the symbol time in
                 * SubGhzBlockGeneric.te (serialized as "TE"). ARF's generic
                 * block has no such field, and this protocol always sets it to
                 * the same constant and never reads it back, so it is dropped. */
                if(instance->base.callback) {
                    instance->base.callback(&instance->base, instance->base.context);
                }
            }
            mercedes_v0_reset(instance);
            return;
        }
    }
}

static uint8_t mercedes_v0_hash(void* context) {
    MercedesV0Decoder* instance = context;
    uint64_t mix = instance->generic.data ^ instance->generic.data_2;
    return (uint8_t)(mix ^ (mix >> 8) ^ (mix >> 16) ^ (mix >> 24) ^ (mix >> 32) ^ (mix >> 40));
}

static SubGhzProtocolStatus
    mercedes_v0_serialize(void* context, FlipperFormat* format, SubGhzRadioPreset* preset) {
    MercedesV0Decoder* instance = context;
    return subghz_block_generic_serialize(&instance->generic, format, preset);
}

static SubGhzProtocolStatus mercedes_v0_deserialize(void* context, FlipperFormat* format) {
    MercedesV0Decoder* instance = context;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize(&instance->generic, format);
    if(status != SubGhzProtocolStatusOk) return status;
    if(!mercedes_v0_data_valid(&instance->generic)) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.serial = 0;
    mercedes_v0_parse_command(&instance->generic);
    instance->generic.cnt = 0;
    /* [ARF_PORT] generic.te dropped, see note in the decoder feed path. */
    return SubGhzProtocolStatusOk;
}

static void* mercedes_v0_encoder_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    MercedesV0Encoder* instance = calloc(1, sizeof(MercedesV0Encoder));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_mercedes_v0;
    instance->generic.protocol_name = SUBGHZ_PROTOCOL_MERCEDES_V0_NAME;
    instance->encoder.upload = malloc(MERCEDES_V0_UPLOAD_CAPACITY * sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    return instance;
}

static void mercedes_v0_encoder_free(void* context) {
    MercedesV0Encoder* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static void
    mercedes_v0_encoder_append(MercedesV0Encoder* instance, bool level, uint32_t duration) {
    const size_t size = instance->encoder.size_upload;
    LevelDuration* upload = instance->encoder.upload;
    if(size && level_duration_get_level(upload[size - 1]) == level) {
        upload[size - 1] =
            level_duration_make(level, level_duration_get_duration(upload[size - 1]) + duration);
    } else {
        furi_check(size < MERCEDES_V0_UPLOAD_CAPACITY);
        upload[size] = level_duration_make(level, duration);
        instance->encoder.size_upload++;
    }
}

static void mercedes_v0_encoder_burst(MercedesV0Encoder* instance, uint64_t hi) {
    mercedes_v0_encoder_append(instance, false, MERCEDES_V0_TX_START_US);
    for(uint8_t i = 0; i < MERCEDES_V0_TX_PREAMBLE_BITS; i++) {
        mercedes_v0_encoder_append(instance, true, MERCEDES_V0_TE_US);
        mercedes_v0_encoder_append(instance, false, MERCEDES_V0_TE_US);
    }
    mercedes_v0_encoder_append(instance, false, MERCEDES_V0_TE_US);
    mercedes_v0_encoder_append(instance, true, MERCEDES_V0_TE_US);
    for(uint8_t i = 0; i < MERCEDES_V0_BITS; i++) {
        const bool bit = i < 64 ? (hi >> (63 - i)) & 1 :
                                  (instance->generic.data_2 >> (127 - i)) & 1;
        mercedes_v0_encoder_append(instance, !bit, MERCEDES_V0_TE_US);
        mercedes_v0_encoder_append(instance, bit, MERCEDES_V0_TE_US);
    }
}

/* [ARF_PORT] Static button map (ARF style) replacing the upstream runtime
 * subghz_custom_btn_configure_map(). Codes from the upstream "Mercedes V0"
 * entry; labels live in subghz_button_labels.c. Returns false when the pressed
 * D-pad direction is not mapped, so the caller keeps the captured button. */
static bool mercedes_v0_custom_btn_code(uint8_t* code) {
    switch(subghz_custom_btn_get()) {
    case SUBGHZ_CUSTOM_BTN_OK:
        *code = 0x08U; /* Unlock */
        return true;
    case SUBGHZ_CUSTOM_BTN_UP:
        *code = 0x0CU; /* Lock */
        return true;
    case SUBGHZ_CUSTOM_BTN_DOWN:
        *code = 0x0AU; /* unknown function, kept from upstream map */
        return true;
    default:
        return false;
    }
}

static bool mercedes_v0_encoder_upload(MercedesV0Encoder* instance) {
    instance->encoder.size_upload = 0;
    mercedes_v0_parse_command(&instance->generic);
    if((subghz_custom_btn_get_original() == 0)) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    subghz_custom_btn_set_max(SUBGHZ_CUSTOM_BTN_DOWN);

    uint8_t command = instance->generic.btn;
    uint8_t mapped = 0;
    bool selected = mercedes_v0_custom_btn_code(&mapped);
    if(selected) command = mapped;
    if(subghz_block_generic_global_button_override_get(&command)) selected = true;
    if(command > 0xF || !mercedes_v0_prefix_valid((uint64_t)command << 60)) return false;

    const uint64_t tail = instance->generic.data & 0x0FFFFFFFFFFFFFFFULL;
    mercedes_v0_encoder_burst(instance, tail | ((uint64_t)command << 60));
    if(selected && command == 0x8) {
        mercedes_v0_encoder_append(instance, false, MERCEDES_V0_COMMAND_GAP_US);
        mercedes_v0_encoder_burst(instance, tail);
    }
    return true;
}

static SubGhzProtocolStatus
    mercedes_v0_encoder_deserialize(void* context, FlipperFormat* format) {
    MercedesV0Encoder* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    instance->encoder.size_upload = 0;
    instance->encoder.repeat = 0;
    SubGhzProtocolStatus status = subghz_block_generic_deserialize(&instance->generic, format);
    if(status != SubGhzProtocolStatusOk) return status;
    if(!mercedes_v0_data_valid(&instance->generic)) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    if(!flipper_format_rewind(format)) return SubGhzProtocolStatusErrorParserOthers;
    uint32_t repeat = 1;
    flipper_format_read_uint32(format, "Repeat", &repeat, 1);
    if(!mercedes_v0_encoder_upload(instance)) return SubGhzProtocolStatusErrorParserOthers;
    instance->encoder.repeat = repeat;
    instance->encoder.is_running = repeat != 0;
    return SubGhzProtocolStatusOk;
}

static void mercedes_v0_encoder_stop(void* context) {
    MercedesV0Encoder* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.repeat = 0;
    instance->encoder.front = 0;
}

static LevelDuration mercedes_v0_encoder_yield(void* context) {
    MercedesV0Encoder* instance = context;
    if(!instance->encoder.is_running || !instance->encoder.repeat) {
        return level_duration_reset();
    }
    LevelDuration result = instance->encoder.upload[instance->encoder.front++];
    if(instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.front = 0;
        instance->encoder.repeat--;
        if(!instance->encoder.repeat) instance->encoder.is_running = false;
    }
    return result;
}

static void mercedes_v0_get_string(void* context, FuriString* output) {
    MercedesV0Decoder* instance = context;
    mercedes_v0_parse_command(&instance->generic);
    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.btn_length_bit = 4;
    subghz_block_generic_global.current_btn = instance->generic.btn;
    if((subghz_custom_btn_get_original() == 0)) {
        subghz_custom_btn_set_original(instance->generic.btn);
    }
    subghz_custom_btn_set_max(SUBGHZ_CUSTOM_BTN_DOWN);
    subghz_block_generic_global.cnt_is_available = false;
    furi_string_cat_printf(
        output,
        "%s\r\n112bit Btn:%s\r\nKey:%016llX\r\n%012llX",
        instance->generic.protocol_name,
        mercedes_v0_command_name(instance->generic.btn),
        (unsigned long long)instance->generic.data,
        (unsigned long long)(instance->generic.data_2 >> 16));
}

static const SubGhzProtocolDecoder mercedes_v0_decoder = {
    .alloc = mercedes_v0_alloc,
    .free = mercedes_v0_free,
    .feed = mercedes_v0_feed,
    .reset = mercedes_v0_reset,
    .get_hash_data = mercedes_v0_hash,
    .serialize = mercedes_v0_serialize,
    .deserialize = mercedes_v0_deserialize,
    .get_string = mercedes_v0_get_string,
};

static const SubGhzProtocolEncoder mercedes_v0_encoder = {
    .alloc = mercedes_v0_encoder_alloc,
    .free = mercedes_v0_encoder_free,
    .deserialize = mercedes_v0_encoder_deserialize,
    .stop = mercedes_v0_encoder_stop,
    .yield = mercedes_v0_encoder_yield,
};

const SubGhzProtocol subghz_protocol_mercedes_v0 = {
    .name = SUBGHZ_PROTOCOL_MERCEDES_V0_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM | SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &mercedes_v0_decoder,
    .encoder = &mercedes_v0_encoder,
};
