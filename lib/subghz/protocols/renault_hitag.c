#include "renault_hitag.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"

#define TAG "RenaultHitag"

// Hitag2 / PCF7936 keyfob — OOK PWM
// te_short ≈ 200 µs (bit 0 mark)
// te_long  ≈ 400 µs (bit 1 mark)
// Space between bits ≈ te_short
// Gap between frames > 3 × te_long

#define HITAG_TE_SHORT  200
#define HITAG_TE_LONG   400
#define HITAG_TE_DELTA  120
#define HITAG_MIN_BITS  40
#define HITAG_MAX_BITS  63
#define HITAG_GAP_MIN   (HITAG_TE_LONG * 3)

typedef enum {
    HitagStepReset = 0,
    HitagStepWaitMark,
    HitagStepWaitSpace,
} HitagStep;

// ─── Struct ──────────────────────────────────────────────────────────────────

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint64_t data;
    uint8_t bit_count;
    uint8_t parser_step;
} RenaultHitagDecoder;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static inline uint32_t hitag_abs_diff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static void renault_hitag_extract_fields(RenaultHitagDecoder* inst) {
    uint8_t total = inst->generic.data_count_bit;
    if(total >= 48) {
        inst->generic.btn = (uint8_t)((inst->generic.data >> (total - 4)) & 0xF);
        inst->generic.serial = (uint32_t)((inst->generic.data >> 16) & 0x0FFFFFFF);
        inst->generic.cnt = (uint32_t)(inst->generic.data & 0xFFFF);
    } else if(total >= 40) {
        inst->generic.btn = (uint8_t)((inst->generic.data >> (total - 4)) & 0xF);
        inst->generic.serial = (uint32_t)((inst->generic.data >> 12) & 0x0FFFFFF);
        inst->generic.cnt = (uint32_t)(inst->generic.data & 0x0FFF);
    } else {
        inst->generic.btn = 0;
        inst->generic.serial = (uint32_t)(inst->generic.data >> 16);
        inst->generic.cnt = (uint32_t)(inst->generic.data & 0xFFFF);
    }
}

static void renault_hitag_try_accept(RenaultHitagDecoder* inst) {
    if(inst->bit_count >= HITAG_MIN_BITS && inst->bit_count <= HITAG_MAX_BITS) {
        inst->generic.data = inst->data;
        inst->generic.data_count_bit = inst->bit_count;
        renault_hitag_extract_fields(inst);
        if(inst->base.callback) {
            inst->base.callback(&inst->base, inst->base.context);
        }
    }
}

// ─── Alloc / Free / Reset ────────────────────────────────────────────────────

static void* renault_hitag_alloc(SubGhzEnvironment* env) {
    UNUSED(env);
    RenaultHitagDecoder* inst = malloc(sizeof(RenaultHitagDecoder));
    memset(inst, 0, sizeof(RenaultHitagDecoder));
    inst->base.protocol = &subghz_protocol_renault_hitag;
    inst->generic.protocol_name = inst->base.protocol->name;
    return inst;
}

static void renault_hitag_free(void* ctx) {
    furi_assert(ctx);
    free(ctx);
}

static void renault_hitag_reset(void* ctx) {
    furi_assert(ctx);
    RenaultHitagDecoder* inst = ctx;
    inst->bit_count = 0;
    inst->data = 0;
    inst->parser_step = HitagStepReset;
}

// ─── Feed — OOK PWM decoder ─────────────────────────────────────────────────

static void renault_hitag_feed(void* ctx, bool level, uint32_t duration) {
    furi_assert(ctx);
    RenaultHitagDecoder* inst = ctx;

    switch(inst->parser_step) {

    case HitagStepReset:
        if(level) {
            if(hitag_abs_diff(duration, HITAG_TE_SHORT) < HITAG_TE_DELTA) {
                inst->data = (inst->data << 1);
                inst->bit_count++;
                inst->parser_step = HitagStepWaitSpace;
            } else if(hitag_abs_diff(duration, HITAG_TE_LONG) < HITAG_TE_DELTA) {
                inst->data = (inst->data << 1) | 1;
                inst->bit_count++;
                inst->parser_step = HitagStepWaitSpace;
            }
        }
        break;

    case HitagStepWaitSpace:
        if(!level) {
            if(hitag_abs_diff(duration, HITAG_TE_SHORT) < HITAG_TE_DELTA) {
                inst->parser_step = HitagStepWaitMark;
            } else if(duration >= HITAG_GAP_MIN) {
                renault_hitag_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = HitagStepReset;
            } else {
                renault_hitag_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = HitagStepReset;
            }
        }
        break;

    case HitagStepWaitMark:
        if(level) {
            if(hitag_abs_diff(duration, HITAG_TE_SHORT) < HITAG_TE_DELTA) {
                inst->data = (inst->data << 1);
                inst->bit_count++;
                inst->parser_step = HitagStepWaitSpace;
            } else if(hitag_abs_diff(duration, HITAG_TE_LONG) < HITAG_TE_DELTA) {
                inst->data = (inst->data << 1) | 1;
                inst->bit_count++;
                inst->parser_step = HitagStepWaitSpace;
            } else {
                renault_hitag_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = HitagStepReset;
            }
        } else {
            if(duration >= HITAG_GAP_MIN) {
                renault_hitag_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = HitagStepReset;
            }
        }
        break;

    default:
        renault_hitag_reset(ctx);
        break;
    }

    if(inst->bit_count > HITAG_MAX_BITS) {
        renault_hitag_try_accept(inst);
        inst->data = 0;
        inst->bit_count = 0;
        inst->parser_step = HitagStepReset;
    }
}

// ─── Hash ────────────────────────────────────────────────────────────────────

static uint8_t renault_hitag_get_hash(void* ctx) {
    furi_assert(ctx);
    RenaultHitagDecoder* inst = ctx;
    return (uint8_t)(inst->generic.data ^
                     (inst->generic.data >> 8) ^
                     (inst->generic.data >> 16) ^
                     (inst->generic.data >> 24));
}

// ─── Serialize / Deserialize ─────────────────────────────────────────────────

static SubGhzProtocolStatus renault_hitag_serialize(
    void* ctx,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(ctx);
    RenaultHitagDecoder* inst = ctx;
    return subghz_block_generic_serialize(&inst->generic, flipper_format, preset);
}

static SubGhzProtocolStatus
    renault_hitag_deserialize(void* ctx, FlipperFormat* flipper_format) {
    furi_assert(ctx);
    RenaultHitagDecoder* inst = ctx;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &inst->generic, flipper_format, HITAG_MIN_BITS);
    if(ret == SubGhzProtocolStatusOk) {
        inst->data = inst->generic.data;
        inst->bit_count = inst->generic.data_count_bit;
        renault_hitag_extract_fields(inst);
    }
    return ret;
}

// ─── get_string ──────────────────────────────────────────────────────────────

static void renault_hitag_get_string(void* ctx, FuriString* output) {
    furi_assert(ctx);
    RenaultHitagDecoder* inst = ctx;

    renault_hitag_extract_fields(inst);

    subghz_block_generic_global.btn_is_available = true;
    subghz_block_generic_global.current_btn = inst->generic.btn;
    subghz_block_generic_global.btn_length_bit = 4;
    subghz_block_generic_global.cnt_is_available = true;
    subghz_block_generic_global.current_cnt = inst->generic.cnt;
    subghz_block_generic_global.cnt_length_bit = 16;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%016llX\r\n"
        "Sn:%07lX Btn:%X\r\n"
        "Cnt:%04lX\r\n",
        inst->generic.protocol_name,
        inst->generic.data_count_bit,
        (unsigned long long)inst->generic.data,
        (unsigned long)inst->generic.serial,
        (unsigned int)inst->generic.btn,
        (unsigned long)inst->generic.cnt);
}

// ─── Descriptor ──────────────────────────────────────────────────────────────

const SubGhzProtocolDecoder renault_hitag_decoder = {
    .alloc = renault_hitag_alloc,
    .free = renault_hitag_free,
    .feed = renault_hitag_feed,
    .reset = renault_hitag_reset,
    .get_hash_data = renault_hitag_get_hash,
    .serialize = renault_hitag_serialize,
    .deserialize = renault_hitag_deserialize,
    .get_string = renault_hitag_get_string,
};

const SubGhzProtocol subghz_protocol_renault_hitag = {
    .name = SUBGHZ_PROTOCOL_RENAULT_HITAG_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &renault_hitag_decoder,
    .encoder = NULL,
};
