#include "renault_siemens.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"

#define TAG "RenaultSiemens"

// Siemens VDO keyfob — OOK PWM
// te_short ≈ 250 µs (bit 0 mark)
// te_long  ≈ 500 µs (bit 1 mark)
// Space ≈ te_short
// Gap > 3 × te_long

#define SIEMENS_TE_SHORT  250
#define SIEMENS_TE_LONG   500
#define SIEMENS_TE_DELTA  120
#define SIEMENS_MIN_BITS  64
#define SIEMENS_MAX_BITS  80
#define SIEMENS_GAP_MIN   (SIEMENS_TE_LONG * 3)

typedef enum {
    SiemensStepReset = 0,
    SiemensStepWaitMark,
    SiemensStepWaitSpace,
} SiemensStep;

// ─── Struct ──────────────────────────────────────────────────────────────────

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint64_t data;
    uint8_t bit_count;
    uint8_t parser_step;
} RenaultSiemensDecoder;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static inline uint32_t siemens_abs_diff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static void renault_siemens_extract_fields(RenaultSiemensDecoder* inst) {
    inst->generic.serial = (uint32_t)(inst->generic.data >> 32);
    inst->generic.btn = (uint8_t)((inst->generic.data >> 28) & 0xF);
    inst->generic.cnt = (uint32_t)(inst->generic.data & 0xFFFF);
}

static void renault_siemens_try_accept(RenaultSiemensDecoder* inst) {
    if(inst->bit_count >= SIEMENS_MIN_BITS && inst->bit_count <= SIEMENS_MAX_BITS) {
        inst->generic.data = inst->data;
        inst->generic.data_count_bit = inst->bit_count;
        renault_siemens_extract_fields(inst);
        if(inst->base.callback) {
            inst->base.callback(&inst->base, inst->base.context);
        }
    }
}

// ─── Alloc / Free / Reset ────────────────────────────────────────────────────

static void* renault_siemens_alloc(SubGhzEnvironment* env) {
    UNUSED(env);
    RenaultSiemensDecoder* inst = malloc(sizeof(RenaultSiemensDecoder));
    memset(inst, 0, sizeof(RenaultSiemensDecoder));
    inst->base.protocol = &subghz_protocol_renault_siemens;
    inst->generic.protocol_name = inst->base.protocol->name;
    return inst;
}

static void renault_siemens_free(void* ctx) {
    furi_assert(ctx);
    free(ctx);
}

static void renault_siemens_reset(void* ctx) {
    furi_assert(ctx);
    RenaultSiemensDecoder* inst = ctx;
    inst->data = 0;
    inst->bit_count = 0;
    inst->parser_step = SiemensStepReset;
}

// ─── Feed — OOK PWM decoder ─────────────────────────────────────────────────

static void renault_siemens_feed(void* ctx, bool level, uint32_t duration) {
    furi_assert(ctx);
    RenaultSiemensDecoder* inst = ctx;

    switch(inst->parser_step) {

    case SiemensStepReset:
        if(level) {
            if(siemens_abs_diff(duration, SIEMENS_TE_SHORT) < SIEMENS_TE_DELTA) {
                inst->data = (inst->data << 1);
                inst->bit_count++;
                inst->parser_step = SiemensStepWaitSpace;
            } else if(siemens_abs_diff(duration, SIEMENS_TE_LONG) < SIEMENS_TE_DELTA) {
                inst->data = (inst->data << 1) | 1;
                inst->bit_count++;
                inst->parser_step = SiemensStepWaitSpace;
            }
        }
        break;

    case SiemensStepWaitSpace:
        if(!level) {
            if(siemens_abs_diff(duration, SIEMENS_TE_SHORT) < SIEMENS_TE_DELTA) {
                inst->parser_step = SiemensStepWaitMark;
            } else if(duration >= SIEMENS_GAP_MIN) {
                renault_siemens_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = SiemensStepReset;
            } else {
                renault_siemens_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = SiemensStepReset;
            }
        }
        break;

    case SiemensStepWaitMark:
        if(level) {
            if(siemens_abs_diff(duration, SIEMENS_TE_SHORT) < SIEMENS_TE_DELTA) {
                inst->data = (inst->data << 1);
                inst->bit_count++;
                inst->parser_step = SiemensStepWaitSpace;
            } else if(siemens_abs_diff(duration, SIEMENS_TE_LONG) < SIEMENS_TE_DELTA) {
                inst->data = (inst->data << 1) | 1;
                inst->bit_count++;
                inst->parser_step = SiemensStepWaitSpace;
            } else {
                renault_siemens_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = SiemensStepReset;
            }
        } else {
            if(duration >= SIEMENS_GAP_MIN) {
                renault_siemens_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = SiemensStepReset;
            }
        }
        break;

    default:
        renault_siemens_reset(ctx);
        break;
    }

    if(inst->bit_count > SIEMENS_MAX_BITS) {
        renault_siemens_try_accept(inst);
        inst->data = 0;
        inst->bit_count = 0;
        inst->parser_step = SiemensStepReset;
    }
}

// ─── Hash ────────────────────────────────────────────────────────────────────

static uint8_t renault_siemens_get_hash(void* ctx) {
    furi_assert(ctx);
    RenaultSiemensDecoder* inst = ctx;
    return (uint8_t)(inst->generic.data ^
                     (inst->generic.data >> 8) ^
                     (inst->generic.data >> 16) ^
                     (inst->generic.data >> 24));
}

// ─── Serialize / Deserialize ─────────────────────────────────────────────────

static SubGhzProtocolStatus renault_siemens_serialize(
    void* ctx,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(ctx);
    RenaultSiemensDecoder* inst = ctx;
    return subghz_block_generic_serialize(&inst->generic, flipper_format, preset);
}

static SubGhzProtocolStatus
    renault_siemens_deserialize(void* ctx, FlipperFormat* flipper_format) {
    furi_assert(ctx);
    RenaultSiemensDecoder* inst = ctx;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &inst->generic, flipper_format, SIEMENS_MIN_BITS);
    if(ret == SubGhzProtocolStatusOk) {
        inst->data = inst->generic.data;
        inst->bit_count = inst->generic.data_count_bit;
        renault_siemens_extract_fields(inst);
    }
    return ret;
}

// ─── get_string ──────────────────────────────────────────────────────────────

static void renault_siemens_get_string(void* ctx, FuriString* output) {
    furi_assert(ctx);
    RenaultSiemensDecoder* inst = ctx;

    renault_siemens_extract_fields(inst);

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
        "Sn:%08lX Btn:%X\r\n"
        "Cnt:%04lX\r\n",
        inst->generic.protocol_name,
        inst->generic.data_count_bit,
        (unsigned long long)inst->generic.data,
        (unsigned long)inst->generic.serial,
        (unsigned int)inst->generic.btn,
        (unsigned long)inst->generic.cnt);
}

// ─── Descriptor ──────────────────────────────────────────────────────────────

const SubGhzProtocolDecoder renault_siemens_decoder = {
    .alloc = renault_siemens_alloc,
    .free = renault_siemens_free,
    .feed = renault_siemens_feed,
    .reset = renault_siemens_reset,
    .get_hash_data = renault_siemens_get_hash,
    .serialize = renault_siemens_serialize,
    .deserialize = renault_siemens_deserialize,
    .get_string = renault_siemens_get_string,
};

const SubGhzProtocol subghz_protocol_renault_siemens = {
    .name = SUBGHZ_PROTOCOL_RENAULT_SIEMENS_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &renault_siemens_decoder,
    .encoder = NULL,
};
