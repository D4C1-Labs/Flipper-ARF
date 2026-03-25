#include "renault_hitag.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"

#define TAG "RenaultHitag"

// reemplazo de DURATION_DIFF
#define ABS_DIFF(a, b) ((a) > (b) ? ((a) - (b)) : ((b) - (a)))

static const SubGhzBlockConst renault_hitag_const = {
    .te_short = 200,
    .te_long = 400,
    .te_delta = 120,
    .min_count_bit_for_found = 40,
};

#define HITAG_MIN_BITS 40
#define HITAG_MAX_BITS 72

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint64_t data;
    uint8_t bit_count;
} RenaultHitagDecoder;

static void* renault_hitag_alloc(SubGhzEnvironment* env) {
    UNUSED(env);
    RenaultHitagDecoder* inst = calloc(1, sizeof(RenaultHitagDecoder));
    inst->base.protocol = &subghz_protocol_renault_hitag;
    inst->generic.protocol_name = inst->base.protocol->name;
    return inst;
}

static void renault_hitag_free(void* ctx) {
    free(ctx);
}

static void renault_hitag_reset(void* ctx) {
    RenaultHitagDecoder* inst = ctx;
    inst->bit_count = 0;
    inst->data = 0;
}

static void renault_hitag_feed(void* ctx, bool level, uint32_t duration) {
    UNUSED(level); // 👈 FIX warning

    RenaultHitagDecoder* inst = ctx;

    if(ABS_DIFF(duration, renault_hitag_const.te_short) < renault_hitag_const.te_delta) {
        inst->data <<= 1;
        inst->bit_count++;
    } else if(ABS_DIFF(duration, renault_hitag_const.te_long) < renault_hitag_const.te_delta) {
        inst->data = (inst->data << 1) | 1;
        inst->bit_count++;
    } else {
        if(inst->bit_count >= HITAG_MIN_BITS && inst->bit_count <= HITAG_MAX_BITS) {
            inst->generic.data = inst->data;
            inst->generic.data_count_bit = inst->bit_count;

            inst->generic.serial = (uint32_t)(inst->data >> 16);
            inst->generic.cnt = inst->data & 0xFFFF;

            if(inst->base.callback) {
                inst->base.callback(&inst->base, inst->base.context);
            }
        }
        renault_hitag_reset(ctx);
    }
}

static uint8_t renault_hitag_hash(void* ctx) {
    RenaultHitagDecoder* inst = ctx;
    return (uint8_t)(inst->data ^ (inst->data >> 8) ^ (inst->data >> 16));
}

const SubGhzProtocolDecoder renault_hitag_decoder = {
    .alloc = renault_hitag_alloc,
    .free = renault_hitag_free,
    .feed = renault_hitag_feed,
    .reset = renault_hitag_reset,
    .get_hash_data = renault_hitag_hash,
};

const SubGhzProtocol subghz_protocol_renault_hitag = {
    .name = SUBGHZ_PROTOCOL_RENAULT_HITAG_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_Decodable,
    .decoder = &renault_hitag_decoder,
};
