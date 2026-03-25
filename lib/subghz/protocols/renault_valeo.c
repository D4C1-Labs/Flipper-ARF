#include "renault_valeo.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/generic.h"

#define TAG "RenaultValeo"

#define ABS_DIFF(a,b) ((a)>(b)?((a)-(b)):((b)-(a)))

#define MIN_BITS 64
#define MAX_BITS 96

typedef struct {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockGeneric generic;

    uint64_t data;
    uint8_t bit_count;
} RenaultValeoDecoder;

static void* alloc(SubGhzEnvironment* e){
    UNUSED(e);
    RenaultValeoDecoder* i = calloc(1,sizeof(*i));
    i->base.protocol = &subghz_protocol_renault_valeo;
    i->generic.protocol_name = i->base.protocol->name;
    return i;
}

static void reset(void* ctx){
    RenaultValeoDecoder* i = ctx;
    i->data = 0;
    i->bit_count = 0;
}

static void feed(void* ctx,bool level,uint32_t d){
    UNUSED(level);
    RenaultValeoDecoder* i = ctx;

    if(d<600){ // heurística simple
        i->data <<=1;
        i->bit_count++;
    } else if(d<1200){
        i->data = (i->data<<1)|1;
        i->bit_count++;
    } else {
        if(i->bit_count>=MIN_BITS && i->bit_count<=MAX_BITS){
            i->generic.data = i->data;
            i->generic.data_count_bit = i->bit_count;

            i->generic.serial = (uint32_t)(i->data>>32);
            i->generic.cnt = (i->data>>8)&0xFFFF;

            // 🔐 placeholder crypto
            // uint64_t key = [MFKey keeloq key];

            if(i->base.callback)
                i->base.callback(&i->base,i->base.context);
        }
        reset(ctx);
    }
}

static uint8_t hash(void* ctx){
    RenaultValeoDecoder* i = ctx;
    return (uint8_t)(i->data ^ (i->data>>16));
}

static const SubGhzProtocolDecoder renault_valeo_decoder = {
    .alloc = alloc,
    .free = free,
    .feed = feed,
    .reset = reset,
    .get_hash_data = hash,
};

const SubGhzProtocol subghz_protocol_renault_valeo = {
    .name = SUBGHZ_PROTOCOL_RENAULT_VALEO_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_Decodable,
    .decoder = &renault_valeo_decoder,
};
