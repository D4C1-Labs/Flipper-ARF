#include "renault_siemens.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"

#define TAG "RenaultSiemens"

#define ABS_DIFF(a,b) ((a)>(b)?((a)-(b)):((b)-(a)))

static const SubGhzBlockConst consts = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 120,
    .min_count_bit_for_found = 64,
};

#define MIN_BITS 64
#define MAX_BITS 100

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;

    uint64_t data;
    uint8_t bit_count;
} RenaultSiemensDecoder;

static void* alloc(SubGhzEnvironment* e){
    UNUSED(e);
    RenaultSiemensDecoder* i = calloc(1,sizeof(*i));
    i->base.protocol = &subghz_protocol_renault_siemens;
    i->generic.protocol_name = i->base.protocol->name;
    return i;
}

static void reset(void* ctx){
    RenaultSiemensDecoder* i = ctx;
    i->data = 0;
    i->bit_count = 0;
}

static void feed(void* ctx,bool level,uint32_t d){
    UNUSED(level);
    RenaultSiemensDecoder* i = ctx;

    if(ABS_DIFF(d,consts.te_short)<consts.te_delta){
        i->data <<=1;
        i->bit_count++;
    } else if(ABS_DIFF(d,consts.te_long)<consts.te_delta){
        i->data = (i->data<<1)|1;
        i->bit_count++;
    } else {
        if(i->bit_count>=MIN_BITS && i->bit_count<=MAX_BITS){
            i->generic.data = i->data;
            i->generic.data_count_bit = i->bit_count;

            i->generic.serial = (uint32_t)(i->data>>32);
            i->generic.cnt = i->data & 0xFFFF;

            if(i->base.callback)
                i->base.callback(&i->base,i->base.context);
        }
        reset(ctx);
    }
}

static uint8_t hash(void* ctx){
    RenaultSiemensDecoder* i = ctx;
    return (uint8_t)(i->data ^ (i->data>>8));
}

const SubGhzProtocolDecoder decoder = {
    .alloc = alloc,
    .free = free,
    .feed = feed,
    .reset = reset,
    .get_hash_data = hash,
};

const SubGhzProtocol subghz_protocol_renault_siemens = {
    .name = SUBGHZ_PROTOCOL_RENAULT_SIEMENS_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_Decodable,
    .decoder = &decoder,
};
