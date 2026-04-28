#include "renault_valeo.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"
#include "keeloq_common.h"
#include "../subghz_keystore.h"
#include "../subghz_keystore_i.h"
#include <m-array.h>

#define TAG "RenaultValeo"

// Valeo OOK keyfob — Captur 2017 / Clio IV / PCF7961
// OOK PWM encoding:
//   te_short ≈ 66 µs  → bit 0 (mark)
//   te_long  ≈ 264 µs → bit 1 (mark)
//   Space between bits ≈ te_short (66 µs)
//   Gap between frames > 500 µs
//
// Trama (64-96 bits):
//   [MSB..32] fix: btn[4] + serial[28]
//   [31..0]   hop: 32 bits KeeLoq encrypted

#define VALEO_TE_SHORT  66
#define VALEO_TE_LONG   264
#define VALEO_TE_DELTA  60
#define VALEO_TE_SHORT_ALT 100
#define VALEO_TE_LONG_ALT  400
#define VALEO_TE_DELTA_ALT 80
#define VALEO_MIN_BITS  64
#define VALEO_MAX_BITS  96
#define VALEO_GAP_MIN   500

typedef enum {
    ValeoStepReset = 0,
    ValeoStepWaitMark,
    ValeoStepWaitSpace,
} ValeoStep;

// ─── Struct ──────────────────────────────────────────────────────────────────

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint64_t data;
    uint8_t bit_count;
    uint8_t parser_step;
    SubGhzKeystore* keystore;
    const char* manufacture_name;
} RenaultValeoDecoder;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static inline uint32_t valeo_abs_diff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static bool renault_valeo_is_short(uint32_t duration) {
    return (valeo_abs_diff(duration, VALEO_TE_SHORT) < VALEO_TE_DELTA) ||
           (valeo_abs_diff(duration, VALEO_TE_SHORT_ALT) < VALEO_TE_DELTA_ALT);
}

static bool renault_valeo_is_long(uint32_t duration) {
    return (valeo_abs_diff(duration, VALEO_TE_LONG) < VALEO_TE_DELTA) ||
           (valeo_abs_diff(duration, VALEO_TE_LONG_ALT) < VALEO_TE_DELTA_ALT);
}

static bool renault_valeo_frame_is_plausible(RenaultValeoDecoder* inst) {
    if(inst->bit_count < VALEO_MIN_BITS || inst->bit_count > VALEO_MAX_BITS) {
        return false;
    }
    if((inst->bit_count % 2U) != 0U) {
        return false;
    }

    const uint32_t fix = (uint32_t)(inst->data >> 32U);
    const uint8_t btn = (uint8_t)((fix >> 28U) & 0x0FU);
    const uint32_t serial = fix & 0x0FFFFFFFU;

    return (serial != 0U) && (btn <= 0x0DU);
}

// ─── KeeLoq decode ───────────────────────────────────────────────────────────

static void renault_valeo_decode_keeloq(RenaultValeoDecoder* inst) {
    if(!inst->keystore) return;

    uint32_t fix = (uint32_t)(inst->data >> 32);
    uint32_t hop = (uint32_t)(inst->data & 0xFFFFFFFF);

    uint8_t btn = (fix >> 28) & 0xF;
    uint32_t serial = fix & 0x0FFFFFFF;

    inst->generic.serial = serial;
    inst->generic.btn = btn;
    inst->manufacture_name = "Unknown";

    for
        M_EACH(mf, *subghz_keystore_get_data(inst->keystore), SubGhzKeyArray_t) {
            // Normal Learning (Valeo primary)
            if(mf->type == KEELOQ_LEARNING_NORMAL ||
               mf->type == KEELOQ_LEARNING_UNKNOWN) {
                uint64_t man = subghz_protocol_keeloq_common_normal_learning(fix, mf->key);
                uint32_t decrypt = subghz_protocol_keeloq_common_decrypt(hop, man);
                if((decrypt >> 28) == btn &&
                   ((decrypt >> 16) & 0xFF) == (serial & 0xFF)) {
                    inst->generic.cnt = decrypt & 0xFFFF;
                    inst->manufacture_name = furi_string_get_cstr(mf->name);
                    return;
                }
            }
            // Simple Learning fallback
            if(mf->type == KEELOQ_LEARNING_SIMPLE ||
               mf->type == KEELOQ_LEARNING_UNKNOWN) {
                uint32_t decrypt = subghz_protocol_keeloq_common_decrypt(hop, mf->key);
                if((decrypt >> 28) == btn &&
                   ((decrypt >> 16) & 0xFF) == (serial & 0xFF)) {
                    inst->generic.cnt = decrypt & 0xFFFF;
                    inst->manufacture_name = furi_string_get_cstr(mf->name);
                    return;
                }
            }
        }
}

// ─── Alloc / Free / Reset ────────────────────────────────────────────────────

static void* renault_valeo_alloc(SubGhzEnvironment* env) {
    RenaultValeoDecoder* inst = malloc(sizeof(RenaultValeoDecoder));
    memset(inst, 0, sizeof(RenaultValeoDecoder));
    inst->base.protocol = &subghz_protocol_renault_valeo;
    inst->generic.protocol_name = inst->base.protocol->name;
    inst->keystore = subghz_environment_get_keystore(env);
    inst->manufacture_name = "Unknown";
    return inst;
}

static void renault_valeo_free(void* ctx) {
    furi_assert(ctx);
    free(ctx);
}

static void renault_valeo_reset(void* ctx) {
    furi_assert(ctx);
    RenaultValeoDecoder* inst = ctx;
    inst->data = 0;
    inst->bit_count = 0;
    inst->parser_step = ValeoStepReset;
}

// ─── Feed — OOK PWM ─────────────────────────────────────────────────────────

static void renault_valeo_try_accept(RenaultValeoDecoder* inst) {
    if(renault_valeo_frame_is_plausible(inst)) {
        inst->generic.data = inst->data;
        inst->generic.data_count_bit = inst->bit_count;
        renault_valeo_decode_keeloq(inst);
        if(inst->base.callback) {
            inst->base.callback(&inst->base, inst->base.context);
        }
    }
}

static void renault_valeo_feed(void* ctx, bool level, uint32_t duration) {
    furi_assert(ctx);
    RenaultValeoDecoder* inst = ctx;

    switch(inst->parser_step) {

    case ValeoStepReset:
        if(level) {
            if(renault_valeo_is_short(duration)) {
                inst->data = (inst->data << 1);
                inst->bit_count++;
                inst->parser_step = ValeoStepWaitSpace;
            } else if(renault_valeo_is_long(duration)) {
                inst->data = (inst->data << 1) | 1;
                inst->bit_count++;
                inst->parser_step = ValeoStepWaitSpace;
            }
        }
        break;

    case ValeoStepWaitSpace:
        if(!level) {
            if(renault_valeo_is_short(duration)) {
                inst->parser_step = ValeoStepWaitMark;
            } else if(duration >= VALEO_GAP_MIN) {
                renault_valeo_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = ValeoStepReset;
            } else {
                // Allow some tolerance on space — accept wider spaces as inter-bit
                if(duration < VALEO_GAP_MIN) {
                    inst->parser_step = ValeoStepWaitMark;
                } else {
                    renault_valeo_try_accept(inst);
                    inst->data = 0;
                    inst->bit_count = 0;
                    inst->parser_step = ValeoStepReset;
                }
            }
        }
        break;

    case ValeoStepWaitMark:
        if(level) {
            if(renault_valeo_is_short(duration)) {
                inst->data = (inst->data << 1);
                inst->bit_count++;
                inst->parser_step = ValeoStepWaitSpace;
            } else if(renault_valeo_is_long(duration)) {
                inst->data = (inst->data << 1) | 1;
                inst->bit_count++;
                inst->parser_step = ValeoStepWaitSpace;
            } else {
                renault_valeo_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = ValeoStepReset;
            }
        } else {
            if(duration >= VALEO_GAP_MIN) {
                renault_valeo_try_accept(inst);
                inst->data = 0;
                inst->bit_count = 0;
                inst->parser_step = ValeoStepReset;
            }
        }
        break;

    default:
        renault_valeo_reset(ctx);
        break;
    }

    if(inst->bit_count > VALEO_MAX_BITS) {
        renault_valeo_try_accept(inst);
        inst->data = 0;
        inst->bit_count = 0;
        inst->parser_step = ValeoStepReset;
    }
}

// ─── Hash ────────────────────────────────────────────────────────────────────

static uint8_t renault_valeo_get_hash(void* ctx) {
    furi_assert(ctx);
    RenaultValeoDecoder* inst = ctx;
    return (uint8_t)(inst->generic.data ^
                     (inst->generic.data >> 8) ^
                     (inst->generic.data >> 16) ^
                     (inst->generic.data >> 24));
}

// ─── Serialize / Deserialize ─────────────────────────────────────────────────

static SubGhzProtocolStatus renault_valeo_serialize(
    void* ctx,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(ctx);
    RenaultValeoDecoder* inst = ctx;
    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&inst->generic, flipper_format, preset);
    if(res == SubGhzProtocolStatusOk) {
        if(!flipper_format_write_string_cstr(
               flipper_format, "Manufacture", inst->manufacture_name)) {
            res = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    return res;
}

static SubGhzProtocolStatus
    renault_valeo_deserialize(void* ctx, FlipperFormat* flipper_format) {
    furi_assert(ctx);
    RenaultValeoDecoder* inst = ctx;
    SubGhzProtocolStatus res =
        subghz_block_generic_deserialize_check_count_bit(
            &inst->generic, flipper_format, VALEO_MIN_BITS);
    if(res == SubGhzProtocolStatusOk) {
        inst->data = inst->generic.data;
        inst->bit_count = inst->generic.data_count_bit;

        // Read manufacture name safely
        FuriString* mf = furi_string_alloc();
        if(flipper_format_read_string(flipper_format, "Manufacture", mf)) {
            // Store a copy since mf will be freed
            if(furi_string_size(mf) > 0) {
                inst->manufacture_name = "Loaded";
            }
        }
        furi_string_free(mf);

        // Re-extract fields
        uint32_t fix = (uint32_t)(inst->generic.data >> 32);
        inst->generic.serial = fix & 0x0FFFFFFF;
        inst->generic.btn = (fix >> 28) & 0xF;
    }
    return res;
}

// ─── get_string ──────────────────────────────────────────────────────────────

static void renault_valeo_get_string(void* ctx, FuriString* output) {
    furi_assert(ctx);
    RenaultValeoDecoder* inst = ctx;

    uint32_t fix = (uint32_t)(inst->generic.data >> 32);
    inst->generic.serial = fix & 0x0FFFFFFF;
    inst->generic.btn = (fix >> 28) & 0xF;

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
        "Cnt:%04lX Mf:%s\r\n",
        inst->generic.protocol_name,
        inst->generic.data_count_bit,
        (unsigned long long)inst->generic.data,
        (unsigned long)inst->generic.serial,
        (unsigned int)inst->generic.btn,
        (unsigned long)inst->generic.cnt,
        inst->manufacture_name);
}

// ─── Descriptor ──────────────────────────────────────────────────────────────

static const SubGhzProtocolDecoder renault_valeo_decoder = {
    .alloc = renault_valeo_alloc,
    .free = renault_valeo_free,
    .feed = renault_valeo_feed,
    .reset = renault_valeo_reset,
    .get_hash_data = renault_valeo_get_hash,
    .serialize = renault_valeo_serialize,
    .deserialize = renault_valeo_deserialize,
    .get_string = renault_valeo_get_string,
};

const SubGhzProtocol subghz_protocol_renault_valeo = {
    .name = SUBGHZ_PROTOCOL_RENAULT_VALEO_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &renault_valeo_decoder,
    .encoder = NULL,
};
