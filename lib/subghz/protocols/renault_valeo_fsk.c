#include "renault_valeo_fsk.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"
#include "keeloq_common.h"
#include "../subghz_keystore.h"
#include "../subghz_keystore_i.h"
#include <lib/toolbox/manchester_decoder.h>
#include <m-array.h>

#define TAG "RenaultValeoFSK"

// Valeo FSK (Megane III, Scenic III, Ren3) — 2FSKDev476Async
// Manchester encoding over FSK
// te_short = 500 µs (half-bit cell)
// te_long  = 1000 µs (full-bit cell)
// te_delta = 200 µs
// Preamble: alternating half-cells (min 8)

#define VALEO_FSK_TE_SHORT   500
#define VALEO_FSK_TE_LONG    1000
#define VALEO_FSK_TE_DELTA   200
#define VALEO_FSK_TE_SHORT_ALT 400
#define VALEO_FSK_TE_LONG_ALT  800
#define VALEO_FSK_TE_DELTA_ALT 180
#define VALEO_FSK_MIN_BITS   64
#define VALEO_FSK_MAX_BITS   96
#define VALEO_FSK_PREAMBLE_MIN 8

#ifndef DURATION_DIFF
#define DURATION_DIFF(x, y) (((x) > (y)) ? ((x) - (y)) : ((y) - (x)))
#endif

typedef enum {
    ValeoFSKStepReset = 0,
    ValeoFSKStepPreamble,
    ValeoFSKStepDecode,
} ValeoFSKStep;

// ─── Struct ──────────────────────────────────────────────────────────────────

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint64_t data;
    uint8_t bit_count;
    uint8_t preamble_count;
    ManchesterState manchester_state;
    SubGhzKeystore* keystore;
    const char* manufacture_name;
} RenaultValeoFSKDecoder;

static bool renault_valeo_fsk_frame_is_plausible(RenaultValeoFSKDecoder* inst) {
    if(inst->bit_count < VALEO_FSK_MIN_BITS || inst->bit_count > VALEO_FSK_MAX_BITS) {
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

static void renault_valeo_fsk_decode_keeloq(RenaultValeoFSKDecoder* inst) {
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

// ─── Accept helper ───────────────────────────────────────────────────────────

static void renault_valeo_fsk_try_accept(RenaultValeoFSKDecoder* inst) {
    if(renault_valeo_fsk_frame_is_plausible(inst)) {
        inst->generic.data = inst->data;
        inst->generic.data_count_bit = inst->bit_count;
        renault_valeo_fsk_decode_keeloq(inst);
        if(inst->base.callback) {
            inst->base.callback(&inst->base, inst->base.context);
        }
    }
}

// ─── Alloc / Free / Reset ────────────────────────────────────────────────────

static void* renault_valeo_fsk_alloc(SubGhzEnvironment* env) {
    RenaultValeoFSKDecoder* inst = malloc(sizeof(RenaultValeoFSKDecoder));
    memset(inst, 0, sizeof(RenaultValeoFSKDecoder));
    inst->base.protocol = &subghz_protocol_renault_valeo_fsk;
    inst->generic.protocol_name = inst->base.protocol->name;
    inst->keystore = subghz_environment_get_keystore(env);
    inst->manufacture_name = "Unknown";
    inst->manchester_state = ManchesterStateMid1;
    inst->decoder.parser_step = ValeoFSKStepReset;
    return inst;
}

static void renault_valeo_fsk_free(void* ctx) {
    furi_assert(ctx);
    free(ctx);
}

static void renault_valeo_fsk_reset(void* ctx) {
    furi_assert(ctx);
    RenaultValeoFSKDecoder* inst = ctx;
    inst->data = 0;
    inst->bit_count = 0;
    inst->preamble_count = 0;
    inst->manchester_state = ManchesterStateMid1;
    inst->decoder.parser_step = ValeoFSKStepReset;
}

// ─── Feed — Manchester over FSK ──────────────────────────────────────────────

static void renault_valeo_fsk_feed(void* ctx, bool level, uint32_t duration) {
    furi_assert(ctx);
    RenaultValeoFSKDecoder* inst = ctx;

    // Classify duration
    ManchesterEvent event = ManchesterEventReset;

    if((DURATION_DIFF(duration, VALEO_FSK_TE_SHORT) < VALEO_FSK_TE_DELTA) ||
       (DURATION_DIFF(duration, VALEO_FSK_TE_SHORT_ALT) < VALEO_FSK_TE_DELTA_ALT)) {
        event = level ? ManchesterEventShortHigh : ManchesterEventShortLow;
    } else if((DURATION_DIFF(duration, VALEO_FSK_TE_LONG) < VALEO_FSK_TE_DELTA) ||
              (DURATION_DIFF(duration, VALEO_FSK_TE_LONG_ALT) < VALEO_FSK_TE_DELTA_ALT)) {
        event = level ? ManchesterEventLongHigh : ManchesterEventLongLow;
    } else {
        // Out of range — gap or noise
        renault_valeo_fsk_try_accept(inst);
        renault_valeo_fsk_reset(ctx);
        return;
    }

    switch(inst->decoder.parser_step) {

    case ValeoFSKStepReset:
        if(event == ManchesterEventShortHigh || event == ManchesterEventShortLow) {
            inst->preamble_count = 1;
            inst->decoder.parser_step = ValeoFSKStepPreamble;
        }
        break;

    case ValeoFSKStepPreamble:
        if(event == ManchesterEventShortHigh || event == ManchesterEventShortLow) {
            inst->preamble_count++;
            if(inst->preamble_count >= VALEO_FSK_PREAMBLE_MIN) {
                inst->data = 0;
                inst->bit_count = 0;
                inst->manchester_state = ManchesterStateMid1;
                inst->decoder.parser_step = ValeoFSKStepDecode;
            }
        } else {
            renault_valeo_fsk_reset(ctx);
        }
        break;

    case ValeoFSKStepDecode: {
        bool bit_out = false;
        ManchesterState next_state;

        if(manchester_advance(
               inst->manchester_state, event, &next_state, &bit_out)) {
            inst->data = (inst->data << 1) | (bit_out ? 1 : 0);
            inst->bit_count++;

            if(inst->bit_count >= VALEO_FSK_MAX_BITS) {
                renault_valeo_fsk_try_accept(inst);
                renault_valeo_fsk_reset(ctx);
                return;
            }
        }
        inst->manchester_state = next_state;
        break;
    }

    default:
        renault_valeo_fsk_reset(ctx);
        break;
    }
}

// ─── Hash ────────────────────────────────────────────────────────────────────

static uint8_t renault_valeo_fsk_get_hash(void* ctx) {
    furi_assert(ctx);
    RenaultValeoFSKDecoder* inst = ctx;
    return (uint8_t)(inst->generic.data ^
                     (inst->generic.data >> 8) ^
                     (inst->generic.data >> 16) ^
                     (inst->generic.data >> 24));
}

// ─── Serialize / Deserialize ─────────────────────────────────────────────────

static SubGhzProtocolStatus renault_valeo_fsk_serialize(
    void* ctx,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(ctx);
    RenaultValeoFSKDecoder* inst = ctx;
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
    renault_valeo_fsk_deserialize(void* ctx, FlipperFormat* flipper_format) {
    furi_assert(ctx);
    RenaultValeoFSKDecoder* inst = ctx;
    SubGhzProtocolStatus res =
        subghz_block_generic_deserialize_check_count_bit(
            &inst->generic, flipper_format, VALEO_FSK_MIN_BITS);
    if(res == SubGhzProtocolStatusOk) {
        inst->data = inst->generic.data;
        inst->bit_count = inst->generic.data_count_bit;

        FuriString* mf = furi_string_alloc();
        if(flipper_format_read_string(flipper_format, "Manufacture", mf)) {
            if(furi_string_size(mf) > 0) {
                inst->manufacture_name = "Loaded";
            }
        }
        furi_string_free(mf);

        uint32_t fix = (uint32_t)(inst->generic.data >> 32);
        inst->generic.serial = fix & 0x0FFFFFFF;
        inst->generic.btn = (fix >> 28) & 0xF;
    }
    return res;
}

// ─── get_string ──────────────────────────────────────────────────────────────

static void renault_valeo_fsk_get_string(void* ctx, FuriString* output) {
    furi_assert(ctx);
    RenaultValeoFSKDecoder* inst = ctx;

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

static const SubGhzProtocolDecoder renault_valeo_fsk_decoder = {
    .alloc = renault_valeo_fsk_alloc,
    .free = renault_valeo_fsk_free,
    .feed = renault_valeo_fsk_feed,
    .reset = renault_valeo_fsk_reset,
    .get_hash_data = renault_valeo_fsk_get_hash,
    .serialize = renault_valeo_fsk_serialize,
    .deserialize = renault_valeo_fsk_deserialize,
    .get_string = renault_valeo_fsk_get_string,
};

const SubGhzProtocol subghz_protocol_renault_valeo_fsk = {
    .name = SUBGHZ_PROTOCOL_RENAULT_VALEO_FSK_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 |
            SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &renault_valeo_fsk_decoder,
    .encoder = NULL,
};
