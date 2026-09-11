#include "weather_station_history.h"
#include <flipper_format/flipper_format_i.h>
#include <lib/toolbox/stream/stream.h>
#include <lib/subghz/receiver.h>
#include "protocols/ws_generic.h"

#include <furi.h>
#include <string.h>

#define WS_HISTORY_MAX 5
#define TAG "WSHistory"

typedef struct {
    FuriString* item_str;
    FlipperFormat* flipper_string;
    uint8_t type;
    uint32_t id;
    uint8_t channel;
    FuriString* protocol_name;
    SubGhzRadioPreset* preset;
    uint32_t last_seen_tick;
} WSHistoryItem;

ARRAY_DEF(WSHistoryItemArray, WSHistoryItem, M_POD_OPLIST)

#define M_OPL_WSHistoryItemArray_t() ARRAY_OPLIST(WSHistoryItemArray, M_POD_OPLIST)

typedef struct {
    WSHistoryItemArray_t data;
} WSHistoryStruct;

struct WSHistory {
    uint32_t last_update_timestamp;
    uint16_t last_index_write;
    uint8_t code_last_hash_data;
    FuriString* tmp_string;
    /* Reused scratch objects: avoid heap allocation/free on every received frame. */
    FuriString* scratch_protocol;
    FlipperFormat* scratch_format;
    WSHistoryStruct* history;
    uint16_t last_touched_index;
};

static void ws_history_preset_copy(SubGhzRadioPreset* destination, const SubGhzRadioPreset* source) {
    if(!destination || !destination->name || !source || !source->name) return;

    destination->frequency = source->frequency;
    furi_string_set(destination->name, source->name);

    /* Most sensors use the same preset on every update. Reuse the existing
       allocation instead of free+malloc on every frame, which fragmented the
       Flipper heap during long scans. */
    if(!source->data || source->data_size == 0U) {
        free(destination->data);
        destination->data = NULL;
        destination->data_size = 0U;
        return;
    }

    if(destination->data && destination->data_size == source->data_size) {
        memcpy(destination->data, source->data, source->data_size);
        return;
    }

    uint8_t* replacement = malloc(source->data_size);
    if(!replacement) {
        FURI_LOG_E(TAG, "Preset data allocation failed");
        return;
    }

    memcpy(replacement, source->data, source->data_size);
    free(destination->data);
    destination->data = replacement;
    destination->data_size = source->data_size;
}


static void ws_history_update_item_text(WSHistory* instance, WSHistoryItem* item) {
    if(!instance || !instance->tmp_string || !item || !item->item_str ||
       !item->flipper_string) return;

    furi_string_reset(instance->tmp_string);
    if(!flipper_format_rewind(item->flipper_string) ||
       !flipper_format_read_string(item->flipper_string, "Protocol", instance->tmp_string)) {
        FURI_LOG_E(TAG, "Missing Protocol");
        furi_string_set(item->item_str, "UNKNOWN");
        return;
    }

    if(!flipper_format_rewind(item->flipper_string)) {
        FURI_LOG_E(TAG, "Rewind error");
        return;
    }

    uint8_t key_data[sizeof(uint64_t)] = {0};
    if(!flipper_format_read_hex(item->flipper_string, "Data", key_data, sizeof(key_data))) {
        FURI_LOG_E(TAG, "Missing Data");
        return;
    }

    uint64_t data = 0;
    for(uint8_t i = 0; i < sizeof(uint64_t); i++) {
        data = (data << 8) | key_data[i];
    }

    uint32_t channel = WS_NO_CHANNEL;
    flipper_format_rewind(item->flipper_string);
    if(flipper_format_read_uint32(item->flipper_string, "Ch", &channel, 1) &&
       channel != WS_NO_CHANNEL) {
        furi_string_cat_printf(instance->tmp_string, " Ch:%X", (uint8_t)channel);
    }

    furi_string_printf(
        item->item_str, "%s %llX", furi_string_get_cstr(instance->tmp_string), data);
}
static void ws_history_preset_free(SubGhzRadioPreset* preset) {
    if(!preset) return;
    if(preset->name) furi_string_free(preset->name);
    free(preset->data);
    free(preset);
}

WSHistory* ws_history_alloc(void) {
    WSHistory* instance = malloc(sizeof(WSHistory));
    if(!instance) return NULL;
    memset(instance, 0, sizeof(WSHistory));

    instance->tmp_string = furi_string_alloc();
    instance->scratch_protocol = furi_string_alloc();
    instance->scratch_format = flipper_format_string_alloc();
    instance->history = malloc(sizeof(WSHistoryStruct));
    if(!instance->tmp_string || !instance->scratch_protocol || !instance->scratch_format ||
       !instance->history) {
        if(instance->tmp_string) furi_string_free(instance->tmp_string);
        if(instance->scratch_protocol) furi_string_free(instance->scratch_protocol);
        if(instance->scratch_format) flipper_format_free(instance->scratch_format);
        free(instance->history);
        free(instance);
        return NULL;
    }
    WSHistoryItemArray_init(instance->history->data);
    return instance;
}

void ws_history_free(WSHistory* instance) {
    if(!instance) return;
    if(instance->tmp_string) furi_string_free(instance->tmp_string);
    if(instance->scratch_protocol) furi_string_free(instance->scratch_protocol);
    if(instance->scratch_format) flipper_format_free(instance->scratch_format);
    if(instance->history) {
        for M_EACH(item, instance->history->data, WSHistoryItemArray_t) {
            if(item->item_str) furi_string_free(item->item_str);
            if(item->protocol_name) furi_string_free(item->protocol_name);
            ws_history_preset_free(item->preset);
            if(item->flipper_string) flipper_format_free(item->flipper_string);
            item->type = 0;
        }
        WSHistoryItemArray_clear(instance->history->data);
        free(instance->history);
    }
    free(instance);
}

bool ws_history_is_valid_index(const WSHistory* instance, uint16_t idx) {
    if(!instance || !instance->history) return false;
    const size_t size = WSHistoryItemArray_size(instance->history->data);
    return idx < instance->last_index_write && idx < size;
}

uint32_t ws_history_get_frequency(WSHistory* instance, uint16_t idx) {
    if(!ws_history_is_valid_index(instance, idx)) return 0U;
    WSHistoryItem* item = WSHistoryItemArray_get(instance->history->data, idx);
    return (item && item->preset) ? item->preset->frequency : 0U;
}

SubGhzRadioPreset* ws_history_get_radio_preset(WSHistory* instance, uint16_t idx) {
    if(!ws_history_is_valid_index(instance, idx)) return NULL;
    WSHistoryItem* item = WSHistoryItemArray_get(instance->history->data, idx);
    return item ? item->preset : NULL;
}

const char* ws_history_get_preset(WSHistory* instance, uint16_t idx) {
    SubGhzRadioPreset* preset = ws_history_get_radio_preset(instance, idx);
    return (preset && preset->name) ? furi_string_get_cstr(preset->name) : "";
}

void ws_history_reset(WSHistory* instance) {
    if(!instance || !instance->history) return;
    if(instance->tmp_string) furi_string_reset(instance->tmp_string);
    for
        M_EACH(item, instance->history->data, WSHistoryItemArray_t) {
            if(item->item_str) furi_string_free(item->item_str);
            if(item->protocol_name) furi_string_free(item->protocol_name);
            ws_history_preset_free(item->preset);
            if(item->flipper_string) flipper_format_free(item->flipper_string);
            memset(item, 0, sizeof(WSHistoryItem));
        }
    WSHistoryItemArray_reset(instance->history->data);
    instance->last_index_write = 0;
    instance->last_update_timestamp = 0;
    instance->code_last_hash_data = 0;
    instance->last_touched_index = 0;
}

uint16_t ws_history_get_item(WSHistory* instance) {
    return instance ? instance->last_index_write : 0U;
}

uint8_t ws_history_get_type_protocol(WSHistory* instance, uint16_t idx) {
    if(!ws_history_is_valid_index(instance, idx)) return 0U;
    WSHistoryItem* item = WSHistoryItemArray_get(instance->history->data, idx);
    return item ? item->type : 0U;
}

const char* ws_history_get_protocol_name(WSHistory* instance, uint16_t idx) {
    if(!ws_history_is_valid_index(instance, idx) || !instance->tmp_string) return "";
    WSHistoryItem* item = WSHistoryItemArray_get(instance->history->data, idx);
    if(!item || !item->flipper_string) return "";
    flipper_format_rewind(item->flipper_string);
    if(!flipper_format_read_string(item->flipper_string, "Protocol", instance->tmp_string)) {
        FURI_LOG_E(TAG, "Missing Protocol");
        furi_string_reset(instance->tmp_string);
    }
    return furi_string_get_cstr(instance->tmp_string);
}

FlipperFormat* ws_history_get_decoded_data(WSHistory* instance, uint16_t idx) {
    if(!ws_history_is_valid_index(instance, idx)) return NULL;
    WSHistoryItem* item = WSHistoryItemArray_get(instance->history->data, idx);
    return item ? item->flipper_string : NULL;
}

bool ws_history_get_text_space_left(WSHistory* instance, FuriString* output) {
    if(!instance) {
        if(output) furi_string_set(output, "00/05");
        return false;
    }
    if(output) furi_string_printf(output, "%02u/%02u", instance->last_index_write, WS_HISTORY_MAX);
    /* Five-slot RX cache: after 5 distinct stations, the least recently seen slot is reused. */
    return false;
}

void ws_history_get_text_item_menu(WSHistory* instance, FuriString* output, uint16_t idx) {
    if(!output) return;
    if(!ws_history_is_valid_index(instance, idx)) {
        furi_string_set(output, "NO DATA");
        return;
    }
    WSHistoryItem* item = WSHistoryItemArray_get(instance->history->data, idx);
    if(!item || !item->item_str) {
        furi_string_set(output, "NO DATA");
        return;
    }
    furi_string_set(output, item->item_str);
}

WSHistoryStateAddKey
    ws_history_add_to_history(WSHistory* instance, void* context, SubGhzRadioPreset* preset) {
    if(!instance || !instance->history || !context || !preset || !instance->scratch_format ||
       !instance->scratch_protocol || !instance->tmp_string) {
        return WSHistoryStateAddKeyOverflow;
    }

    SubGhzProtocolDecoderBase* decoder_base = context;
    if(!decoder_base->protocol) return WSHistoryStateAddKeyOverflow;
    instance->code_last_hash_data = subghz_protocol_decoder_base_get_hash_data(decoder_base);
    instance->last_update_timestamp = furi_get_tick();

    FlipperFormat* fff = instance->scratch_format;
    FuriString* incoming_protocol = instance->scratch_protocol;
    Stream* scratch_stream = flipper_format_get_raw_stream(fff);
    stream_clean(scratch_stream);
    furi_string_reset(incoming_protocol);

    uint32_t id = 0;
    uint32_t channel_u32 = WS_NO_CHANNEL;
    subghz_protocol_decoder_base_serialize(decoder_base, fff, preset);

    flipper_format_rewind(fff);
    if(!flipper_format_read_string(fff, "Protocol", incoming_protocol)) {
        furi_string_set(incoming_protocol, "UNKNOWN");
    }
    flipper_format_rewind(fff);
    flipper_format_read_uint32(fff, "Id", &id, 1);
    flipper_format_rewind(fff);
    flipper_format_read_uint32(fff, "Ch", &channel_u32, 1);
    const uint8_t channel = (uint8_t)channel_u32;
    const uint32_t now = instance->last_update_timestamp;

    /* Same protocol + sensor ID + channel updates one existing station. */
    for(size_t i = 0; i < WSHistoryItemArray_size(instance->history->data); i++) {
        WSHistoryItem* item = WSHistoryItemArray_get(instance->history->data, i);
        if(!item || !item->protocol_name || !item->flipper_string || !item->preset ||
           !item->item_str) continue;
        if(item->id == id && item->channel == channel &&
           furi_string_equal(item->protocol_name, incoming_protocol)) {
            Stream* item_stream = flipper_format_get_raw_stream(item->flipper_string);
            stream_clean(item_stream);
            subghz_protocol_decoder_base_serialize(decoder_base, item->flipper_string, preset);
            ws_history_preset_copy(item->preset, preset);
            item->last_seen_tick = now;
            ws_history_update_item_text(instance, item);
            instance->last_touched_index = i;
            return WSHistoryStateAddKeyUpdateData;
        }
    }

    /* At 5 stations, reuse the least recently seen slot. This keeps memory
       bounded and avoids a 6th set of heap allocations. */
    if(instance->last_index_write >= WS_HISTORY_MAX) {
        size_t oldest_index = 0;
        uint32_t oldest_age = 0;
        for(size_t i = 0; i < WSHistoryItemArray_size(instance->history->data); i++) {
            WSHistoryItem* candidate = WSHistoryItemArray_get(instance->history->data, i);
            if(!candidate) continue;
            const uint32_t age = now - candidate->last_seen_tick;
            if(i == 0 || age > oldest_age) {
                oldest_age = age;
                oldest_index = i;
            }
        }

        WSHistoryItem* item = WSHistoryItemArray_get(instance->history->data, oldest_index);
        if(!item || !item->protocol_name || !item->preset || !item->flipper_string ||
           !item->item_str) {
            return WSHistoryStateAddKeyOverflow;
        }
        item->type = decoder_base->protocol->type;
        item->id = id;
        item->channel = channel;
        item->last_seen_tick = now;
        furi_string_set(item->protocol_name, incoming_protocol);
        ws_history_preset_copy(item->preset, preset);

        Stream* item_stream = flipper_format_get_raw_stream(item->flipper_string);
        stream_clean(item_stream);
        subghz_protocol_decoder_base_serialize(decoder_base, item->flipper_string, preset);
        ws_history_update_item_text(instance, item);
        instance->last_touched_index = oldest_index;
        return WSHistoryStateAddKeyReplaceData;
    }

    SubGhzRadioPreset* new_preset = malloc(sizeof(SubGhzRadioPreset));
    if(!new_preset) {
        FURI_LOG_E(TAG, "History preset allocation failed");
        return WSHistoryStateAddKeyOverflow;
    }
    memset(new_preset, 0, sizeof(SubGhzRadioPreset));
    new_preset->name = furi_string_alloc();
    FuriString* new_protocol_name = furi_string_alloc_set(incoming_protocol);
    FuriString* new_item_str = furi_string_alloc();
    FlipperFormat* new_format = flipper_format_string_alloc();
    if(!new_preset->name || !new_protocol_name || !new_item_str || !new_format) {
        if(new_protocol_name) furi_string_free(new_protocol_name);
        if(new_item_str) furi_string_free(new_item_str);
        if(new_format) flipper_format_free(new_format);
        ws_history_preset_free(new_preset);
        FURI_LOG_E(TAG, "History item allocation failed");
        return WSHistoryStateAddKeyOverflow;
    }

    WSHistoryItem* item = WSHistoryItemArray_push_raw(instance->history->data);
    if(!item) {
        furi_string_free(new_protocol_name);
        furi_string_free(new_item_str);
        flipper_format_free(new_format);
        ws_history_preset_free(new_preset);
        return WSHistoryStateAddKeyOverflow;
    }
    memset(item, 0, sizeof(WSHistoryItem));
    item->preset = new_preset;
    item->protocol_name = new_protocol_name;
    item->item_str = new_item_str;
    item->flipper_string = new_format;

    item->type = decoder_base->protocol->type;
    item->id = id;
    item->channel = channel;
    item->last_seen_tick = now;
    ws_history_preset_copy(item->preset, preset);
    subghz_protocol_decoder_base_serialize(decoder_base, item->flipper_string, preset);
    ws_history_update_item_text(instance, item);

    instance->last_touched_index = instance->last_index_write;
    instance->last_index_write++;
    return WSHistoryStateAddKeyNewDada;
}

uint16_t ws_history_get_last_touched_index(WSHistory* instance) {
    return instance ? instance->last_touched_index : 0U;
}
