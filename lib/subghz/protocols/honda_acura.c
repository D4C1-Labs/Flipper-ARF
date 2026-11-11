#include "honda_acura.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"

#define TAG "SubGhzProtocolHondaAcura"

/*
 * Honda/Acura RKE — 313.85 MHz (NA) / 433.92 MHz (EU), OOK.
 *
 * Reverse-engineered from real Flipper BinRAW captures of a 2014 Accord fob.
 * The previous implementation modelled this as PWM (bit = high-pulse width)
 * behind an 8-bit CRC gate and left the preamble after 20 pulses; that never
 * matched real frames, so every capture was silently dropped.
 *
 * Real on-air format:
 *   [preamble]  ~200-320 cycles of a ~250us square wave (te_short hi/lo)
 *   [sync gap]  one low of ~700-930us
 *   [data]      64 bits, MANCHESTER (bi-phase), half-bit ~250us, long ~500us
 *   whole frame repeated ~2x.
 *
 * Decoded 64-bit frame (MSB = first decoded bit):
 *   bits [63:52] 12-bit command    0xC20 = Lock, 0xA28 = Unlock
 *   bits [43:16] 28-bit serial / device id (fixed for a given fob)
 *   bits [15:0]  rolling counter (changes every press)
 *
 * ROLLING code: we identify & label the frame, we do not predict the next
 * counter. Command values validated across all provided lock/unlock captures.
 * If a future fob reads "Unknown", dump the raw Key shown in the info string
 * and extend HONDA_ACURA_CMD_*.
 */

#define HONDA_ACURA_BIT_COUNT     64U
#define HONDA_ACURA_MIN_SYMBOLS   36U
#define HONDA_ACURA_SYMBOL_CAP    512U
#define HONDA_ACURA_SYMBOL_BYTES  ((HONDA_ACURA_SYMBOL_CAP + 7U) / 8U)

/* Manchester element timing (microseconds). Windows are jitter-tolerant. */
#define HONDA_ACURA_SHORT_MIN 90U
#define HONDA_ACURA_SHORT_MAX 370U
#define HONDA_ACURA_LONG_MIN  370U
#define HONDA_ACURA_LONG_MAX  670U
#define HONDA_ACURA_ELEMENT   250U
#define HONDA_ACURA_SYNC_US   800U
#define HONDA_ACURA_PREAMBLE_TX 40U

/* 12-bit command prefix (top 12 bits of the frame).
 * Lock/Unlock are confirmed from real captures. Trunk/Panic codes are NOT yet
 * known — placeholders so the emulate D-pad grid is complete like the other
 * car protocols; this fob is a rolling code so a re-sent frame won't operate
 * the vehicle regardless. Replace the placeholders once captured. */
#define HONDA_ACURA_CMD_LOCK   0xC20U
#define HONDA_ACURA_CMD_UNLOCK 0xA28U
#define HONDA_ACURA_CMD_TRUNK  0x640U /* placeholder — unverified */
#define HONDA_ACURA_CMD_PANIC  0x080U /* placeholder — unverified */

/* Button tokens for display / custom-button machinery */
#define HONDA_ACURA_BTN_LOCK   0x10U
#define HONDA_ACURA_BTN_UNLOCK 0x20U
#define HONDA_ACURA_BTN_TRUNK  0x40U
#define HONDA_ACURA_BTN_PANIC  0x08U

static const SubGhzBlockConst subghz_protocol_honda_acura_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 160,
    .min_count_bit_for_found = 64,
};

struct SubGhzProtocolDecoderHondaAcura {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;
    uint8_t symbols[HONDA_ACURA_SYMBOL_BYTES];
    uint16_t symbols_count;
    uint16_t cmd;
};

struct SubGhzProtocolEncoderHondaAcura {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

const SubGhzProtocolDecoder subghz_protocol_honda_acura_decoder = {
    .alloc = subghz_protocol_decoder_honda_acura_alloc,
    .free = subghz_protocol_decoder_honda_acura_free,
    .feed = subghz_protocol_decoder_honda_acura_feed,
    .reset = subghz_protocol_decoder_honda_acura_reset,
    .get_hash_data = subghz_protocol_decoder_honda_acura_get_hash_data,
    .serialize = subghz_protocol_decoder_honda_acura_serialize,
    .deserialize = subghz_protocol_decoder_honda_acura_deserialize,
    .get_string = subghz_protocol_decoder_honda_acura_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_honda_acura_encoder = {
    .alloc = subghz_protocol_encoder_honda_acura_alloc,
    .free = subghz_protocol_encoder_honda_acura_free,
    .deserialize = subghz_protocol_encoder_honda_acura_deserialize,
    .stop = subghz_protocol_encoder_honda_acura_stop,
    .yield = subghz_protocol_encoder_honda_acura_yield,
};

const SubGhzProtocol subghz_protocol_honda_acura = {
    .name = HONDA_ACURA_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
            SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
            SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_honda_acura_decoder,
    .encoder = &subghz_protocol_honda_acura_encoder,
};

// ─── bit / symbol helpers ────────────────────────────────────────────────────

static void honda_acura_symbol_set(uint8_t* buf, uint16_t index, uint8_t v) {
    const uint16_t byte_index = index >> 3U;
    const uint8_t shift = (uint8_t)(~index) & 0x07U;
    const uint8_t mask = (uint8_t)(1U << shift);
    if(v) {
        buf[byte_index] |= mask;
    } else {
        buf[byte_index] &= (uint8_t)~mask;
    }
}

static uint8_t honda_acura_symbol_get(const uint8_t* buf, uint16_t index) {
    const uint16_t byte_index = index >> 3U;
    const uint8_t shift = (uint8_t)(~index) & 0x07U;
    return (uint8_t)((buf[byte_index] >> shift) & 1U);
}

static uint32_t honda_acura_packet_bits(const uint8_t packet[8], uint8_t start, uint8_t count) {
    uint32_t value = 0;
    for(uint8_t i = 0; i < count; i++) {
        const uint8_t bit_index = start + i;
        const uint8_t byte = packet[bit_index >> 3U];
        const uint8_t shift = (uint8_t)(~bit_index) & 0x07U;
        value = (value << 1U) | ((byte >> shift) & 1U);
    }
    return value;
}

/* Manchester-pack up to 64 bits from the half-bit symbol buffer. */
static bool honda_acura_manchester_pack_64(
    const uint8_t* sym,
    uint16_t count,
    uint16_t start_pos,
    bool inverted,
    uint8_t packet[8]) {
    memset(packet, 0, 8);
    uint16_t pos = start_pos;
    uint16_t bit_count = 0U;

    while(((uint16_t)(pos + 1U) < count) && (bit_count < HONDA_ACURA_BIT_COUNT)) {
        const uint8_t a = honda_acura_symbol_get(sym, pos);
        const uint8_t b = honda_acura_symbol_get(sym, pos + 1U);
        if(a == b) {
            pos++;
            continue;
        }
        const bool bit = inverted ? ((a == 0U) && (b == 1U)) : ((a == 1U) && (b == 0U));
        if(bit) {
            packet[bit_count >> 3U] |= (uint8_t)(1U << (((uint8_t)~bit_count) & 0x07U));
        }
        bit_count++;
        pos += 2U;
    }
    return bit_count >= HONDA_ACURA_BIT_COUNT;
}

static uint8_t honda_acura_cmd_to_btn(uint16_t cmd12) {
    /* Accept the decoded polarity or its 12-bit inverse (a flipped Manchester
     * phase inverts every bit) so both orientations classify correctly. */
    const uint16_t inv = (uint16_t)((~cmd12) & 0x0FFFU);
    if(cmd12 == HONDA_ACURA_CMD_LOCK || inv == HONDA_ACURA_CMD_LOCK) return HONDA_ACURA_BTN_LOCK;
    if(cmd12 == HONDA_ACURA_CMD_UNLOCK || inv == HONDA_ACURA_CMD_UNLOCK) return HONDA_ACURA_BTN_UNLOCK;
    return 0U;
}

/*
 * Custom-button (D-pad) support for emulation — same layout as the other car
 * protocols: OK = captured button, then Lock / Unlock / Trunk / Panic on the
 * D-pad. Lock/Unlock command codes are confirmed; Trunk/Panic are placeholders
 * (rolling code — a re-sent frame won't operate the car anyway).
 */
#define HONDA_ACURA_CUSTOM_BTN_MAX 4U

static uint8_t honda_acura_btn_to_custom(uint8_t btn) {
    switch(btn) {
    case HONDA_ACURA_BTN_LOCK: return 1U;
    case HONDA_ACURA_BTN_UNLOCK: return 2U;
    case HONDA_ACURA_BTN_TRUNK: return 3U;
    case HONDA_ACURA_BTN_PANIC: return 4U;
    default: return 1U;
    }
}

static uint8_t honda_acura_custom_to_btn(uint8_t custom, uint8_t original_btn) {
    switch(custom) {
    case 1U: return HONDA_ACURA_BTN_LOCK;
    case 2U: return HONDA_ACURA_BTN_UNLOCK;
    case 3U: return HONDA_ACURA_BTN_TRUNK;
    case 4U: return HONDA_ACURA_BTN_PANIC;
    default: return original_btn;
    }
}

/* 12-bit command word for a button token, in the captured Manchester polarity. */
static uint16_t honda_acura_btn_to_cmd(uint8_t btn, bool inverted) {
    uint16_t base;
    switch(btn) {
    case HONDA_ACURA_BTN_UNLOCK: base = HONDA_ACURA_CMD_UNLOCK; break;
    case HONDA_ACURA_BTN_TRUNK: base = HONDA_ACURA_CMD_TRUNK; break;
    case HONDA_ACURA_BTN_PANIC: base = HONDA_ACURA_CMD_PANIC; break;
    case HONDA_ACURA_BTN_LOCK:
    default: base = HONDA_ACURA_CMD_LOCK; break;
    }
    return inverted ? (uint16_t)(~base & 0x0FFFU) : base;
}

/* Register the D-pad remap so the emulate view shows the button grid. */
static void honda_acura_custom_btn_setup(uint8_t btn) {
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(honda_acura_btn_to_custom(btn));
    }
    subghz_custom_btn_set_max(HONDA_ACURA_CUSTOM_BTN_MAX);
}

static const char* honda_acura_button_name(uint8_t btn) {
    switch(btn) {
    case HONDA_ACURA_BTN_LOCK: return "Lock";
    case HONDA_ACURA_BTN_UNLOCK: return "Unlock";
    case HONDA_ACURA_BTN_TRUNK: return "Trunk";
    case HONDA_ACURA_BTN_PANIC: return "Panic";
    default: return "Unknown";
    }
}

// ─── Encoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_encoder_honda_acura_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderHondaAcura* instance = malloc(sizeof(SubGhzProtocolEncoderHondaAcura));
    instance->base.protocol = &subghz_protocol_honda_acura;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.size_upload = 1U + (2U * HONDA_ACURA_PREAMBLE_TX) + (2U * HONDA_ACURA_BIT_COUNT) + 1U;
    instance->encoder.upload =
        malloc(instance->encoder.size_upload * sizeof(LevelDuration));
    instance->encoder.repeat = 2;
    instance->encoder.is_running = false;
    return instance;
}

void subghz_protocol_encoder_honda_acura_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderHondaAcura* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_honda_acura_stop(void* context) {
    SubGhzProtocolEncoderHondaAcura* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_honda_acura_yield(void* context) {
    SubGhzProtocolEncoderHondaAcura* instance = context;
    if(instance->encoder.repeat == 0 || !instance->encoder.is_running) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = instance->encoder.upload[instance->encoder.front];
    if(++instance->encoder.front == instance->encoder.size_upload) {
        instance->encoder.repeat--;
        instance->encoder.front = 0;
    }
    return ret;
}

static bool honda_acura_encoder_get_upload(SubGhzProtocolEncoderHondaAcura* instance) {
    furi_assert(instance);
    uint64_t data = instance->generic.data;

    /* Apply the D-pad custom button: swap the 12-bit command field, keeping
     * the fob serial and counter, in whatever Manchester polarity was captured. */
    uint8_t custom = (subghz_custom_btn_get() == SUBGHZ_CUSTOM_BTN_OK) ?
                         subghz_custom_btn_get_original() :
                         subghz_custom_btn_get();
    if(custom != 0U) {
        const uint16_t orig_cmd = (uint16_t)((data >> 52) & 0x0FFFU);
        const bool inverted = (orig_cmd == (uint16_t)(~HONDA_ACURA_CMD_LOCK & 0x0FFFU)) ||
                              (orig_cmd == (uint16_t)(~HONDA_ACURA_CMD_UNLOCK & 0x0FFFU));
        const uint8_t new_btn = honda_acura_custom_to_btn(custom, instance->generic.btn);
        const uint16_t new_cmd = honda_acura_btn_to_cmd(new_btn, inverted);
        data = (data & ~((uint64_t)0x0FFFU << 52)) | ((uint64_t)new_cmd << 52);
        instance->generic.data = data;
        instance->generic.btn = new_btn;
    }

    /* Rolling counter: advance it on every transmit so each emulated frame is
     * a genuinely different code, exactly like the sibling car protocols
     * (renault/ford/kia). Without this the encoder re-emits a byte-identical
     * frame on every press — which is what a HackRF capture shows as "the
     * signal never changes". The counter lives in the low 16 bits of the
     * 64-bit frame (frame bits [15:0]). */
    uint16_t cnt = (uint16_t)(data & 0xFFFFU);
    cnt = (uint16_t)(cnt + furi_hal_subghz_get_rolling_counter_mult());
    data = (data & ~(uint64_t)0xFFFFU) | (uint64_t)cnt;
    instance->generic.data = data;
    instance->generic.cnt = cnt;

    size_t idx = 0;

    /* preamble: te_short square wave */
    for(size_t i = 0; i < HONDA_ACURA_PREAMBLE_TX; i++) {
        instance->encoder.upload[idx++] = level_duration_make(true, HONDA_ACURA_ELEMENT);
        instance->encoder.upload[idx++] = level_duration_make(false, HONDA_ACURA_ELEMENT);
    }
    /* sync gap */
    instance->encoder.upload[idx++] = level_duration_make(false, HONDA_ACURA_SYNC_US);
    /* data: Manchester MSB-first. bit0 -> low,high ; bit1 -> high,low */
    for(int b = (int)HONDA_ACURA_BIT_COUNT - 1; b >= 0; b--) {
        bool bit = (data >> b) & 1U;
        if(bit) {
            instance->encoder.upload[idx++] = level_duration_make(true, HONDA_ACURA_ELEMENT);
            instance->encoder.upload[idx++] = level_duration_make(false, HONDA_ACURA_ELEMENT);
        } else {
            instance->encoder.upload[idx++] = level_duration_make(false, HONDA_ACURA_ELEMENT);
            instance->encoder.upload[idx++] = level_duration_make(true, HONDA_ACURA_ELEMENT);
        }
    }
    instance->encoder.size_upload = idx;
    return true;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_honda_acura_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolEncoderHondaAcura* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_honda_acura_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;

    /* Register the D-pad button grid for the emulate view. */
    const uint16_t cmd = (uint16_t)((instance->generic.data >> 52) & 0x0FFFU);
    honda_acura_custom_btn_setup(honda_acura_cmd_to_btn(cmd));

    if(!honda_acura_encoder_get_upload(instance)) return SubGhzProtocolStatusErrorEncoderGetUpload;
    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}

// ─── Decoder ────────────────────────────────────────────────────────────────

void* subghz_protocol_decoder_honda_acura_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderHondaAcura* instance = malloc(sizeof(SubGhzProtocolDecoderHondaAcura));
    memset(instance, 0, sizeof(*instance));
    instance->base.protocol = &subghz_protocol_honda_acura;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_honda_acura_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_honda_acura_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaAcura* instance = context;
    instance->symbols_count = 0;
    instance->cmd = 0;
}

static bool honda_acura_parse_symbols(SubGhzProtocolDecoderHondaAcura* instance, bool inverted) {
    const uint16_t count = instance->symbols_count;
    if(count < HONDA_ACURA_MIN_SYMBOLS) return false;

    for(uint16_t start = 0U; (start < 16U) && ((uint16_t)(start + 1U) < count); start++) {
        uint8_t packet[8];
        if(!honda_acura_manchester_pack_64(instance->symbols, count, start, inverted, packet)) {
            continue;
        }
        const uint16_t cmd12 = (uint16_t)honda_acura_packet_bits(packet, 0, 12);
        const uint8_t btn = honda_acura_cmd_to_btn(cmd12);
        if(btn == 0U) continue;

        instance->cmd = cmd12;
        instance->generic.data = ((uint64_t)honda_acura_packet_bits(packet, 0, 32) << 32) |
                                 honda_acura_packet_bits(packet, 32, 32);
        instance->generic.data_count_bit = HONDA_ACURA_BIT_COUNT;
        instance->generic.serial = honda_acura_packet_bits(packet, 20, 28); /* device id */
        instance->generic.cnt = (uint16_t)honda_acura_packet_bits(packet, 48, 16); /* rolling */
        instance->generic.btn = btn;
        instance->decoder.decode_data = instance->generic.data;
        instance->decoder.decode_count_bit = HONDA_ACURA_BIT_COUNT;

        if(instance->base.callback) {
            instance->base.callback(&instance->base, instance->base.context);
        }
        return true;
    }
    return false;
}

void subghz_protocol_decoder_honda_acura_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaAcura* instance = context;
    const uint8_t sym = level ? 1U : 0U;

    if((duration >= HONDA_ACURA_SHORT_MIN) && (duration <= HONDA_ACURA_SHORT_MAX)) {
        if(instance->symbols_count < HONDA_ACURA_SYMBOL_CAP) {
            honda_acura_symbol_set(instance->symbols, instance->symbols_count, sym);
            instance->symbols_count++;
        }
        return;
    }
    if((duration >= HONDA_ACURA_LONG_MIN) && (duration <= HONDA_ACURA_LONG_MAX)) {
        if((uint16_t)(instance->symbols_count + 2U) <= HONDA_ACURA_SYMBOL_CAP) {
            honda_acura_symbol_set(instance->symbols, instance->symbols_count, sym);
            instance->symbols_count++;
            honda_acura_symbol_set(instance->symbols, instance->symbols_count, sym);
            instance->symbols_count++;
        }
        return;
    }

    /* gap / sync / silence: end of a symbol batch — try to decode it. */
    if(instance->symbols_count >= HONDA_ACURA_MIN_SYMBOLS) {
        if(!honda_acura_parse_symbols(instance, true)) {
            honda_acura_parse_symbols(instance, false);
        }
    }
    instance->symbols_count = 0;
}

uint8_t subghz_protocol_decoder_honda_acura_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaAcura* instance = context;
    return subghz_protocol_blocks_get_hash_data(
        &instance->decoder, (instance->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_honda_acura_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaAcura* instance = context;
    return subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
}

SubGhzProtocolStatus
    subghz_protocol_decoder_honda_acura_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaAcura* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret == SubGhzProtocolStatusOk) {
        instance->cmd = (uint16_t)((instance->generic.data >> 52) & 0x0FFFU);
        instance->generic.btn = honda_acura_cmd_to_btn(instance->cmd);
        /* Register the D-pad button grid when a saved signal is loaded. */
        honda_acura_custom_btn_setup(instance->generic.btn);
    }
    return ret;
}

void subghz_protocol_decoder_honda_acura_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderHondaAcura* instance = context;

    furi_string_cat_printf(
        output,
        "Honda/Acura 313/433MHz\r\n"
        "Key:%016llX\r\n"
        "Sn:%07lX  Cnt:%04lX\r\n"
        "Cmd:%03X Btn:[%s]  %dbit",
        (unsigned long long)instance->generic.data,
        (unsigned long)instance->generic.serial,
        (unsigned long)instance->generic.cnt,
        instance->cmd,
        honda_acura_button_name(instance->generic.btn),
        instance->generic.data_count_bit);
}
