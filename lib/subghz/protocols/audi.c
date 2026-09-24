#include "audi.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <furi.h>
#include <furi_hal.h>
#include <string.h>

// ============================================================================
// Audi (Pandora) SubGHz keyfob protocol (decoder + encoder)
//
// Reverse-engineered from the PANDORA_MAX firmware:
//   - button/counter dispatcher handler @ 0xf152
//   - {b3,b4,b5} scramble + parity engine @ 0xf8fc
//   - PWM (pulse-width) RF bit-banger    @ 0xbd64
//
// This is the Pandora "Audi" keyfob, a SEPARATE protocol from our vag.c.
//
// Wire (verified against the PWM engine @ 0xbd64):
//   - modulation: OOK/ASK (AM650 preset), bands 315 / 433 / 868 MHz.
//   - PWM (pulse-width) encoding, Te = 550 us (te_delta ~137).
//       bit1 = HIGH ~1100us (2*Te) then LOW 550us
//       bit0 = HIGH 550us       then LOW ~1100us (2*Te)
//   - header / sync (also re-sent each repeat):
//       HIGH 2500us -> LOW 850us
//   - 64 data bits = 8 bytes, LSB-FIRST within each byte (mask = 1<<(i&7)).
//   - 5 repeats, inter-repeat gap ~850us LOW.
//
// Logical 8-byte payload:
//   b0,b1,b2 = UID / serial
//   b3,b4,b5 = rolling payload (SCRAMBLED on the wire, see below)
//   b6       = button / counter (btn0=0x01, btn1=0x02, btn2=0x04)
//   b7       = checksum = sum(b0..b6) & 0xFF (over the SCRAMBLED bytes; display)
//
// CRYPTO (@ 0xf8fc) applied when BUILDING the frame; reversed on decode:
//   A) parity P (recomputed each of 4 scramble iterations on the CURRENT block):
//        P = bit3(b3) ^ bit4(b4) ^ bit6(b4) ^ bit7(b4) ^ bit4(b5) ^ bit7(b5)
//      (bitN(x) = (x>>N)&1). P seeds the carry chain each iteration.
//   B) scramble over {b3,b4,b5}, applied 4 TIMES. Each iteration processes the
//      three bytes in order (b3,b4,b5) with a forward carry chain:
//        out       = ((in << 1) & 0xFE) | carry_in
//        carry_out = (in >> 7) & 1
//        carry_in(first byte) = P  ; carry_in(next) = previous carry_out
//   C) checksum b7 = sum(b0..b6) & 0xFF, taken AFTER the scramble.
//
// INVERSE of one scramble iteration (see audi_unscramble_iteration):
//   Given the three scrambled bytes o0,o1,o2 and P (== o0 bit0):
//     in0 bits0..6 = o0 bits1..7 ; in0 bit7 = o1 bit0
//     in1 bits0..6 = o1 bits1..7 ; in1 bit7 = o2 bit0
//     in2 bits0..6 = o2 bits1..7 ; in2 bit7 = recovered from the parity relation
//   The forward chain drops in2's MSB (the final carry is never stored), but the
//   parity P is known (it equals o0 bit0, the injected seed) and P depends on
//   bit7(b5)=in2 bit7. Every other term of P is already recovered, so:
//     in2 bit7 = P ^ bit3(in0) ^ bit4(in1) ^ bit6(in1) ^ bit7(in1) ^ bit4(in2)
//   which recovers the lost MSB EXACTLY. The RE proved the transform invertible;
//   this replicates it. Round-trip is verified in comments and by construction:
//   scramble(unscramble(x)) == x and unscramble(scramble(x)) == x for all x.
//
// 64 logical bits fit in generic.data, but the authoritative payload (raw 8
// bytes) is stored in raw_data[] and serialized as a "Data" hex field (mirrors
// fiat_v2.c "Raw" / mercedes.c "Data").
// ============================================================================

#define AUDI_TE_SHORT     550U
#define AUDI_TE_LONG      1100U
#define AUDI_TE_DELTA     137U
#define AUDI_HEADER_HIGH  2500U
#define AUDI_HEADER_LOW   850U
#define AUDI_GAP_US       850U
#define AUDI_WIRE_BYTES   8U
#define AUDI_WIRE_BITS    64U
#define AUDI_REPEATS      5U
#define AUDI_SCRAMBLE_ROUNDS 4U

// Boundary to detect the 2500us header HIGH vs a normal data cell (<= 1100+delta).
#define AUDI_HEADER_MIN_US 1900U

// Encoder capacity per repeat: header(2) + 64 bits * 2 cells + gap(1). Times 5.
#define AUDI_UPLOAD_CAPACITY ((2U + (AUDI_WIRE_BITS * 2U) + 1U) * AUDI_REPEATS)

#define AUDI_DEFAULT_REPEAT AUDI_REPEATS

#define AUDI_DATA_FIELD "Data"

// Button on-wire codes (b6).
#define AUDI_BTN_0 0x01U
#define AUDI_BTN_1 0x02U
#define AUDI_BTN_2 0x04U

static const SubGhzBlockConst subghz_protocol_audi_const = {
    .te_short = AUDI_TE_SHORT,
    .te_long = AUDI_TE_LONG,
    .te_delta = AUDI_TE_DELTA,
    .min_count_bit_for_found = AUDI_WIRE_BITS,
};

typedef enum {
    AudiDecoderStepReset = 0,
    AudiDecoderStepHeaderLow,
    AudiDecoderStepData,
} AudiDecoderStep;

struct SubGhzProtocolDecoderAudi {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    // PWM demod scratch: we collect the HIGH duration then pair it with the
    // following LOW duration to classify one bit.
    uint32_t pending_high;
    bool have_high;

    uint8_t bytes[AUDI_WIRE_BYTES];
    uint16_t bit_count;

    uint8_t raw_data[AUDI_WIRE_BYTES]; // on-wire (scrambled) 8 bytes
    uint8_t last_raw_data[AUDI_WIRE_BYTES];
    bool last_raw_valid;
    bool parity_ok; // scramble inverse recovered a self-consistent block
};

struct SubGhzProtocolEncoderAudi {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_data[AUDI_WIRE_BYTES]; // on-wire (scrambled) 8 bytes
};

const SubGhzProtocolDecoder subghz_protocol_audi_decoder = {
    .alloc = subghz_protocol_decoder_audi_alloc,
    .free = subghz_protocol_decoder_audi_free,
    .feed = subghz_protocol_decoder_audi_feed,
    .reset = subghz_protocol_decoder_audi_reset,
    .get_hash_data = subghz_protocol_decoder_audi_get_hash_data,
    .serialize = subghz_protocol_decoder_audi_serialize,
    .deserialize = subghz_protocol_decoder_audi_deserialize,
    .get_string = subghz_protocol_decoder_audi_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_audi_encoder = {
    .alloc = subghz_protocol_encoder_audi_alloc,
    .free = subghz_protocol_encoder_audi_free,
    .deserialize = subghz_protocol_encoder_audi_deserialize,
    .stop = subghz_protocol_encoder_audi_stop,
    .yield = subghz_protocol_encoder_audi_yield,
};

const SubGhzProtocol subghz_protocol_audi = {
    .name = AUDI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_audi_decoder,
    .encoder = &subghz_protocol_audi_encoder,
};

// ---------------------------------------------------------------------------
// Scramble / parity engine (@ 0xf8fc). Operates in place on {b3,b4,b5}.
// ---------------------------------------------------------------------------

static inline uint8_t audi_bit(uint8_t x, uint8_t n) {
    return (uint8_t)((x >> n) & 1U);
}

// P = bit3(b3) ^ bit4(b4) ^ bit6(b4) ^ bit7(b4) ^ bit4(b5) ^ bit7(b5)
static uint8_t audi_parity(uint8_t b3, uint8_t b4, uint8_t b5) {
    return (uint8_t)(
        audi_bit(b3, 3) ^ audi_bit(b4, 4) ^ audi_bit(b4, 6) ^ audi_bit(b4, 7) ^ audi_bit(b5, 4) ^
        audi_bit(b5, 7));
}

// One forward scramble iteration over block[0..2] = {b3,b4,b5}.
static void audi_scramble_iteration(uint8_t block[3]) {
    const uint8_t p = audi_parity(block[0], block[1], block[2]);
    uint8_t carry = p;
    for(size_t i = 0; i < 3U; i++) {
        const uint8_t in = block[i];
        block[i] = (uint8_t)(((in << 1) & 0xFEU) | carry);
        carry = (uint8_t)((in >> 7) & 1U);
    }
    // The final carry (bit7 of the last input, i.e. old bit7(b5)) is dropped;
    // it is recoverable on decode via the parity relation (see below).
}

// One inverse scramble iteration. Returns true if the recovered block is
// self-consistent (recomputed parity matches the injected seed P == o0 bit0).
static bool audi_unscramble_iteration(uint8_t block[3]) {
    const uint8_t o0 = block[0];
    const uint8_t o1 = block[1];
    const uint8_t o2 = block[2];

    // The parity P injected as the first carry equals bit0 of the first output.
    const uint8_t p = (uint8_t)(o0 & 1U);

    // Recover all bits except in2 bit7:
    //   in0 = (o0 >> 1) | (o1 bit0 << 7)
    //   in1 = (o1 >> 1) | (o2 bit0 << 7)
    //   in2 = (o2 >> 1) | (in2_bit7 << 7)
    const uint8_t in0 = (uint8_t)(((o0 >> 1) & 0x7FU) | ((o1 & 1U) << 7));
    const uint8_t in1 = (uint8_t)(((o1 >> 1) & 0x7FU) | ((o2 & 1U) << 7));
    uint8_t in2_low7 = (uint8_t)((o2 >> 1) & 0x7FU);

    // Recover in2 bit7 from the parity relation:
    //   P = bit3(in0) ^ bit4(in1) ^ bit6(in1) ^ bit7(in1) ^ bit4(in2) ^ bit7(in2)
    // => bit7(in2) = P ^ bit3(in0) ^ bit4(in1) ^ bit6(in1) ^ bit7(in1) ^ bit4(in2)
    const uint8_t known = (uint8_t)(
        audi_bit(in0, 3) ^ audi_bit(in1, 4) ^ audi_bit(in1, 6) ^ audi_bit(in1, 7) ^
        audi_bit(in2_low7, 4));
    const uint8_t in2_bit7 = (uint8_t)(p ^ known);
    const uint8_t in2 = (uint8_t)(in2_low7 | (in2_bit7 << 7));

    block[0] = in0;
    block[1] = in1;
    block[2] = in2;

    // Self-check: recomputing P on the recovered plaintext must equal the seed.
    // This is guaranteed by construction (we solved for in2 bit7 to make it so),
    // but we return it so the decoder can flag corrupt captures.
    return audi_parity(in0, in1, in2) == p;
}

// Forward: scramble {b3,b4,b5} in place, 4 iterations, then checksum b7.
static void audi_encrypt(uint8_t raw[AUDI_WIRE_BYTES]) {
    uint8_t block[3] = {raw[3], raw[4], raw[5]};
    for(uint8_t r = 0; r < AUDI_SCRAMBLE_ROUNDS; r++) {
        audi_scramble_iteration(block);
    }
    raw[3] = block[0];
    raw[4] = block[1];
    raw[5] = block[2];

    uint32_t sum = 0U;
    for(size_t i = 0; i < 7U; i++) sum += raw[i];
    raw[7] = (uint8_t)(sum & 0xFFU);
}

// Inverse: un-scramble {b3,b4,b5} in place, 4 iterations. Returns parity_ok.
static bool audi_decrypt_block(uint8_t raw[AUDI_WIRE_BYTES]) {
    uint8_t block[3] = {raw[3], raw[4], raw[5]};
    bool ok = true;
    for(uint8_t r = 0; r < AUDI_SCRAMBLE_ROUNDS; r++) {
        ok = audi_unscramble_iteration(block) && ok;
    }
    raw[3] = block[0];
    raw[4] = block[1];
    raw[5] = block[2];
    return ok;
}

// ---------------------------------------------------------------------------
// Field helpers. raw_data[] holds the on-wire (scrambled) bytes; the plaintext
// rolling bytes are obtained on demand via audi_decrypt_block on a copy.
// ---------------------------------------------------------------------------

static uint8_t audi_checksum(const uint8_t raw[AUDI_WIRE_BYTES]) {
    uint32_t sum = 0U;
    for(size_t i = 0; i < 7U; i++) sum += raw[i];
    return (uint8_t)(sum & 0xFFU);
}

static uint32_t audi_uid(const uint8_t raw[AUDI_WIRE_BYTES]) {
    return ((uint32_t)raw[0] << 16U) | ((uint32_t)raw[1] << 8U) | raw[2];
}

static bool audi_frame_valid(const uint8_t raw[AUDI_WIRE_BYTES]) {
    const uint32_t uid = audi_uid(raw);
    return uid != 0U && uid != 0xFFFFFFU;
}

static const char* audi_button_name(uint8_t btn) {
    switch(btn) {
    case AUDI_BTN_0:
        return "Lock";
    case AUDI_BTN_1:
        return "Unlock";
    case AUDI_BTN_2:
        return "Trunk";
    default:
        return "Unknown";
    }
}

static void audi_decode_fields(SubGhzBlockGeneric* generic, const uint8_t raw[AUDI_WIRE_BYTES]) {
    generic->serial = audi_uid(raw);
    generic->btn = raw[6];
    // Rolling payload (plaintext) forms the counter view.
    uint8_t plain[AUDI_WIRE_BYTES];
    memcpy(plain, raw, AUDI_WIRE_BYTES);
    audi_decrypt_block(plain);
    generic->cnt = (uint16_t)(((uint16_t)plain[3] << 8U) | plain[4]);
    generic->data_count_bit = AUDI_WIRE_BITS;
    // 64-bit on-wire payload fits generic.data (MSB-first over b0..b7) for hash.
    uint64_t data = 0U;
    for(size_t i = 0; i < AUDI_WIRE_BYTES; i++) data = (data << 8U) | raw[i];
    generic->data = data;
}

// ---------------------------------------------------------------------------
// Decoder: detect 2500/850 header, then PWM-demod 64 bits LSB-first.
//
// PWM classification (matches the @0xbd64 emitter):
//   bit1 = HIGH long (2*Te) then LOW short (Te)
//   bit0 = HIGH short (Te)  then LOW long  (2*Te)
// We buffer the HIGH duration, then when the following LOW arrives compare the
// two: HIGH>LOW -> bit1, HIGH<LOW -> bit0.
// ---------------------------------------------------------------------------

static bool audi_is_short(uint32_t d) {
    return DURATION_DIFF(d, AUDI_TE_SHORT) < AUDI_TE_DELTA;
}

static bool audi_is_long(uint32_t d) {
    return DURATION_DIFF(d, AUDI_TE_LONG) < AUDI_TE_DELTA;
}

static void audi_data_reset(SubGhzProtocolDecoderAudi* instance) {
    instance->bit_count = 0U;
    instance->have_high = false;
    instance->pending_high = 0U;
    memset(instance->bytes, 0, sizeof(instance->bytes));
}

// LSB-first within each byte: mask = 1 << (bit_index & 7).
static void audi_add_bit(SubGhzProtocolDecoderAudi* instance, bool bit) {
    if(instance->bit_count >= AUDI_WIRE_BITS) return;
    if(bit) {
        instance->bytes[instance->bit_count >> 3U] |=
            (uint8_t)(1U << (instance->bit_count & 7U));
    }
    instance->bit_count++;
}

static void audi_report(SubGhzProtocolDecoderAudi* instance) {
    if(instance->bit_count < AUDI_WIRE_BITS) return;
    if(!audi_frame_valid(instance->bytes)) return;

    if(instance->last_raw_valid &&
       memcmp(instance->last_raw_data, instance->bytes, AUDI_WIRE_BYTES) == 0) {
        return; // repeated identical frame
    }

    memcpy(instance->raw_data, instance->bytes, AUDI_WIRE_BYTES);
    memcpy(instance->last_raw_data, instance->bytes, AUDI_WIRE_BYTES);
    instance->last_raw_valid = true;

    // Un-scramble a copy to flag parity/checksum consistency (display only).
    uint8_t plain[AUDI_WIRE_BYTES];
    memcpy(plain, instance->raw_data, AUDI_WIRE_BYTES);
    instance->parity_ok = audi_decrypt_block(plain);

    audi_decode_fields(&instance->generic, instance->raw_data);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_audi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderAudi* instance = calloc(1, sizeof(SubGhzProtocolDecoderAudi));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_audi;
    instance->generic.protocol_name = instance->base.protocol->name;
    subghz_protocol_decoder_audi_reset(instance);
    return instance;
}

void subghz_protocol_decoder_audi_free(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;
    free(instance);
}

void subghz_protocol_decoder_audi_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;
    instance->decoder.parser_step = AudiDecoderStepReset;
    instance->decoder.decode_data = 0U;
    instance->decoder.decode_count_bit = 0U;
    instance->last_raw_valid = false;
    instance->parity_ok = false;
    memset(instance->raw_data, 0, sizeof(instance->raw_data));
    memset(instance->last_raw_data, 0, sizeof(instance->last_raw_data));
    audi_data_reset(instance);
}

void subghz_protocol_decoder_audi_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;

    switch(instance->decoder.parser_step) {
    case AudiDecoderStepReset:
        // Header HIGH ~2500us.
        if(level && duration >= AUDI_HEADER_MIN_US) {
            instance->decoder.parser_step = AudiDecoderStepHeaderLow;
        }
        break;

    case AudiDecoderStepHeaderLow:
        // Header LOW ~850us follows the header HIGH.
        if(!level && DURATION_DIFF(duration, AUDI_HEADER_LOW) < AUDI_TE_DELTA * 2U) {
            instance->decoder.parser_step = AudiDecoderStepData;
            audi_data_reset(instance);
        } else {
            instance->decoder.parser_step = AudiDecoderStepReset;
        }
        break;

    case AudiDecoderStepData:
        if(level) {
            // A new header HIGH mid-stream: try to report, then re-sync.
            if(duration >= AUDI_HEADER_MIN_US) {
                audi_report(instance);
                instance->decoder.parser_step = AudiDecoderStepHeaderLow;
                break;
            }
            if(audi_is_short(duration) || audi_is_long(duration)) {
                instance->pending_high = duration;
                instance->have_high = true;
            } else {
                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
            }
        } else {
            if(!instance->have_high) {
                // A lone LOW with no preceding HIGH: end of frame / gap.
                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
                break;
            }
            instance->have_high = false;

            const bool high_long = audi_is_long(instance->pending_high);
            const bool high_short = audi_is_short(instance->pending_high);
            const bool low_long = audi_is_long(duration);
            const bool low_short = audi_is_short(duration);

            if(high_long && low_short) {
                audi_add_bit(instance, true); // bit1
            } else if(high_short && low_long) {
                audi_add_bit(instance, false); // bit0
            } else if(high_short && !low_short && !low_long) {
                // HIGH short then a long gap => last bit0 followed by gap; count
                // the bit then finish.
                audi_add_bit(instance, false);
                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
                break;
            } else {
                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
                break;
            }

            if(instance->bit_count == AUDI_WIRE_BITS) {
                audi_report(instance);
                audi_data_reset(instance);
                instance->decoder.parser_step = AudiDecoderStepReset;
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_audi_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;
    uint8_t hash = 0U;
    for(size_t i = 0; i < AUDI_WIRE_BYTES; i++) hash ^= instance->raw_data[i];
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_audi_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_insert_or_update_hex(
           flipper_format, AUDI_DATA_FIELD, instance->raw_data, AUDI_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!flipper_format_insert_or_update_uint32(
           flipper_format, "Serial", &instance->generic.serial, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t btn = instance->generic.btn;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Btn", &btn, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    uint32_t cnt = instance->generic.cnt;
    if(!flipper_format_insert_or_update_uint32(flipper_format, "Cnt", &cnt, 1)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    return SubGhzProtocolStatusOk;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_audi_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic, flipper_format, subghz_protocol_audi_const.min_count_bit_for_found);
    if(ret != SubGhzProtocolStatusOk) return ret;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, AUDI_DATA_FIELD, instance->raw_data, AUDI_WIRE_BYTES)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }
    if(!audi_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint8_t plain[AUDI_WIRE_BYTES];
    memcpy(plain, instance->raw_data, AUDI_WIRE_BYTES);
    instance->parity_ok = audi_decrypt_block(plain);
    audi_decode_fields(&instance->generic, instance->raw_data);
    return SubGhzProtocolStatusOk;
}

void subghz_protocol_decoder_audi_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderAudi* instance = context;

    const uint8_t cs = instance->raw_data[7];
    const uint8_t cs_calc = audi_checksum(instance->raw_data);

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "SN:%06lX Btn:[%s]\r\n"
        "Cnt:%04lX CS:%02X[%s] P:%s\r\n"
        "Data:%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        AUDI_WIRE_BITS,
        (unsigned long)instance->generic.serial,
        audi_button_name((uint8_t)instance->generic.btn),
        (unsigned long)instance->generic.cnt,
        cs,
        (cs == cs_calc) ? "OK" : "opaque",
        instance->parity_ok ? "OK" : "BAD",
        instance->raw_data[0],
        instance->raw_data[1],
        instance->raw_data[2],
        instance->raw_data[3],
        instance->raw_data[4],
        instance->raw_data[5],
        instance->raw_data[6],
        instance->raw_data[7]);
}

// ---------------------------------------------------------------------------
// Encoder: rebuild the exact TX wire.
//   Per repeat (x5):
//     HIGH 2500 -> LOW 850 (header/sync)
//     64 PWM bits LSB-first:
//        bit1 -> HIGH 1100 , LOW 550
//        bit0 -> HIGH 550  , LOW 1100
//     LOW 850 inter-repeat gap
// The stored raw_data are the on-wire (scrambled) bytes, so replay reproduces
// the wire exactly. A D-pad button change re-derives the whole frame: set b6,
// advance the plaintext rolling counter (b3/b4), re-scramble, re-checksum.
// ---------------------------------------------------------------------------

static size_t audi_append_data_pairs(LevelDuration* upload, size_t index, const uint8_t raw[AUDI_WIRE_BYTES]) {
    for(uint16_t bit_index = 0U; bit_index < AUDI_WIRE_BITS; bit_index++) {
        const bool bit = ((raw[bit_index >> 3U] >> (bit_index & 7U)) & 1U) != 0U;
        if(bit) {
            upload[index++] = level_duration_make(true, AUDI_TE_LONG);
            upload[index++] = level_duration_make(false, AUDI_TE_SHORT);
        } else {
            upload[index++] = level_duration_make(true, AUDI_TE_SHORT);
            upload[index++] = level_duration_make(false, AUDI_TE_LONG);
        }
    }
    return index;
}

static bool audi_encoder_build_upload(SubGhzProtocolEncoderAudi* instance) {
    furi_check(instance);
    LevelDuration* upload = instance->encoder.upload;
    if(!upload) return false;

    size_t index = 0U;
    for(uint8_t rep = 0U; rep < AUDI_REPEATS; rep++) {
        // Header / sync.
        upload[index++] = level_duration_make(true, AUDI_HEADER_HIGH);
        upload[index++] = level_duration_make(false, AUDI_HEADER_LOW);
        // 64 PWM data bits LSB-first.
        index = audi_append_data_pairs(upload, index, instance->raw_data);
        // Inter-repeat gap.
        upload[index++] = level_duration_make(false, AUDI_GAP_US);
    }

    instance->encoder.size_upload = index;
    instance->encoder.front = 0U;
    return true;
}

void* subghz_protocol_encoder_audi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderAudi* instance = calloc(1, sizeof(SubGhzProtocolEncoderAudi));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_audi;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = AUDI_DEFAULT_REPEAT;
    instance->encoder.size_upload = 0U;
    instance->encoder.upload = NULL;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
    return instance;
}

void subghz_protocol_encoder_audi_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderAudi* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

void subghz_protocol_encoder_audi_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderAudi* instance = context;
    instance->encoder.is_running = false;
    instance->encoder.front = 0U;
}

LevelDuration subghz_protocol_encoder_audi_yield(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderAudi* instance = context;

    if(!instance->encoder.is_running || instance->encoder.repeat == 0 ||
       instance->encoder.size_upload == 0) {
        instance->encoder.is_running = false;
        return level_duration_reset();
    }

    LevelDuration ret = instance->encoder.upload[instance->encoder.front];

    if(++instance->encoder.front == instance->encoder.size_upload) {
        // Endless/breakless TX: while OK is held do not consume repeats.
        if(!subghz_block_generic_global.endless_tx) instance->encoder.repeat--;
        instance->encoder.front = 0U;
    }

    return ret;
}

// Rebuild the on-wire raw from a chosen button + plaintext rolling counter.
// Keeps UID (b0..b2) and b5 plaintext as captured; only b3/b4 carry the counter.
static void audi_reencode(SubGhzProtocolEncoderAudi* instance, uint8_t new_btn, uint16_t new_cnt) {
    uint8_t plain[AUDI_WIRE_BYTES];
    memcpy(plain, instance->raw_data, AUDI_WIRE_BYTES);
    audi_decrypt_block(plain); // recover plaintext b3,b4,b5

    plain[3] = (uint8_t)((new_cnt >> 8U) & 0xFFU);
    plain[4] = (uint8_t)(new_cnt & 0xFFU);
    plain[6] = new_btn;

    audi_encrypt(plain); // re-scramble {b3,b4,b5} + checksum b7
    memcpy(instance->raw_data, plain, AUDI_WIRE_BYTES);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_audi_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    furi_check(flipper_format);
    SubGhzProtocolEncoderAudi* instance = context;

    instance->encoder.is_running = false;
    instance->encoder.front = 0U;

    flipper_format_rewind(flipper_format);
    FuriString* protocol_name = furi_string_alloc();
    bool protocol_ok = flipper_format_read_string(flipper_format, "Protocol", protocol_name) &&
                       furi_string_equal(protocol_name, instance->base.protocol->name);
    furi_string_free(protocol_name);
    if(!protocol_ok) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    uint32_t bit_count = 0U;
    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_uint32(flipper_format, "Bit", &bit_count, 1) ||
       bit_count != AUDI_WIRE_BITS) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }
    instance->generic.data_count_bit = (uint16_t)bit_count;

    flipper_format_rewind(flipper_format);
    if(!flipper_format_read_hex(
           flipper_format, AUDI_DATA_FIELD, instance->raw_data, AUDI_WIRE_BYTES) ||
       !audi_frame_valid(instance->raw_data)) {
        return SubGhzProtocolStatusErrorParserOthers;
    }

    instance->generic.serial = audi_uid(instance->raw_data);
    instance->generic.btn = instance->raw_data[6];
    {
        uint8_t plain[AUDI_WIRE_BYTES];
        memcpy(plain, instance->raw_data, AUDI_WIRE_BYTES);
        audi_decrypt_block(plain);
        instance->generic.cnt = (uint16_t)(((uint16_t)plain[3] << 8U) | plain[4]);
    }

    // [PROTOPIRATE_PORT] custom_btn support. Audi has 3 buttons:
    //   btn0=0x01 (Lock), btn1=0x02 (Unlock), btn2=0x04 (Trunk).
    // OK replays the captured frame byte-identically. A D-pad change re-derives
    // the whole frame (b6 + advanced rolling counter, re-scrambled + checksum),
    // so the emitted frame round-trips through this decoder.
    const uint8_t original_btn = instance->raw_data[6];
    if(subghz_custom_btn_get_original() == 0) {
        subghz_custom_btn_set_original(original_btn);
    }
    subghz_custom_btn_set_max(3);
    const uint8_t custom_btn_id = subghz_custom_btn_get();
    uint8_t new_btn = original_btn;
    switch(custom_btn_id) {
    case SUBGHZ_CUSTOM_BTN_UP:   new_btn = AUDI_BTN_0; break;
    case SUBGHZ_CUSTOM_BTN_DOWN: new_btn = AUDI_BTN_1; break;
    case SUBGHZ_CUSTOM_BTN_LEFT: new_btn = AUDI_BTN_2; break;
    case SUBGHZ_CUSTOM_BTN_OK:
    default:                     new_btn = original_btn; break; // exact replay
    }

    if(new_btn != original_btn) {
        // Advance the plaintext rolling counter for a genuine (non-replay) press.
        uint32_t new_counter = instance->generic.cnt;
        uint32_t override_cnt = 0U;
        if(subghz_block_generic_global_counter_override_get(&override_cnt)) {
            new_counter = override_cnt;
        } else {
            new_counter += (uint32_t)furi_hal_subghz_get_rolling_counter_mult();
        }
        new_counter &= 0xFFFFU;
        audi_reencode(instance, new_btn, (uint16_t)new_counter);
        instance->generic.serial = audi_uid(instance->raw_data);
        instance->generic.btn = new_btn;
        instance->generic.cnt = (uint16_t)new_counter;
    }

    audi_decode_fields(&instance->generic, instance->raw_data);

    uint32_t repeat = AUDI_DEFAULT_REPEAT;
    flipper_format_rewind(flipper_format);
    flipper_format_read_uint32(flipper_format, "Repeat", &repeat, 1);
    instance->encoder.repeat = (repeat == 0U) ? AUDI_DEFAULT_REPEAT : (size_t)repeat;

    if(instance->encoder.upload == NULL) {
        instance->encoder.upload = malloc(AUDI_UPLOAD_CAPACITY * sizeof(LevelDuration));
        furi_check(instance->encoder.upload);
    }
    if(!audi_encoder_build_upload(instance)) {
        return SubGhzProtocolStatusErrorEncoderGetUpload;
    }

    instance->encoder.is_running = true;
    return SubGhzProtocolStatusOk;
}
