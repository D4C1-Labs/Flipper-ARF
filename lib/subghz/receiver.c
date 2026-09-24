#include "receiver.h"

#include "registry.h"

#include <m-array.h>

typedef struct {
    SubGhzProtocolEncoderBase* base;
} SubGhzReceiverSlot;

ARRAY_DEF(SubGhzReceiverSlotArray, SubGhzReceiverSlot, M_POD_OPLIST); //-V658
#define M_OPL_SubGhzReceiverSlotArray_t() ARRAY_OPLIST(SubGhzReceiverSlotArray, M_POD_OPLIST)

struct SubGhzReceiver {
    SubGhzReceiverSlotArray_t slots;
    SubGhzProtocolFlag filter;
    // Modulation gate: only protocols whose AM/FM flag matches the active
    // preset's modulation are fed. 0 means "gate disabled" (feed everything),
    // preserving legacy behaviour for callers that never set it.
    SubGhzProtocolFlag modulation_filter;

    SubGhzReceiverCallback callback;
    void* context;
};

SubGhzReceiver* subghz_receiver_alloc_init(SubGhzEnvironment* environment) {
    SubGhzReceiver* instance = malloc(sizeof(SubGhzReceiver));
    SubGhzReceiverSlotArray_init(instance->slots);
    const SubGhzProtocolRegistry* protocol_registry_items =
        subghz_environment_get_protocol_registry(environment);

    for(size_t i = 0; i < subghz_protocol_registry_count(protocol_registry_items); ++i) {
        const SubGhzProtocol* protocol =
            subghz_protocol_registry_get_by_index(protocol_registry_items, i);

        if(protocol->decoder && protocol->decoder->alloc) {
            SubGhzReceiverSlot* slot = SubGhzReceiverSlotArray_push_new(instance->slots);
            slot->base = protocol->decoder->alloc(environment);
        }
    }

    instance->callback = NULL;
    instance->context = NULL;
    instance->modulation_filter = 0;
    return instance;
}

void subghz_receiver_free(SubGhzReceiver* instance) {
    furi_check(instance);

    instance->callback = NULL;
    instance->context = NULL;

    // Release allocated slots
    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            slot->base->protocol->decoder->free(slot->base);
            slot->base = NULL;
        }
    SubGhzReceiverSlotArray_clear(instance->slots);

    free(instance);
}

void subghz_receiver_decode(SubGhzReceiver* instance, bool level, uint32_t duration) {
    furi_check(instance);
    furi_check(instance->slots);

    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            const SubGhzProtocolFlag protocol_flag = slot->base->protocol->flag;
            if((protocol_flag & instance->filter) == 0) {
                continue;
            }
            // Modulation gate (mirrors ProtoPirate's per-preset registry
            // selection): when active, skip protocols whose declared AM/FM
            // modulation does not match the current preset's modulation.
            // Without this, an AM (OOK) protocol such as Fiat V2 would be fed
            // an FM (2-FSK) capture (e.g. a KIA V6 signal) and can produce
            // spurious matches. A protocol declaring neither AM nor FM is
            // always allowed through (e.g. RAW).
            if(instance->modulation_filter != 0) {
                const SubGhzProtocolFlag protocol_modulation =
                    protocol_flag & (SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM);
                if(protocol_modulation != 0 &&
                   (protocol_modulation & instance->modulation_filter) == 0) {
                    continue;
                }
            }
            slot->base->protocol->decoder->feed(slot->base, level, duration);
        }
}

void subghz_receiver_reset(SubGhzReceiver* instance) {
    furi_check(instance);
    furi_check(instance->slots);

    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            slot->base->protocol->decoder->reset(slot->base);
        }
}

static void subghz_receiver_rx_callback(SubGhzProtocolDecoderBase* decoder_base, void* context) {
    SubGhzReceiver* instance = context;
    if(instance->callback) {
        instance->callback(instance, decoder_base, instance->context);
    }
}

void subghz_receiver_set_rx_callback(
    SubGhzReceiver* instance,
    SubGhzReceiverCallback callback,
    void* context) {
    furi_check(instance);

    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            subghz_protocol_decoder_base_set_decoder_callback(
                (SubGhzProtocolDecoderBase*)slot->base, subghz_receiver_rx_callback, instance);
        }

    instance->callback = callback;
    instance->context = context;
}

void subghz_receiver_set_filter(SubGhzReceiver* instance, SubGhzProtocolFlag filter) {
    furi_check(instance);
    instance->filter = filter;
}

void subghz_receiver_set_modulation_filter(
    SubGhzReceiver* instance,
    SubGhzProtocolFlag modulation_filter) {
    furi_check(instance);
    instance->modulation_filter =
        modulation_filter & (SubGhzProtocolFlag_AM | SubGhzProtocolFlag_FM);
}

SubGhzProtocolDecoderBase* subghz_receiver_search_decoder_base_by_name(
    SubGhzReceiver* instance,
    const char* decoder_name) {
    furi_check(instance);

    SubGhzProtocolDecoderBase* result = NULL;

    for
        M_EACH(slot, instance->slots, SubGhzReceiverSlotArray_t) {
            if(strcmp(slot->base->protocol->name, decoder_name) == 0) {
                result = (SubGhzProtocolDecoderBase*)slot->base;
                break;
            }
        }
    return result;
}
