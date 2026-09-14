// BMW CAS4 / FEM-BDC remote keyless entry (sub-GHz RKE) decoder.
//
// Reverse-engineered from real 2023 BMW X5 captures at 433.92 MHz OOK. The
// prototype decoder (validated out-of-firmware against the raw captures) lives
// in tools/bmw_cas4_decode.py. Reference implementation notes:
//
//   Modulation : OOK / ASK
//   Encoding   : PWM  (fixed ~250us HIGH pulse, then a LOW gap)
//                  gap ~500us  -> bit 0
//                  gap ~1500us -> bit 1
//   Wakeup/sync: ~25000us HIGH + ~20000us gap (NOT a data bit)
//   End marker : ~75000us HIGH + ~30000us gap
//   Frame      : first data byte is 0x45 (shared marker/preamble),
//                byte 1 = command (0x13 = Lock, 0x12 = Unlock; high nibble 0x1),
//                the remaining bytes are the rolling / encrypted authenticator
//                (needs the fob key; not decoded).
//   Length     : variable per command (lock = 64 data bits, unlock = 89 bits).
//                Some frames carry a stray leading 0 before the 0x45; the decoder
//                aligns to the 0x45 marker.
//   Repeats    : the fob repeats the identical frame ~3x per press.
//
// This is a DECODER + replay. A real re-encoder (rolling code) is not possible
// without the fob key, so the encoder is a replay/placeholder like other
// rolling-code protocols.

#include "bmw_cas4.h"

#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#include <stdlib.h>
#include <string.h>

#define TAG "BmwCas4"

// --- timing (microseconds) ---
// [FALSE_POSITIVE_FIX] Tight tolerances. BMW CAS4 shares 433.92 MHz OOK with
// many other car remotes, so loose te_delta would let unrelated PWM bursts
// decode as BMW (the Toyota-style false-positive trap). Real captures are exact
// 250/500/1500us; deltas below cover RX jitter but keep bit0/bit1 well separated
// (short 300..700, long 1300..1700, wide 700..1300 dead zone) and the pulse
// window tight around 250us.
#define BMW_CAS4_TE              250u
#define BMW_CAS4_GAP_SHORT       500u // bit 0
#define BMW_CAS4_GAP_LONG        1500u // bit 1
#define BMW_CAS4_GAP_DELTA       200u // 40% of short, 13% of long; non-overlapping
#define BMW_CAS4_PULSE_MIN       150u
#define BMW_CAS4_PULSE_MAX       380u
// [FALSE_POSITIVE_FIX] The BMW wakeup is a very distinctive ~25ms HIGH pulse.
// Requiring it to fall in a 15..35ms window (rather than "anything long") is the
// single strongest discriminator against other 433.92 MHz OOK remotes, none of
// which precede their data with a ~25ms carrier burst. A separate, larger
// threshold detects the end/repeat boundary.
#define BMW_CAS4_WAKEUP_MIN      15000u // wakeup HIGH pulse lower bound (~25000)
#define BMW_CAS4_WAKEUP_MAX      35000u // wakeup HIGH pulse upper bound
#define BMW_CAS4_LONG_MIN        8000u // any HIGH/LOW >= this ends the current frame

// --- frame model ---
#define BMW_CAS4_PREAMBLE_BYTE   0x45u // shared first data byte (lock & unlock)
#define BMW_CAS4_CMD_HI_NIBBLE   0x10u // command byte high nibble (0x1x)
#define BMW_CAS4_MIN_DATA_BITS   60u
#define BMW_CAS4_MAX_DATA_BITS   128u
#define BMW_CAS4_MAX_BYTES       16u // 128 bits

#define BMW_CAS4_CMD_LOCK        0x13u
#define BMW_CAS4_CMD_UNLOCK      0x12u

typedef enum {
    BmwCas4DecoderStepReset = 0,
    BmwCas4DecoderStepData,
} BmwCas4DecoderStep;

struct SubGhzProtocolDecoderBmwCas4 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t decoder_state;
    uint8_t raw_bits[BMW_CAS4_MAX_DATA_BITS]; // one entry per demodulated bit (0/1)
    uint16_t bit_count;

    // Aligned payload (after the 0x45 marker search), packed MSB-first.
    uint8_t data[BMW_CAS4_MAX_BYTES];
    uint16_t data_bit_count;
    uint8_t cmd_byte;
};

struct SubGhzProtocolEncoderBmwCas4 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

const SubGhzProtocolDecoder subghz_protocol_bmw_cas4_decoder = {
    .alloc = subghz_protocol_decoder_bmw_cas4_alloc,
    .free = subghz_protocol_decoder_bmw_cas4_free,
    .feed = subghz_protocol_decoder_bmw_cas4_feed,
    .reset = subghz_protocol_decoder_bmw_cas4_reset,
    .get_hash_data = subghz_protocol_decoder_bmw_cas4_get_hash_data,
    .serialize = subghz_protocol_decoder_bmw_cas4_serialize,
    .deserialize = subghz_protocol_decoder_bmw_cas4_deserialize,
    .get_string = subghz_protocol_decoder_bmw_cas4_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_bmw_cas4_encoder = {
    .alloc = subghz_protocol_encoder_bmw_cas4_alloc,
    .free = subghz_protocol_encoder_bmw_cas4_free,
    .deserialize = subghz_protocol_encoder_bmw_cas4_deserialize,
    .stop = subghz_protocol_encoder_bmw_cas4_stop,
    .yield = subghz_protocol_encoder_bmw_cas4_yield,
};

const SubGhzProtocol subghz_protocol_bmw_cas4 = {
    .name = BMW_CAS4_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_868 |
            SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
            SubGhzProtocolFlag_Save,
    .decoder = &subghz_protocol_bmw_cas4_decoder,
    .encoder = &subghz_protocol_bmw_cas4_encoder,
};

// ---------------------------------------------------------------------------
// Encoder — placeholder (rolling code; cannot re-encode without the fob key)
// ---------------------------------------------------------------------------

void* subghz_protocol_encoder_bmw_cas4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderBmwCas4* instance = calloc(1, sizeof(SubGhzProtocolEncoderBmwCas4));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_bmw_cas4;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.is_running = false;
    instance->encoder.size_upload = 1;
    instance->encoder.upload = malloc(sizeof(LevelDuration));
    furi_check(instance->encoder.upload);
    return instance;
}

void subghz_protocol_encoder_bmw_cas4_free(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderBmwCas4* instance = context;
    free(instance->encoder.upload);
    free(instance);
}

SubGhzProtocolStatus
    subghz_protocol_encoder_bmw_cas4_deserialize(void* context, FlipperFormat* flipper_format) {
    UNUSED(context);
    UNUSED(flipper_format);
    // TODO: replay-from-Raw encoder. BMW CAS4 is a rolling code; a real
    // re-encode needs the fob key. Left as a placeholder like other
    // rolling-code protocols.
    return SubGhzProtocolStatusError;
}

void subghz_protocol_encoder_bmw_cas4_stop(void* context) {
    furi_check(context);
    SubGhzProtocolEncoderBmwCas4* instance = context;
    instance->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_bmw_cas4_yield(void* context) {
    UNUSED(context);
    return level_duration_reset();
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

void* subghz_protocol_decoder_bmw_cas4_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderBmwCas4* instance = calloc(1, sizeof(SubGhzProtocolDecoderBmwCas4));
    furi_check(instance);
    instance->base.protocol = &subghz_protocol_bmw_cas4;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_bmw_cas4_free(void* context) {
    furi_check(context);
    free(context);
}

void subghz_protocol_decoder_bmw_cas4_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas4* instance = context;
    instance->decoder_state = BmwCas4DecoderStepReset;
    instance->bit_count = 0;
    instance->data_bit_count = 0;
    instance->cmd_byte = 0;
    memset(instance->data, 0, sizeof(instance->data));
}

// Pack the aligned bit run (after the 0x45 search) into data[] MSB-first and
// commit if it looks like a valid BMW CAS4 frame. Returns true if committed.
static bool bmw_cas4_try_commit(SubGhzProtocolDecoderBmwCas4* instance) {
    if(instance->bit_count < BMW_CAS4_MIN_DATA_BITS ||
       instance->bit_count > BMW_CAS4_MAX_DATA_BITS) {
        return false;
    }

    // Search a small window for the 0x45 preamble byte (frames may carry a
    // stray leading 0 before it).
    int off = -1;
    for(int o = 0; o <= 4; o++) {
        if((uint16_t)(o + 8) > instance->bit_count) break;
        uint8_t b = 0;
        for(int k = 0; k < 8; k++) {
            b = (uint8_t)((b << 1) | (instance->raw_bits[o + k] & 1));
        }
        if(b == BMW_CAS4_PREAMBLE_BYTE) {
            off = o;
            break;
        }
    }
    if(off < 0) return false;

    uint16_t aligned = (uint16_t)(instance->bit_count - off);
    if(aligned < BMW_CAS4_MIN_DATA_BITS) return false;

    // Pack MSB-first into data[].
    memset(instance->data, 0, sizeof(instance->data));
    uint16_t nbytes = aligned / 8; // whole bytes only
    if(nbytes > BMW_CAS4_MAX_BYTES) nbytes = BMW_CAS4_MAX_BYTES;
    for(uint16_t i = 0; i < nbytes; i++) {
        uint8_t b = 0;
        for(int k = 0; k < 8; k++) {
            b = (uint8_t)((b << 1) | (instance->raw_bits[off + i * 8 + k] & 1));
        }
        instance->data[i] = b;
    }
    instance->data_bit_count = (uint16_t)(nbytes * 8);

    if(nbytes < 2) return false;
    if(instance->data[0] != BMW_CAS4_PREAMBLE_BYTE) return false;
    instance->cmd_byte = instance->data[1];

    // [FALSE_POSITIVE_FIX] The command byte's high nibble is a fixed 0x1x family
    // on BMW CAS4 (lock 0x13 / unlock 0x12). Requiring both the 0x45 marker AND
    // this nibble makes a stray PWM burst from another 433.92 MHz car remote very
    // unlikely to be mis-accepted as BMW.
    if((instance->cmd_byte & 0xF0u) != BMW_CAS4_CMD_HI_NIBBLE) return false;

    // Fill the generic block: pack the first up-to-8 bytes into generic.data so
    // the hash/history has a stable key; data_count_bit carries the real length.
    uint64_t key = 0;
    uint16_t key_bytes = (nbytes < 8) ? nbytes : 8;
    for(uint16_t i = 0; i < key_bytes; i++) {
        key = (key << 8) | instance->data[i];
    }
    instance->generic.data = key;
    instance->generic.data_count_bit = instance->data_bit_count;
    instance->generic.btn = (uint8_t)(instance->cmd_byte & 0x0F);

    return true;
}

void subghz_protocol_decoder_bmw_cas4_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas4* instance = context;

    switch(instance->decoder_state) {
    case BmwCas4DecoderStepReset:
        // Start a fresh frame ONLY after a genuine BMW wakeup HIGH pulse
        // (~25ms). This distinctive burst is the primary anti-false-positive
        // gate — other 433.92 MHz OOK remotes don't precede data with it.
        if(level && duration >= BMW_CAS4_WAKEUP_MIN && duration <= BMW_CAS4_WAKEUP_MAX) {
            instance->decoder_state = BmwCas4DecoderStepData;
            instance->bit_count = 0;
        }
        break;

    case BmwCas4DecoderStepData:
        if(level) {
            // Expect a short data pulse (~TE). A very long HIGH means a new
            // wakeup or the end marker -> close the current frame.
            if(duration >= BMW_CAS4_LONG_MIN) {
                if(bmw_cas4_try_commit(instance)) {
                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                // Treat as the start of the next repeat.
                instance->bit_count = 0;
                // stay in Data
            } else if(duration < BMW_CAS4_PULSE_MIN || duration > BMW_CAS4_PULSE_MAX) {
                // malformed pulse -> reset
                instance->decoder_state = BmwCas4DecoderStepReset;
            }
            // a normal short pulse: the following LOW gap carries the bit value
        } else {
            // LOW gap -> a data bit (0 = short gap, 1 = long gap)
            if(DURATION_DIFF(duration, BMW_CAS4_GAP_SHORT) < BMW_CAS4_GAP_DELTA) {
                if(instance->bit_count < BMW_CAS4_MAX_DATA_BITS) {
                    instance->raw_bits[instance->bit_count++] = 0;
                }
            } else if(DURATION_DIFF(duration, BMW_CAS4_GAP_LONG) < BMW_CAS4_GAP_DELTA) {
                if(instance->bit_count < BMW_CAS4_MAX_DATA_BITS) {
                    instance->raw_bits[instance->bit_count++] = 1;
                }
            } else if(duration >= BMW_CAS4_LONG_MIN) {
                // long LOW (wakeup/end gap) -> frame boundary
                if(bmw_cas4_try_commit(instance)) {
                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                instance->bit_count = 0;
            } else {
                // unknown gap -> end of frame; try to commit then reset
                if(bmw_cas4_try_commit(instance)) {
                    if(instance->base.callback) {
                        instance->base.callback(&instance->base, instance->base.context);
                    }
                }
                instance->decoder_state = BmwCas4DecoderStepReset;
                instance->bit_count = 0;
            }
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_bmw_cas4_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas4* instance = context;
    SubGhzBlockDecoder dec = {
        .decode_data = instance->generic.data,
        .decode_count_bit = instance->generic.data_count_bit,
    };
    return subghz_protocol_blocks_get_hash_data(&dec, (dec.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_bmw_cas4_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas4* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        // Store the full variable-length payload as a hex blob so it survives
        // save/load regardless of length (>64 bits).
        uint32_t nbytes = instance->data_bit_count / 8;
        if(nbytes == 0 || nbytes > BMW_CAS4_MAX_BYTES) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        } else if(!flipper_format_write_hex(
                      flipper_format, "Data", instance->data, (uint16_t)nbytes)) {
            ret = SubGhzProtocolStatusErrorParserOthers;
        }
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_bmw_cas4_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas4* instance = context;

    SubGhzProtocolStatus ret =
        subghz_block_generic_deserialize(&instance->generic, flipper_format);
    if(ret != SubGhzProtocolStatusOk) return ret;

    instance->data_bit_count = instance->generic.data_count_bit;
    uint32_t nbytes = instance->data_bit_count / 8;
    if(nbytes == 0 || nbytes > BMW_CAS4_MAX_BYTES) {
        return SubGhzProtocolStatusErrorValueBitCount;
    }

    memset(instance->data, 0, sizeof(instance->data));
    // Prefer the full "Data" blob; fall back to reconstructing from generic.data.
    if(!flipper_format_read_hex(flipper_format, "Data", instance->data, (uint16_t)nbytes)) {
        uint16_t key_bytes = (nbytes < 8) ? (uint16_t)nbytes : 8;
        for(uint16_t i = 0; i < key_bytes; i++) {
            instance->data[i] =
                (uint8_t)(instance->generic.data >> (8 * (key_bytes - 1 - i)));
        }
    }
    if(nbytes >= 2) instance->cmd_byte = instance->data[1];

    return SubGhzProtocolStatusOk;
}

static const char* bmw_cas4_button_name(uint8_t cmd) {
    switch(cmd) {
    case BMW_CAS4_CMD_LOCK:
        return "Lock";
    case BMW_CAS4_CMD_UNLOCK:
        return "Unlock";
    default:
        return "Unknown";
    }
}

void subghz_protocol_decoder_bmw_cas4_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderBmwCas4* instance = context;

    uint16_t nbytes = instance->data_bit_count / 8;

    furi_string_cat_printf(
        output,
        "%s %ubit\r\n"
        "Btn:[%s] Cmd:%02X\r\n",
        instance->generic.protocol_name,
        instance->data_bit_count,
        bmw_cas4_button_name(instance->cmd_byte),
        instance->cmd_byte);

    // Print the raw payload bytes (up to a couple of lines).
    furi_string_cat_printf(output, "Data:");
    for(uint16_t i = 0; i < nbytes && i < BMW_CAS4_MAX_BYTES; i++) {
        if(i == 6) furi_string_cat_printf(output, "\r\n     ");
        furi_string_cat_printf(output, "%02X", instance->data[i]);
    }
    furi_string_cat_printf(output, "\r\n");
}
