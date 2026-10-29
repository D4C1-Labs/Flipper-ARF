#include "honeywell_wdb.h"

#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include <lib/subghz/blocks/math.h>

#define TAG "SubGhzProtocolHoneywellWdb"

static const SubGhzBlockConst subghz_protocol_honeywell_wdb_const = {
    .te_short = 160,
    .te_long = 320,
    .te_delta = 60,
    .min_count_bit_for_found = 48,
};

struct SubGhzProtocolDecoderHoneywell_WDB {
    SubGhzProtocolDecoderBase base;

    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    const char* device_type;
    const char* alert;
    uint8_t secret_knock;
    uint8_t relay;
    uint8_t lowbat;
};

struct SubGhzProtocolEncoderHoneywell_WDB {
    SubGhzProtocolEncoderBase base;

    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

typedef enum {
    Honeywell_WDBDecoderStepReset = 0,
    Honeywell_WDBDecoderStepFoundStartBit,
    Honeywell_WDBDecoderStepSaveDuration,
    Honeywell_WDBDecoderStepCheckDuration,
} Honeywell_WDBDecoderStep;

const SubGhzProtocolDecoder subghz_protocol_honeywell_wdb_decoder = {
    .alloc = subghz_protocol_decoder_honeywell_wdb_alloc,
    .free = subghz_protocol_decoder_honeywell_wdb_free,

    .feed = subghz_protocol_decoder_honeywell_wdb_feed,
    .reset = subghz_protocol_decoder_honeywell_wdb_reset,

    .get_hash_data = subghz_protocol_decoder_honeywell_wdb_get_hash_data,
    .serialize = subghz_protocol_decoder_honeywell_wdb_serialize,
    .deserialize = subghz_protocol_decoder_honeywell_wdb_deserialize,
    .get_string = subghz_protocol_decoder_honeywell_wdb_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_honeywell_wdb_encoder = {
    .alloc = subghz_protocol_encoder_honeywell_wdb_alloc,
    .free = subghz_protocol_encoder_honeywell_wdb_free,

    .deserialize = subghz_protocol_encoder_honeywell_wdb_deserialize,
    .stop = subghz_protocol_encoder_honeywell_wdb_stop,
    .yield = subghz_protocol_encoder_honeywell_wdb_yield,
};

const SubGhzProtocol subghz_protocol_honeywell_wdb = {
    .name = SUBGHZ_PROTOCOL_HONEYWELL_WDB_NAME,
    .type = SubGhzProtocolTypeStatic,
    .flag = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_315 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,

    .decoder = &subghz_protocol_honeywell_wdb_decoder,
    .encoder = &subghz_protocol_honeywell_wdb_encoder,
};

void* subghz_protocol_encoder_honeywell_wdb_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHoneywell_WDB* instance =
        malloc(sizeof(SubGhzProtocolEncoderHoneywell_WDB));

    instance->base.protocol = &subghz_protocol_honeywell_wdb;
    instance->generic.protocol_name = instance->base.protocol->name;

    instance->encoder.repeat = 3;
    instance->encoder.size_upload = 128;
    instance->encoder.upload = malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_honeywell_wdb_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderHoneywell_WDB* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

static bool subghz_protocol_encoder_honeywell_wdb_get_upload(
    SubGhzProtocolEncoderHoneywell_WDB* instance) {
    furi_assert(instance);
    size_t index = 0;
    size_t size_upload = (instance->generic.data_count_bit * 2) + 2;
    if(size_upload > instance->encoder.size_upload) {
        FURI_LOG_E(TAG, "Size upload exceeds allocated encoder buffer.");
        return false;
    } else {
        instance->encoder.size_upload = size_upload;
    }

    instance->encoder.upload[index++] =
        level_duration_make(false, (uint32_t)subghz_protocol_honeywell_wdb_const.te_short * 3);

    for(uint8_t i = instance->generic.data_count_bit; i > 0; i--) {
        if(bit_read(instance->generic.data, i - 1)) {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_honeywell_wdb_const.te_long);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_honeywell_wdb_const.te_short);
        } else {
            instance->encoder.upload[index++] =
                level_duration_make(true, (uint32_t)subghz_protocol_honeywell_wdb_const.te_short);
            instance->encoder.upload[index++] =
                level_duration_make(false, (uint32_t)subghz_protocol_honeywell_wdb_const.te_long);
        }
    }
    instance->encoder.upload[index++] =
        level_duration_make(true, (uint32_t)subghz_protocol_honeywell_wdb_const.te_short * 3);
    return true;
}

SubGhzProtocolStatus subghz_protocol_encoder_honeywell_wdb_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHoneywell_WDB* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    do {
        ret = subghz_block_generic_deserialize_check_count_bit(
            &instance->generic,
            flipper_format,
            subghz_protocol_honeywell_wdb_const.min_count_bit_for_found);
        if(ret != SubGhzProtocolStatusOk) {
            break;
        }

        flipper_format_read_uint32(
            flipper_format, "Repeat", (uint32_t*)&instance->encoder.repeat, 1);

        if(!subghz_protocol_encoder_honeywell_wdb_get_upload(instance)) {
            ret = SubGhzProtocolStatusErrorEncoderGetUpload;
            break;
        }
        instance->encoder.is_running = true;
    } while(false);

    return ret;
}

void subghz_protocol_encoder_honeywell_wdb_stop(void* context) {
    SubGhzProtocolEncoderHoneywell_WDB* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_honeywell_wdb_yield(void* context) {
    SubGhzProtocolEncoderHoneywell_WDB* instance = context;

    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
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

void* subghz_protocol_decoder_honeywell_wdb_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHoneywell_WDB* instance =
        malloc(sizeof(SubGhzProtocolDecoderHoneywell_WDB));
    instance->base.protocol = &subghz_protocol_honeywell_wdb;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_honeywell_wdb_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell_WDB* instance = context;
    free(instance);
}

void subghz_protocol_decoder_honeywell_wdb_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell_WDB* instance = context;
    instance->decoder.parser_step = Honeywell_WDBDecoderStepReset;
}

void subghz_protocol_decoder_honeywell_wdb_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell_WDB* instance = context;
    switch(instance->decoder.parser_step) {
    case Honeywell_WDBDecoderStepReset:
        if((!level) && (DURATION_DIFF(duration, subghz_protocol_honeywell_wdb_const.te_short * 3) <
                        subghz_protocol_honeywell_wdb_const.te_delta)) {
            instance->decoder.decode_count_bit = 0;
            instance->decoder.decode_data = 0;
            instance->decoder.parser_step = Honeywell_WDBDecoderStepSaveDuration;
        }
        break;
    case Honeywell_WDBDecoderStepSaveDuration:
        if(level) {
            if(DURATION_DIFF(duration, subghz_protocol_honeywell_wdb_const.te_short * 3) <
               subghz_protocol_honeywell_wdb_const.te_delta) {
                if((instance->decoder.decode_count_bit ==
                    subghz_protocol_honeywell_wdb_const.min_count_bit_for_found) &&
                   ((instance->decoder.decode_data & 0x01) ==
                    subghz_protocol_blocks_get_parity(
                        instance->decoder.decode_data >> 1,
                        subghz_protocol_honeywell_wdb_const.min_count_bit_for_found - 1))) {
                    instance->generic.data = instance->decoder.decode_data;
                    instance->generic.data_count_bit = instance->decoder.decode_count_bit;

                    if(instance->base.callback)
                        instance->base.callback(&instance->base, instance->base.context);
                }
                instance->decoder.parser_step = Honeywell_WDBDecoderStepReset;
                break;
            }
            instance->decoder.te_last = duration;
            instance->decoder.parser_step = Honeywell_WDBDecoderStepCheckDuration;
        } else {
            instance->decoder.parser_step = Honeywell_WDBDecoderStepReset;
        }
        break;
    case Honeywell_WDBDecoderStepCheckDuration:
        if(!level) {
            if((DURATION_DIFF(
                    instance->decoder.te_last, subghz_protocol_honeywell_wdb_const.te_short) <
                subghz_protocol_honeywell_wdb_const.te_delta) &&
               (DURATION_DIFF(duration, subghz_protocol_honeywell_wdb_const.te_long) <
                subghz_protocol_honeywell_wdb_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 0);
                instance->decoder.parser_step = Honeywell_WDBDecoderStepSaveDuration;
            } else if(
                (DURATION_DIFF(
                     instance->decoder.te_last, subghz_protocol_honeywell_wdb_const.te_long) <
                 subghz_protocol_honeywell_wdb_const.te_delta) &&
                (DURATION_DIFF(duration, subghz_protocol_honeywell_wdb_const.te_short) <
                 subghz_protocol_honeywell_wdb_const.te_delta)) {
                subghz_protocol_blocks_add_bit(&instance->decoder, 1);
                instance->decoder.parser_step = Honeywell_WDBDecoderStepSaveDuration;
            } else
                instance->decoder.parser_step = Honeywell_WDBDecoderStepReset;
        } else {
            instance->decoder.parser_step = Honeywell_WDBDecoderStepReset;
        }
        break;
    }
}

static void subghz_protocol_honeywell_wdb_check_remote_controller(
    SubGhzProtocolDecoderHoneywell_WDB* instance) {
    instance->generic.serial = (instance->generic.data >> 28) & 0xFFFFF;
    switch((instance->generic.data >> 20) & 0x3) {
    case 0x02:
        instance->device_type = "Doorbell";
        break;
    case 0x01:
        instance->device_type = "PIR-Motion";
        break;
    default:
        instance->device_type = "Unknown";
        break;
    }

    switch((instance->generic.data >> 16) & 0x3) {
    case 0x00:
        instance->alert = "Normal";
        break;
    case 0x01:
    case 0x02:
        instance->alert = "High";
        break;
    case 0x03:
        instance->alert = "Full";
        break;
    default:
        instance->alert = "Unknown";
        break;
    }

    instance->secret_knock = (uint8_t)((instance->generic.data >> 4) & 0x1);
    instance->relay = (uint8_t)((instance->generic.data >> 3) & 0x1);
    instance->lowbat = (uint8_t)((instance->generic.data >> 1) & 0x1);
}

uint8_t subghz_protocol_decoder_honeywell_wdb_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell_WDB* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_honeywell_wdb_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell_WDB* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus subghz_protocol_decoder_honeywell_wdb_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell_WDB* instance = context;
    return subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_honeywell_wdb_const.min_count_bit_for_found);
}

void subghz_protocol_decoder_honeywell_wdb_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderHoneywell_WDB* instance = context;
    subghz_protocol_honeywell_wdb_check_remote_controller(instance);

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%lX%08lX\r\n"
        "Sn:0x%05lX\r\n"
        "DT:%s  Al:%s\r\n"
        "SK:%01X R:%01X LBat:%01X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)((instance->generic.data >> 32) & 0xFFFFFFFF),
        (uint32_t)(instance->generic.data & 0xFFFFFFFF),
        instance->generic.serial,
        instance->device_type,
        instance->alert,
        instance->secret_knock,
        instance->relay,
        instance->lowbat);
}
