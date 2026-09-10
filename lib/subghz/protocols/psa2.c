/**
 * PSA GROUP SubGHz Protocol
 * Reconstructed from Ghidra
 *
 * Function map (firmware addr -> name):
 *   FUN_08028cf8  -> psa_calculate_checksum
 *   FUN_08028d24  -> psa_copy_reverse
 *   FUN_08028d54  -> psa_second_stage_xor_decrypt
 *   FUN_08028d98  -> psa_direct_xor_decrypt
 *   FUN_08028e14  -> psa_tea_encrypt
 *   FUN_08028e60  -> psa_calculate_tea_crc
 *   FUN_08028e88  -> psa_unpack_tea_result_to_buffer
 *   FUN_08028f10  -> psa_extract_fields (mode36/mode23)
 *   FUN_08028f4c  -> psa_prepare_tea_data
 *   FUN_08028f94  -> psa_brute_force_decrypt_bf1
 *   FUN_080290c4  -> psa_extract_crc_bf2
 *   FUN_08029028  -> psa_build_encrypt_mode23
 *   FUN_08029098  -> psa_calculate_crc16_bf2
 *   FUN_080290f8  -> psa_brute_force_decrypt_bf2
 *   FUN_080291c0  -> psa_decrypt_full
 *   FUN_08029678  -> subghz_protocol_decoder_psa2_deserialize
 *   FUN_080297f8  -> subghz_protocol_decoder_psa2_get_string
 *   FUN_080299a0  -> subghz_protocol_encoder_psa2_deserialize
 *   FUN_08029954  -> psa_build_encrypt_mode36
 */

#include "psa2.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"
#include "../blocks/custom_btn_i.h"
#include <lib/toolbox/manchester_decoder.h>

#define TAG "SubGhzProtocolPSA"

/* =========================================================
 * CONSTANTS — extracted from firmware binary
 * ========================================================= */

static const SubGhzBlockConst subghz_protocol_psa_const = {
    .te_short = 250,   /* 0xFA */
    .te_long  = 500,   /* confirmed from encoder: 0xfa timing */
    .te_delta = 100,
    .min_count_bit_for_found = 128,
};

/* Timing variants (mode 2 uses 0x7D = 125 µs half-rate) */
#define PSA_TE_SHORT_STD   250
#define PSA_TE_SHORT_HALF  125   /* 0x7D — mode 2 / State4 */
#define PSA_TE_LONG_STD    500
#define PSA_TE_LONG_HALF   250   /* 0xFA in half-rate = 250 */
#define PSA_TE_END_1000    1000
#define PSA_TE_END_500     500

/* TEA */
#define TEA_DELTA  0x9E3779B9U
#define TEA_ROUNDS 32

/* BF1 range: 0x23000000 – 0x24000000 (confirmed from FUN_08028f94) */
#define PSA_BF1_START  0x23000000U
#define PSA_BF1_END    0x24000000U

/* BF1 constants used inside the loop (from FUN_08028f94 / FUN_080291c0) */
/*   wk0 seed:  counter << 8 | 0x0E
 *   wk1 seed:  DAT_08029024  (= PSA_BF1_CONST_U5 in your code)
 *   wk2 seed:  DAT_08029020  (= PSA_BF1_CONST_U4)
 *   wk3 seed:  counter
 * Both pairs are encrypted with PSA_BF1_KEY_SCHEDULE before use.
 * the constants (PSA_BF1_CONST_U4/U5 and key schedule) are identical.
 */
#define PSA_BF1_CONST_U4  0x0E0F5C41U
#define PSA_BF1_CONST_U5  0x0F5C4123U

static const uint32_t PSA_BF1_KEY_SCHEDULE[4] = {
    0x4A434915U,
    0xD6743C2BU,
    0x1F29D308U,
    0xE6B79A64U,
};

/* BF2 range: 0xF3000000 – 0xF4000000 (confirmed from FUN_080290f8) */
#define PSA_BF2_START  0xF3000000U
#define PSA_BF2_END    0xF4000000U

/* Validation nibble for mode23 XOR path */
#define PSA_VALID_NIBBLE  0xA   /* (validation_field & 0xF) == 0xA  */

/* Button codes */
#define PSA_BTN_LOCK    0x0
#define PSA_BTN_UNLOCK  0x1
#define PSA_BTN_TRUNK   0x2

/* Bit counts */
#define PSA_KEY1_BITS  64   /* 0x40 */
#define PSA_KEY2_BITS  80   /* 0x50 */
#define PSA_MAX_BITS   121  /* 0x79 */

/* Decoder modes — stored as ASCII chars in firmware ('0','#','6') */
#define PSA_MODE_UNKNOWN  0x00
#define PSA_MODE_23       0x23   /* '#' */
#define PSA_MODE_36       0x36   /* '6' */

/* =========================================================
 * STRUCTS
 * ========================================================= */

typedef enum {
    PSADecoderState0 = 0,
    PSADecoderState1,
    PSADecoderState2,
    PSADecoderState3,
    PSADecoderState4,
} PSADecoderState;

struct SubGhzProtocolDecoderPSA {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint32_t state;
    uint32_t prev_duration;

    uint32_t decode_data_low;
    uint32_t decode_data_high;
    uint8_t  decode_count_bit;

    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t key2_high;

    uint32_t status_flag;

    /* mode_serialize: 0=unknown, 0x23=mode23, 0x36=mode36 */
    uint8_t  mode_serialize;
    /* decrypted: 0=fail, 0x50=success */
    uint16_t decrypted;

    uint8_t  decrypted_button;
    uint32_t decrypted_serial;
    uint32_t decrypted_counter;
    uint16_t decrypted_crc;
    uint32_t decrypted_seed;
    uint8_t  decrypted_type;

    uint16_t       pattern_counter;
    ManchesterState manchester_state;

    uint32_t last_key1_low;
    uint32_t last_key1_high;

    /* Firmware extra field at +0x33: brute-force-attempted flag */
    uint8_t bf_attempted;
};

struct SubGhzProtocolEncoderPSA {
    SubGhzProtocolEncoderBase  base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric         generic;

    uint32_t key1_low;
    uint32_t key1_high;
    uint16_t validation_field;
    uint32_t key2_low;
    uint32_t counter;
    uint8_t  button;
    uint8_t  type;    /* PSA_MODE_23 or PSA_MODE_36 */
    uint8_t  seed;
    uint8_t  mode;
    uint32_t serial;
    uint16_t crc;
    bool     is_running;
};

/* =========================================================
 * FORWARD DECLARATIONS
 * ========================================================= */

static bool psa_direct_xor_decrypt(SubGhzProtocolDecoderPSA* instance);

/* =========================================================
 * PROTOCOL DESCRIPTORS
 * ========================================================= */

const SubGhzProtocolDecoder subghz_protocol_psa2_decoder = {
    .alloc        = subghz_protocol_decoder_psa2_alloc,
    .free         = subghz_protocol_decoder_psa2_free,
    .feed         = subghz_protocol_decoder_psa2_feed,
    .reset        = subghz_protocol_decoder_psa2_reset,
    .get_hash_data = subghz_protocol_decoder_psa2_get_hash_data,
    .serialize    = subghz_protocol_decoder_psa2_serialize,
    .deserialize  = subghz_protocol_decoder_psa2_deserialize,
    .get_string   = subghz_protocol_decoder_psa2_get_string,
};

const SubGhzProtocolEncoder subghz_protocol_psa2_encoder = {
    .alloc       = subghz_protocol_encoder_psa2_alloc,
    .free        = subghz_protocol_encoder_psa2_free,
    .deserialize = subghz_protocol_encoder_psa2_deserialize,
    .stop        = subghz_protocol_encoder_psa2_stop,
    .yield       = subghz_protocol_encoder_psa2_yield,
};

const SubGhzProtocol subghz_protocol_psa2 = {
    .name    = SUBGHZ_PROTOCOL_PSA2_NAME,
    .type    = SubGhzProtocolTypeDynamic,
    .flag    = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
               SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Load |
               SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_psa2_decoder,
    .encoder = &subghz_protocol_psa2_encoder,
};

/* =========================================================
 * BUTTON HELPERS
 * ========================================================= */

static uint8_t psa_get_btn_code(void) {
    uint8_t custom_btn   = subghz_custom_btn_get();
    uint8_t original_raw = subghz_custom_btn_get_original();
    uint8_t original_btn = (original_raw == 0xFF) ? PSA_BTN_LOCK : original_raw;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_OK)    return original_btn;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_UP)    return PSA_BTN_LOCK;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_DOWN)  return PSA_BTN_UNLOCK;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_LEFT)  return PSA_BTN_TRUNK;
    if(custom_btn == SUBGHZ_CUSTOM_BTN_RIGHT) return PSA_BTN_TRUNK;
    return original_btn;
}

/* =========================================================
 * CRYPTO PRIMITIVES
 * =========================================================
 *
 * KEY FINDING — FUN_08028e14
 * The firmware TEA encrypt/decrypt operates on module-level
 * static buffers, NOT on passed pointers. Here we reconstruct
 * the canonical interface that matches the actual algorithm.
 */

static void psa_tea_encrypt(uint32_t* v0, uint32_t* v1, const uint32_t* key) {
    uint32_t a = *v0, b = *v1, sum = 0;
    for(int i = 0; i < TEA_ROUNDS; i++) {
        uint32_t t = key[sum & 3] + sum;
        sum += TEA_DELTA;
        a += t ^ ((b >> 5 ^ b << 4) + b);
        t  = key[(sum >> 11) & 3] + sum;
        b += t ^ ((a >> 5 ^ a << 4) + a);
    }
    *v0 = a; *v1 = b;
}

/* FUN_08028e60 — simple byte-sum CRC over 7 bytes of TEA output */
static uint8_t psa_calculate_tea_crc(uint32_t v0, uint32_t v1) {
    uint32_t crc = ((v0 >> 24) & 0xFF) + ((v0 >> 16) & 0xFF) +
                   ((v0 >>  8) & 0xFF) + ( v0        & 0xFF);
    crc += ((v1 >> 24) & 0xFF) + ((v1 >> 16) & 0xFF) + ((v1 >> 8) & 0xFF);
    return (uint8_t)(crc & 0xFF);
}

/* =========================================================
 * BUFFER HELPERS
 * =========================================================
 *
 * The firmware uses a single flat byte-array (buffer[48]) as
 * a scratch-pad.  Byte layout (confirmed from decompilation):
 *
 *  [0..1]   = buf0/buf1  (XOR of key1 bytes, used as preamble)
 *  [2..9]   = key1 bytes in big-endian order
 *              [2]=key1[56:48], [3]=key1[48:40], [4]=key1[40:32],
 *              [5]=key1[32:24], [6]=key1[24:16], [7]=key1[16:8],
 *              [8]=key1[8:0] high nibble / button,
 *              [9]=key2_low low byte
 *  [11]     = checksum
 *  [13]     = buffer[9] ^ buffer[8]
 *
 * psa_setup_byte_buffer — fills [0..9] from key1_low/high and key2_low.
 * This matches FUN_080291c0's first loop exactly.
 */

static void psa_setup_byte_buffer(uint8_t* buf,
                                   uint32_t key1_low, uint32_t key1_high,
                                   uint32_t key2_low) {
    /* bytes 0..7: key1 big-endian, reversed into buf[7..0] */
    for(int i = 0; i < 8; i++) {
        int shift = i * 8;
        uint8_t b;
        if(shift < 32)
            b = (uint8_t)(key1_low  >> shift);
        else
            b = (uint8_t)(key1_high >> (shift - 32));
        buf[7 - i] = b;
    }
    buf[9] = (uint8_t)(key2_low & 0xFF);
    buf[8] = (uint8_t)((key2_low >> 8) & 0xFF);
}

/* FUN_08028cf8 — nibble checksum over buf[2..7] → buf[11] */
static void psa_calculate_checksum(uint8_t* buf) {
    uint32_t sum = 0;
    for(int i = 2; i < 8; i++)
        sum += (buf[i] & 0xF) + ((buf[i] >> 4) & 0xF);
    buf[11] = (uint8_t)((sum * 0x10) & 0xFF);
}

/* FUN_08028d24 — reverse copy for XOR stage */
static void psa_copy_reverse(uint8_t* temp, const uint8_t* src) {
    temp[0] = src[5]; temp[1] = src[4];
    temp[2] = src[3]; temp[3] = src[2];
    temp[4] = src[9]; temp[5] = src[8];
    temp[6] = src[7]; temp[7] = src[6];
}

/* FUN_08028d54 — XOR decrypt second stage */
static void psa_second_stage_xor_decrypt(uint8_t* buf) {
    uint8_t t[8];
    psa_copy_reverse(t, buf);
    buf[2] = t[0] ^ t[6];
    buf[3] = t[2] ^ t[0];
    buf[4] = t[6] ^ t[3];
    buf[5] = t[7] ^ t[1];
    buf[6] = t[3] ^ t[1];
    buf[7] = t[6] ^ t[4] ^ t[5];
}

/* Inverse of above — used by encoder */
static void psa_second_stage_xor_encrypt(uint8_t* buf) {
    uint8_t E6 = buf[8], E7 = buf[9];
    uint8_t P0=buf[2], P1=buf[3], P2=buf[4], P3=buf[5], P4=buf[6], P5=buf[7];
    uint8_t E5 = P5 ^ E7 ^ E6;
    uint8_t E0 = P2 ^ E5;
    uint8_t E2 = P4 ^ E0;
    uint8_t E4 = P3 ^ E2;
    uint8_t E3 = P0 ^ E5;
    uint8_t E1 = P1 ^ E3;
    buf[2]=E0; buf[3]=E1; buf[4]=E2; buf[5]=E3; buf[6]=E4; buf[7]=E5;
}

/* FUN_08028e88 — unpack TEA words back into buffer[2..9] */
static void psa_unpack_tea_result(uint8_t* buf, uint32_t v0, uint32_t v1) {
    buf[2] = (v0 >> 24) & 0xFF;
    buf[3] = (v0 >> 16) & 0xFF;
    buf[4] = (v0 >>  8) & 0xFF;
    buf[5] =  v0        & 0xFF;
    buf[6] = (v1 >> 24) & 0xFF;
    buf[7] = (v1 >> 16) & 0xFF;
    buf[8] = (v1 >>  8) & 0xFF;
    buf[9] =  v1        & 0xFF;
}

/* =========================================================
 * FIELD EXTRACTION
 * =========================================================
 *
 * Two variants depending on decrypted type.
 * FUN_08028f10 and FUN_08028e88 together implement these.
 *
 * CRITICAL DIFFERENCE vs your psa.c:
 *
 * Mode 23 (FUN_08028f10 path A, after psa_direct_xor_decrypt):
 *   decrypted_button  = buf[8]  & 0xF            (low nibble)
 *   decrypted_serial  = buf[2]<<16 | buf[3]<<8 | buf[4]   (24-bit)
 *   decrypted_counter = buf[6]  | buf[5]<<8      (16-bit, little-endian order)
 *   decrypted_crc     = buf[7]                   (8-bit)
 *
 * Mode 36 (FUN_08028f10 path B, after TEA decrypt):
 *   decrypted_button  = (buf[5] >> 4) & 0xF
 *   decrypted_serial  = buf[2]<<16 | buf[3]<<8 | buf[4]
 *   decrypted_counter = buf[7]<<8 | buf[6]<<16 | buf[8] | (buf[5]&0xF)<<24  (32-bit)
 *   decrypted_crc     = buf[9] (8-bit for BF1) or (buf[8+1]<<8|buf[8]) for BF2
 *
 * IMPORTANT — the firmware stores mode as ASCII:
 *   0x23 = '#'  (mode 23)
 *   0x36 = '6'  (mode 36)
 * This is how mode_serialize is compared in FUN_080297f8 / FUN_080291c0.
 */

static void psa_extract_fields_mode23(uint8_t* buf, SubGhzProtocolDecoderPSA* inst) {
    inst->decrypted_button  = buf[8] & 0xF;
    inst->decrypted_serial  = ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 8) | buf[4];
    inst->decrypted_counter = (uint32_t)buf[6] | ((uint32_t)buf[5] << 8);
    inst->decrypted_crc     = buf[7];
    inst->decrypted_type    = PSA_MODE_23;
    inst->decrypted_seed    = inst->decrypted_serial;
}

/* =========================================================
 * DECRYPTION PATHS
 * =========================================================
 *
 * FUN_08028d98 — mode23 XOR path
 *
 * CRITICAL DIFFERENCE found in firmware vs your psa.c:
 *
 * The firmware validates with:
 *   (checksum ^ key2_high_byte) & 0xF0 == 0
 * then sets:
 *   buf[13] = buf[9] ^ buf[8]
 * and calls psa_second_stage_xor_decrypt.
 *
 * Your code does the same but the validation nibble check is:
 *   validation_result == 0
 * which is identical — no bug here.
 *
 * The REAL difference: in firmware, buf[8] is updated BEFORE
 * the second_stage_xor call:
 *   buf[8] = (buf[8] & 0x0F) | (checksum & 0xF0)
 * This step was MISSING in your psa.c and is the likely
 * source of button/type decode errors.
 */

static bool psa_direct_xor_decrypt(SubGhzProtocolDecoderPSA* inst) {
    uint8_t buf[48] = {0};
    psa_setup_byte_buffer(buf, inst->key1_low, inst->key1_high, inst->key2_low);
    psa_calculate_checksum(buf);

    uint8_t checksum  = buf[11];
    uint8_t key2_high = buf[8];
    uint8_t validation = (checksum ^ key2_high) & 0xF0;

    if(validation == 0) {
        /* FIRMWARE: update buf[8] high nibble before XOR stage */
        buf[8] = (buf[8] & 0x0F) | (checksum & 0xF0);
        buf[13] = buf[9] ^ buf[8];
        psa_second_stage_xor_decrypt(buf);
        psa_extract_fields_mode23(buf, inst);
        return true;
    }
    return false;
}

/* =========================================================
 * MAIN DECRYPT ROUTER — FUN_080291c0
 *
 * CRITICAL DIFFERENCE: firmware checks mode_serialize as ASCII char,
 * not as integer 1/2. Modes stored as 0x23/'#' and 0x36/'6'.
 *
 * Also note the firmware's FAST path:
 *   if mode_serialize == '#' (0x23):  try XOR only
 *   if mode_serialize == '6' (0x36):  try BF only (bf1 then bf2)
 *   if mode_serialize == 0:           try XOR, then BF
 *
 * The BF-attempted flag (+0x33 in struct) prevents re-running BF
 * on a packet that already failed brute force in this session.
 * ========================================================= */

/* Fast path (no BF) — used in feed callback */
static void psa_decrypt_fast(SubGhzProtocolDecoderPSA* inst) {
    if(psa_direct_xor_decrypt(inst)) {
        inst->mode_serialize = PSA_MODE_23;
        inst->decrypted = 0x50;
    } else {
        inst->decrypted      = 0x00;
        inst->mode_serialize = PSA_MODE_36;
    }
}

/* =========================================================
 * DECODER ALLOC / FREE / RESET
 * ========================================================= */

void* subghz_protocol_decoder_psa2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderPSA* inst = malloc(sizeof(SubGhzProtocolDecoderPSA));
    if(inst) {
        memset(inst, 0, sizeof(SubGhzProtocolDecoderPSA));
        inst->base.protocol = &subghz_protocol_psa2;
        inst->generic.protocol_name = inst->base.protocol->name;
        inst->manchester_state = ManchesterStateMid1;
    }
    return inst;
}

void subghz_protocol_decoder_psa2_free(void* context) {
    furi_assert(context);
    free(context);
}

void subghz_protocol_decoder_psa2_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;
    inst->state            = PSADecoderState0;
    inst->status_flag      = 0;
    inst->mode_serialize   = 0;
    inst->key1_low         = 0; inst->key1_high = 0;
    inst->key2_low         = 0; inst->key2_high = 0;
    inst->validation_field = 0;
    inst->decode_data_low  = 0; inst->decode_data_high = 0;
    inst->decode_count_bit = 0;
    inst->pattern_counter  = 0;
    inst->manchester_state = ManchesterStateMid1;
    inst->prev_duration    = 0;
    inst->decrypted        = 0;
    inst->decrypted_button = 0; inst->decrypted_serial  = 0;
    inst->decrypted_counter= 0; inst->decrypted_crc     = 0;
    inst->decrypted_seed   = 0; inst->decrypted_type    = 0;
    inst->bf_attempted     = 0;
}

/* =========================================================
 * DECODER FEED — State machine
 *
 * Two physical variants exist in the firmware:
 *   Mode 1 / State2+State1: te=500/250 µs  (standard)
 *   Mode 2 / State4+State3: te=250/125 µs  (half-rate)
 *
 * The encoder confirms this: mode==1 uses 0xFA (250µs half) and
 * mode==2 uses 0x7D (125µs quarter).  The bit encoding loop
 * uses level_duration_make with those exact values.
 *
 * Validation gate (confirmed from FUN_080291c0 inline decrypt):
 *   Fire callback only if:
 *     decrypted == 0x50  OR  (validation_field & 0xF) == 0xA
 * ========================================================= */

#define PSA_FIRE_CALLBACK_IF_NEW(inst)                                      \
    do {                                                                     \
        bool _dup = ((inst)->key1_low  == (inst)->last_key1_low  &&         \
                     (inst)->key1_high == (inst)->last_key1_high);          \
        if(!_dup) {                                                          \
            (inst)->last_key1_low  = (inst)->key1_low;                      \
            (inst)->last_key1_high = (inst)->key1_high;                     \
            if((inst)->base.callback)                                        \
                (inst)->base.callback(&(inst)->base, (inst)->base.context); \
        }                                                                    \
    } while(0)

static inline bool psa_duration_near(uint32_t dur, uint32_t target, uint32_t tol) {
    return (dur >= target - tol) && (dur <= target + tol);
}

void subghz_protocol_decoder_psa2_feed(void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;

    /* Local copies for speed */
    uint32_t te_s = subghz_protocol_psa_const.te_short; /* 250 */
    uint32_t te_l = subghz_protocol_psa_const.te_long;  /* 500 */
    uint32_t tol  = subghz_protocol_psa_const.te_delta; /* 100 */

    /* Half-rate timings (State 3/4) */
    uint32_t te_hs = PSA_TE_SHORT_HALF;  /* 125 */
    uint32_t te_hl = PSA_TE_LONG_HALF;   /* 250 */
    uint32_t tol_h = 50;

    switch(inst->state) {
    /* ---- STATE 0: wait for preamble start ---- */
    case PSADecoderState0:
        if(!level) return;
        inst->decode_data_low  = 0;
        inst->decode_data_high = 0;
        inst->pattern_counter  = 0;
        inst->decode_count_bit = 0;
        inst->mode_serialize   = 0;
        inst->prev_duration    = duration;
        manchester_advance(inst->manchester_state, ManchesterEventReset,
                           &inst->manchester_state, NULL);

        if(psa_duration_near(duration, te_s, tol)) {
            inst->state = PSADecoderState1;
        } else if(psa_duration_near(duration, te_hs, tol_h)) {
            inst->state = PSADecoderState3;
        }
        /* else: ignore */
        break;

    /* ---- STATE 1: count standard-rate preamble pulses ---- */
    case PSADecoderState1:
        if(level) return;
        if(psa_duration_near(duration, te_s, tol)) {
            if(psa_duration_near(inst->prev_duration, te_s, tol))
                inst->pattern_counter++;
            inst->prev_duration = duration;
            return;
        }
        if(psa_duration_near(duration, te_l, tol)) {
            if(inst->pattern_counter > 0x46) {  /* PSA_PATTERN_THRESHOLD_1 */
                inst->decode_data_low  = 0;
                inst->decode_data_high = 0;
                inst->decode_count_bit = 0;
                manchester_advance(inst->manchester_state, ManchesterEventReset,
                                   &inst->manchester_state, NULL);
                inst->state = PSADecoderState2;
            }
            inst->pattern_counter = 0;
            inst->prev_duration   = duration;
            return;
        }
        inst->state           = PSADecoderState0;
        inst->pattern_counter = 0;
        break;

    /* ---- STATE 2: receive key1 + key2 at standard rate ---- */
    case PSADecoderState2: {
        if(inst->decode_count_bit >= PSA_MAX_BITS) {
            inst->state = PSADecoderState0;
            break;
        }

        /* End-of-packet detection at KEY2_BITS */
        if(level && inst->decode_count_bit == PSA_KEY2_BITS) {
            if(psa_duration_near(duration, PSA_TE_END_1000, 199)) {
                goto psa_s2_packet_complete;
            }
        }

        uint8_t mc_event;
        bool process = false;
        if(psa_duration_near(duration, te_s, tol)) {
            mc_event = ((level ^ 1) & 0x7F) << 1;
            process  = true;
        } else if(psa_duration_near(duration, te_l, tol)) {
            mc_event = level ? 4 : 6;
            process  = true;
        } else {
            if(!level && psa_duration_near(duration, PSA_TE_END_1000, 199)) {
                if(inst->decode_count_bit == PSA_KEY2_BITS &&
                   (inst->validation_field & 0xF) == PSA_VALID_NIBBLE) {
                    goto psa_s2_packet_complete_noend;
                }
            }
            return;
        }

        if(process && inst->decode_count_bit < PSA_KEY2_BITS) {
            bool bit = false;
            if(manchester_advance(inst->manchester_state, (ManchesterEvent)mc_event,
                                  &inst->manchester_state, &bit)) {
                uint32_t carry = (inst->decode_data_low >> 31) & 1;
                inst->decode_data_low  = (inst->decode_data_low << 1) | (bit ? 1 : 0);
                inst->decode_data_high = (inst->decode_data_high << 1) | carry;
                inst->decode_count_bit++;
                if(inst->decode_count_bit == PSA_KEY1_BITS) {
                    inst->key1_low         = inst->decode_data_low;
                    inst->key1_high        = inst->decode_data_high;
                    inst->decode_data_low  = 0;
                    inst->decode_data_high = 0;
                }
            }
        }
        if(!level) return;
        break;

psa_s2_packet_complete:
psa_s2_packet_complete_noend:
        inst->validation_field = (uint16_t)(inst->decode_data_low & 0xFFFF);
        inst->key2_low         = inst->decode_data_low;
        inst->key2_high        = inst->decode_data_high;
        inst->mode_serialize   = 1;
        inst->status_flag      = 0x80;

        psa_decrypt_fast(inst);

        if(inst->decrypted != 0x50 && (inst->validation_field & 0xF) != PSA_VALID_NIBBLE) {
            inst->decode_data_low  = 0;
            inst->decode_data_high = 0;
            inst->decode_count_bit = 0;
            inst->state            = PSADecoderState0;
            return;
        }

        inst->generic.data           = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
        inst->generic.data_count_bit = 64;
        inst->decoder.decode_data    = inst->generic.data;
        inst->decoder.decode_count_bit = 64;

        PSA_FIRE_CALLBACK_IF_NEW(inst);

        inst->decode_data_low  = 0;
        inst->decode_data_high = 0;
        inst->decode_count_bit = 0;
        inst->state            = PSADecoderState0;
        return;
    }

    /* ---- STATE 3: count half-rate preamble pulses ---- */
    case PSADecoderState3:
        if(level) return;
        if(psa_duration_near(duration, te_hs, tol_h)) {
            if(psa_duration_near(inst->prev_duration, te_hs, tol_h))
                inst->pattern_counter++;
            else
                inst->pattern_counter = 0;
            inst->prev_duration = duration;
            return;
        }
        if(duration >= te_hl && duration < 0x12C) {
            if(inst->pattern_counter > 0x45) {  /* PSA_PATTERN_THRESHOLD_2 */
                inst->decode_data_low  = 0;
                inst->decode_data_high = 0;
                inst->decode_count_bit = 0;
                manchester_advance(inst->manchester_state, ManchesterEventReset,
                                   &inst->manchester_state, NULL);
                inst->state = PSADecoderState4;
            }
            inst->pattern_counter = 0;
            inst->prev_duration   = duration;
            return;
        }
        inst->state = PSADecoderState0;
        break;

    /* ---- STATE 4: receive key1 + key2 at half rate ---- */
    case PSADecoderState4: {
        if(inst->decode_count_bit >= PSA_MAX_BITS) {
            inst->state = PSADecoderState0;
            break;
        }

        if(!level) {
            uint8_t mc_event;
            if(psa_duration_near(duration, te_hs, tol_h)) {
                mc_event = ((level ^ 1) & 0x7F) << 1;
            } else if(duration >= te_hl && duration < 0x12C) {
                mc_event = level ? 4 : 6;
            } else {
                return;
            }

            bool bit = false;
            if(manchester_advance(inst->manchester_state, (ManchesterEvent)mc_event,
                                  &inst->manchester_state, &bit)) {
                uint32_t carry = (inst->decode_data_low >> 31) & 1;
                inst->decode_data_low  = (inst->decode_data_low << 1) | (bit ? 1 : 0);
                inst->decode_data_high = (inst->decode_data_high << 1) | carry;
                inst->decode_count_bit++;
                if(inst->decode_count_bit == PSA_KEY1_BITS) {
                    inst->key1_low         = inst->decode_data_low;
                    inst->key1_high        = inst->decode_data_high;
                    inst->decode_data_low  = 0;
                    inst->decode_data_high = 0;
                }
            }
        } else {
            /* Rising edge: check for end-of-packet */
            if(psa_duration_near(duration, PSA_TE_END_500, 99)) {
                if(inst->decode_count_bit != PSA_KEY2_BITS) return;

                inst->validation_field = (uint16_t)(inst->decode_data_low & 0xFFFF);
                inst->key2_low         = inst->decode_data_low;
                inst->key2_high        = inst->decode_data_high;
                inst->mode_serialize   = 2;
                inst->status_flag      = 0x80;

                psa_decrypt_fast(inst);

                if(inst->decrypted != 0x50 && (inst->validation_field & 0xF) != PSA_VALID_NIBBLE) {
                    inst->decode_data_low  = 0;
                    inst->decode_data_high = 0;
                    inst->decode_count_bit = 0;
                    inst->state            = PSADecoderState0;
                    return;
                }

                inst->generic.data             = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
                inst->generic.data_count_bit   = 64;
                inst->decoder.decode_data      = inst->generic.data;
                inst->decoder.decode_count_bit = 64;

                PSA_FIRE_CALLBACK_IF_NEW(inst);

                inst->decode_data_low  = 0;
                inst->decode_data_high = 0;
                inst->decode_count_bit = 0;
                inst->state            = PSADecoderState0;
                return;
            }
        }
        break;
    }
    } /* switch */

    inst->prev_duration = duration;
}

/* =========================================================
 * HASH / SERIALIZE / DESERIALIZE / GET_STRING
 * ========================================================= */

uint8_t subghz_protocol_decoder_psa2_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;
    return subghz_protocol_blocks_get_hash_data(
        &inst->decoder, (inst->decoder.decode_count_bit / 8) + 1);
}

SubGhzProtocolStatus subghz_protocol_decoder_psa2_serialize(
    void* context, FlipperFormat* ff, SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;

    if(inst->decrypted != 0x50 && inst->status_flag == 0x80) {
        psa_decrypt_fast(inst);
        if(inst->decrypted == 0x50) {
            inst->generic.cnt    = inst->decrypted_counter;
            inst->generic.serial = inst->decrypted_serial;
            inst->generic.btn    = inst->decrypted_button;
        }
    }

    inst->generic.data_count_bit = 128;
    SubGhzProtocolStatus ret = subghz_block_generic_serialize(&inst->generic, ff, preset);
    if(ret != SubGhzProtocolStatusOk) return ret;

    do {
        char key2_str[32];
        snprintf(key2_str, sizeof(key2_str),
                 "%02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned int)((inst->key2_high >> 24) & 0xFF), (unsigned int)((inst->key2_high >> 16) & 0xFF),
                 (unsigned int)((inst->key2_high >>  8) & 0xFF), (unsigned int)(inst->key2_high        & 0xFF),
                 (unsigned int)((inst->key2_low  >> 24) & 0xFF), (unsigned int)((inst->key2_low  >> 16) & 0xFF),
                 (unsigned int)((inst->key2_low  >>  8) & 0xFF), (unsigned int)(inst->key2_low         & 0xFF));
        if(!flipper_format_write_string_cstr(ff, "Key_2", key2_str)) { ret = SubGhzProtocolStatusError; break; }

        if(inst->decrypted == 0x50 && inst->decrypted_type != 0) {
            char s[32];
            snprintf(s, sizeof(s), "%02X %02X %02X",
                     (unsigned int)((inst->decrypted_serial >> 16) & 0xFF),
                     (unsigned int)((inst->decrypted_serial >>  8) & 0xFF),
                     (unsigned int)(inst->decrypted_serial        & 0xFF));
            if(!flipper_format_write_string_cstr(ff, "Serial", s)) { ret = SubGhzProtocolStatusError; break; }

            if(inst->decrypted_type == PSA_MODE_23)
                snprintf(s, sizeof(s), "%02X %02X",
                         (unsigned int)((inst->decrypted_counter >> 8) & 0xFF),
                         (unsigned int)(inst->decrypted_counter       & 0xFF));
            else
                snprintf(s, sizeof(s), "%02X %02X %02X %02X",
                         (unsigned int)((inst->decrypted_counter >> 24) & 0x0F),
                         (unsigned int)((inst->decrypted_counter >> 16) & 0xFF),
                         (unsigned int)((inst->decrypted_counter >>  8) & 0xFF),
                         (unsigned int)(inst->decrypted_counter        & 0xFF));
            if(!flipper_format_write_string_cstr(ff, "Cnt", s)) { ret = SubGhzProtocolStatusError; break; }

            snprintf(s, sizeof(s), "%02X", inst->decrypted_button);
            if(!flipper_format_write_string_cstr(ff, "Btn", s)) { ret = SubGhzProtocolStatusError; break; }

            snprintf(s, sizeof(s), "%02X", inst->decrypted_type);
            if(!flipper_format_write_string_cstr(ff, "Type", s)) { ret = SubGhzProtocolStatusError; break; }

            if(inst->decrypted_type == PSA_MODE_23)
                snprintf(s, sizeof(s), "%02X", inst->decrypted_crc & 0xFF);
            else
                snprintf(s, sizeof(s), "%02X %02X",
                         (inst->decrypted_crc >> 8) & 0xFF,
                          inst->decrypted_crc       & 0xFF);
            if(!flipper_format_write_string_cstr(ff, "CRC", s)) { ret = SubGhzProtocolStatusError; break; }

            snprintf(s, sizeof(s), "%02X %02X %02X",
                     (unsigned int)((inst->decrypted_seed >> 16) & 0xFF),
                     (unsigned int)((inst->decrypted_seed >>  8) & 0xFF),
                     (unsigned int)(inst->decrypted_seed        & 0xFF));
            if(!flipper_format_write_string_cstr(ff, "Seed", s)) { ret = SubGhzProtocolStatusError; break; }
        }
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

/* Helper: parse hex string (with spaces) into uint32_t */
static uint32_t psa_parse_hex_str(const char* s) {
    uint32_t v = 0;
    for(size_t i = 0; i < strlen(s); i++) {
        char c = s[i];
        if(c == ' ') continue;
        uint8_t n = (c>='0'&&c<='9') ? c-'0' :
                    (c>='A'&&c<='F') ? c-'A'+10 :
                    (c>='a'&&c<='f') ? c-'a'+10 : 0;
        v = (v << 4) | n;
    }
    return v;
}

SubGhzProtocolStatus subghz_protocol_decoder_psa2_deserialize(
    void* context, FlipperFormat* ff) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    FuriString* tmp = furi_string_alloc();

    do {
        ret = subghz_block_generic_deserialize(&inst->generic, ff);
        if(ret != SubGhzProtocolStatusOk) break;

        uint64_t k1 = inst->generic.data;
        inst->key1_low  = (uint32_t)(k1 & 0xFFFFFFFF);
        inst->key1_high = (uint32_t)(k1 >> 32);

        if(!flipper_format_read_string(ff, "Key_2", tmp)) break;
        uint64_t k2 = 0;
        const char* ks = furi_string_get_cstr(tmp);
        for(size_t i = 0; i < strlen(ks); i++) {
            char c = ks[i]; if(c==' ') continue;
            uint8_t n=(c>='0'&&c<='9')?c-'0':(c>='A'&&c<='F')?c-'A'+10:(c>='a'&&c<='f')?c-'a'+10:0;
            k2 = (k2 << 4) | n;
        }
        inst->key2_low  = (uint32_t)(k2 & 0xFFFFFFFF);
        inst->key2_high = (uint32_t)(k2 >> 32);

        inst->generic.data           = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
        inst->generic.data_count_bit = 128;
        inst->status_flag            = 0x80;

        bool has_data = true;
        uint32_t serial=0, counter=0, seed=0;
        uint16_t crc=0;
        uint8_t  button=0, type=0;

        if(flipper_format_read_string(ff, "Serial", tmp))
            serial = psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "Cnt", tmp))
            counter = psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "Btn", tmp))
            button = (uint8_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "Type", tmp))
            type = (uint8_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "CRC", tmp))
            crc = (uint16_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        if(flipper_format_read_string(ff, "Seed", tmp))
            seed = psa_parse_hex_str(furi_string_get_cstr(tmp));

        if(has_data && type != 0) {
            inst->decrypted_serial  = serial;
            inst->decrypted_counter = counter;
            inst->decrypted_button  = button;
            inst->decrypted_type    = type;
            inst->decrypted_crc     = crc;
            inst->decrypted_seed    = seed;
            inst->decrypted         = 0x50;
            inst->mode_serialize    = type;
            inst->generic.cnt       = counter;
            inst->generic.serial    = serial;
            inst->generic.btn       = button;
        } else {
            psa_decrypt_fast(inst);
            inst->generic.cnt    = inst->decrypted_counter;
            inst->generic.serial = inst->decrypted_serial;
            inst->generic.btn    = inst->decrypted_button;
        }
        ret = SubGhzProtocolStatusOk;
    } while(false);

    furi_string_free(tmp);
    return ret;
}

/* FUN_080297f8 — get_string
 *
 * DIFFERENCE from your code:
 * Firmware selects format string based on:
 *   mode_serialize == '#' (0x23) → standard mode23 display
 *   mode_serialize == '6' (0x36) → checks sub-mode from decrypted_seed:
 *       seed >> 24 == 0x23 → BF1 found key display
 *       seed >> 24 == 0xF3 → BF2 found key display
 *   mode_serialize == 0          → unknown/undecrypted display
 */

void subghz_protocol_decoder_psa2_get_string(void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderPSA* inst = context;

    if(inst->decrypted == 0x50 && inst->decrypted_type != 0) {
        subghz_custom_btn_set_original(inst->generic.btn == 0 ? 0xFF : inst->generic.btn);
        subghz_custom_btn_set_max(4);
        uint8_t display_btn = psa_get_btn_code();

        if(inst->decrypted_type == PSA_MODE_23) {
            furi_string_printf(output,
                "%s %dbit\r\n"
                "Key:0x%08lX%08lX\r\n"
                "SN:0x%lX Btn:%X\r\n"
                "CRC:%02X Cnt:%04lX",
                inst->base.protocol->name, 128,
                inst->key1_high, inst->key1_low,
                inst->generic.serial, display_btn,
                inst->decrypted_crc, inst->generic.cnt);
        } else {
            furi_string_printf(output,
                "%s %dbit\r\n"
                "Key:0x%08lX%08lX\r\n"
                "SN:0x%lX Btn:%X\r\n"
                "CRC:%04X Cnt:%08lX",
                inst->base.protocol->name, 128,
                inst->key1_high, inst->key1_low,
                inst->generic.serial, display_btn,
                inst->decrypted_crc, inst->generic.cnt);
        }
    } else {
        furi_string_printf(output,
            "%s %dbit\r\n"
            "Key:0x%08lX%08lX",
            inst->base.protocol->name, 128,
            inst->key1_high, inst->key1_low);
    }
}

/* =========================================================
 * ENCODER
 * =========================================================
 *
 * FUN_08029028 — build mode23 upload buffer
 * FUN_08029954 — build mode36 upload buffer
 *
 * Timing confirmed from FUN_080299a0:
 *   mode_serialize == 1 (mode23): te = 0xFA (250µs), end = 0x3E8 (1000µs)
 *   mode_serialize == 2 (mode36): te = 0x7D (125µs), end = 0x1F4 (500µs)
 *
 * Bit encoding (confirmed from encoder loop in FUN_080299a0):
 *   bit=1: level_duration_make(true,te) then level_duration_make(false,te)
 *   bit=0: level_duration_make(false,te) then level_duration_make(true,te)
 *
 * Preamble: 80 pairs of (true,te)+(false,te)
 * Sync transition: (false,te) + (true,te_long) + (false,te)
 * Data: 64 bits key1, 16 bits validation_field
 * End: (true,end_dur) + (false,end_dur)
 * repeat = 10
 */

void* subghz_protocol_encoder_psa2_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderPSA* inst = malloc(sizeof(SubGhzProtocolEncoderPSA));
    if(inst) {
        memset(inst, 0, sizeof(SubGhzProtocolEncoderPSA));
        inst->base.protocol      = &subghz_protocol_psa2;
        inst->generic.protocol_name = inst->base.protocol->name;
        inst->encoder.size_upload   = 600;
        inst->encoder.upload        = malloc(600 * sizeof(LevelDuration));
        inst->encoder.repeat        = 10;
    }
    return inst;
}

void subghz_protocol_encoder_psa2_free(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* inst = context;
    if(inst->encoder.upload) free(inst->encoder.upload);
    free(inst);
}

static void psa_encoder_build_upload(SubGhzProtocolEncoderPSA* inst) {
    uint32_t te, te_long_sync, end_dur;
    if(inst->mode == PSA_MODE_23) {
        te           = PSA_TE_SHORT_STD;   /* 250 µs */
        te_long_sync = PSA_TE_LONG_STD;    /* 500 µs */
        end_dur      = PSA_TE_END_1000;
    } else {
        te           = PSA_TE_SHORT_HALF;  /* 125 µs */
        te_long_sync = PSA_TE_LONG_HALF;   /* 250 µs */
        end_dur      = PSA_TE_END_500;
    }

    /* Rebuild key material */
    uint8_t buf[48] = {0};
    uint8_t preserve[2] = {0};
    uint8_t* pptr = NULL;
    if(inst->key1_low || inst->key1_high) {
        uint8_t orig[48] = {0};
        psa_setup_byte_buffer(orig, inst->key1_low, inst->key1_high, inst->key2_low);
        preserve[0] = orig[0]; preserve[1] = orig[1];
        pptr = preserve;
    }

    if(inst->mode == PSA_MODE_23) {
        /* psa_build_encrypt_mode23 equivalent */
        memset(buf, 0, 48);
        buf[2] = (inst->serial >> 16) & 0xFF;
        buf[3] = (inst->serial >>  8) & 0xFF;
        buf[4] =  inst->serial        & 0xFF;
        buf[5] = (inst->counter >> 8) & 0xFF;
        buf[6] =  inst->counter       & 0xFF;
        buf[7] =  inst->crc           & 0xFF;
        buf[8] =  inst->button        & 0xF;
        buf[9] =  inst->key2_low      & 0xFF;
        psa_second_stage_xor_encrypt(buf);
        psa_calculate_checksum(buf);
        buf[8] = (buf[8] & 0x0F) | (buf[11] & 0xF0);
        buf[13] = buf[9] ^ buf[8];
        buf[0] = pptr ? pptr[0] : buf[2] ^ buf[6];
        buf[1] = pptr ? pptr[1] : buf[3] ^ buf[7];
    } else {
        /* psa_build_encrypt_mode36 equivalent */
        memset(buf, 0, 48);
        uint32_t v0 = ((inst->serial & 0xFFFFFF) << 8) |
                      ((inst->button & 0xF) << 4) |
                      ((inst->counter >> 24) & 0x0F);
        uint32_t v1 = ((inst->counter & 0xFFFFFF) << 8) | (inst->crc & 0xFF);
        uint8_t  crc8 = psa_calculate_tea_crc(v0, v1);
        v1 = (v1 & 0xFFFFFF00) | crc8;
        uint32_t bf_counter = PSA_BF1_START | (inst->serial & 0xFFFFFF);
        uint32_t wk2 = PSA_BF1_CONST_U4, wk3 = bf_counter;
        psa_tea_encrypt(&wk2, &wk3, PSA_BF1_KEY_SCHEDULE);
        uint32_t wk0 = (bf_counter << 8) | 0x0E, wk1 = PSA_BF1_CONST_U5;
        psa_tea_encrypt(&wk0, &wk1, PSA_BF1_KEY_SCHEDULE);
        uint32_t wkey[4] = {wk0, wk1, wk2, wk3};
        psa_tea_encrypt(&v0, &v1, wkey);
        psa_unpack_tea_result(buf, v0, v1);
        buf[0] = pptr ? pptr[0] : buf[2] ^ buf[6];
        buf[1] = pptr ? pptr[1] : buf[3] ^ buf[7];
    }

    /* Pack into key1/validation */
    uint32_t k1h = ((uint32_t)buf[0]<<24)|((uint32_t)buf[1]<<16)|((uint32_t)buf[2]<<8)|buf[3];
    uint32_t k1l = ((uint32_t)buf[4]<<24)|((uint32_t)buf[5]<<16)|((uint32_t)buf[6]<<8)|buf[7];
    uint16_t vf  = ((uint16_t)buf[8]<<8) | buf[9];

    size_t idx = 0;

    /* Preamble: 80 pairs */
    for(int i = 0; i < 80 && idx < inst->encoder.size_upload - 2; i++) {
        inst->encoder.upload[idx++] = level_duration_make(true,  te);
        inst->encoder.upload[idx++] = level_duration_make(false, te);
    }

    /* Sync */
    if(idx < inst->encoder.size_upload - 3) {
        inst->encoder.upload[idx++] = level_duration_make(false, te);
        inst->encoder.upload[idx++] = level_duration_make(true,  te_long_sync);
        inst->encoder.upload[idx++] = level_duration_make(false, te);
    }

    /* key1 — 64 bits MSB first */
    uint64_t k1data = ((uint64_t)k1h << 32) | k1l;
    for(int bit = 63; bit >= 0 && idx < inst->encoder.size_upload - 2; bit--) {
        bool b = (k1data >> bit) & 1;
        inst->encoder.upload[idx++] = level_duration_make(b,  te);
        inst->encoder.upload[idx++] = level_duration_make(!b, te);
    }

    /* validation_field — 16 bits MSB first */
    for(int bit = 15; bit >= 0 && idx < inst->encoder.size_upload - 2; bit--) {
        bool b = (vf >> bit) & 1;
        inst->encoder.upload[idx++] = level_duration_make(b,  te);
        inst->encoder.upload[idx++] = level_duration_make(!b, te);
    }

    /* End burst */
    if(idx < inst->encoder.size_upload - 1) {
        inst->encoder.upload[idx++] = level_duration_make(true,  end_dur);
        inst->encoder.upload[idx++] = level_duration_make(false, end_dur);
    }

    inst->encoder.size_upload = idx;
    inst->encoder.front       = 0;
    inst->encoder.repeat      = 10;
    inst->key1_high           = k1h;
    inst->key1_low            = k1l;
    inst->key2_low            = vf;
}

SubGhzProtocolStatus subghz_protocol_encoder_psa2_deserialize(
    void* context, FlipperFormat* ff) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* inst = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;
    FuriString* tmp = furi_string_alloc();

    do {
        flipper_format_rewind(ff);
        ret = subghz_block_generic_deserialize(&inst->generic, ff);
        if(ret != SubGhzProtocolStatusOk) break;

        uint64_t k1 = inst->generic.data;
        inst->key1_low  = (uint32_t)(k1 & 0xFFFFFFFF);
        inst->key1_high = (uint32_t)(k1 >> 32);

        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "Key_2", tmp)) break;
        uint64_t k2 = 0;
        const char* ks = furi_string_get_cstr(tmp);
        for(size_t i = 0; i < strlen(ks); i++) {
            char c = ks[i]; if(c==' ') continue;
            uint8_t n=(c>='0'&&c<='9')?c-'0':(c>='A'&&c<='F')?c-'A'+10:(c>='a'&&c<='f')?c-'a'+10:0;
            k2 = (k2 << 4) | n;
        }
        inst->key2_low        = (uint32_t)(k2 & 0xFFFFFFFF);
        inst->validation_field = (uint16_t)(k2 & 0xFFFF);

        bool has_data = true;
        uint32_t serial=0, counter=0, seed=0;
        uint16_t crc=0;
        uint8_t  button=0, type=0;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Serial", tmp))
            serial = psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Cnt", tmp))
            counter = psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Btn", tmp))
            button = (uint8_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Type", tmp))
            type = (uint8_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "CRC", tmp))
            crc = (uint16_t)psa_parse_hex_str(furi_string_get_cstr(tmp));
        else has_data = false;

        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Seed", tmp))
            seed = psa_parse_hex_str(furi_string_get_cstr(tmp));
        UNUSED(seed);

        if(!has_data || type == 0) { ret = SubGhzProtocolStatusErrorParserOthers; break; }

        inst->serial  = serial;
        inst->counter = counter;
        inst->button  = button;
        inst->type    = type;
        inst->crc     = crc;
        inst->mode    = type; /* 0x23 or 0x36 */

        /* Custom button remapping (firmware-confirmed) */
        subghz_custom_btn_set_original(button == 0 ? 0xFF : button);
        subghz_custom_btn_set_max(4);
        inst->button = psa_get_btn_code();

        /* Increment counter — confirmed present in FUN_080299a0 */
        uint32_t mult = furi_hal_subghz_get_rolling_counter_mult();
        inst->counter = (inst->counter + mult) & 0xFFFFFFFF;

        psa_encoder_build_upload(inst);

        inst->generic.data   = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
        inst->generic.cnt    = inst->counter;
        inst->generic.serial = inst->serial;
        inst->generic.btn    = inst->button;

        /* Update file fields */
        flipper_format_rewind(ff);
        char s[32];
        if(inst->type == PSA_MODE_23)
            snprintf(s,sizeof(s),"%02X %02X",
                     (unsigned int)((inst->counter>>8)&0xFF), (unsigned int)(inst->counter&0xFF));
        else
            snprintf(s,sizeof(s),"%02X %02X %02X %02X",
                     (unsigned int)((inst->counter>>24)&0x0F), (unsigned int)((inst->counter>>16)&0xFF),
                     (unsigned int)((inst->counter>>8)&0xFF),  (unsigned int)(inst->counter&0xFF));
        flipper_format_update_string_cstr(ff, "Cnt", s);

        uint64_t k1out = ((uint64_t)inst->key1_high << 32) | inst->key1_low;
        snprintf(s,sizeof(s),"%02X %02X %02X %02X %02X %02X %02X %02X",
                 (uint8_t)(k1out>>56),(uint8_t)(k1out>>48),(uint8_t)(k1out>>40),(uint8_t)(k1out>>32),
                 (uint8_t)(k1out>>24),(uint8_t)(k1out>>16),(uint8_t)(k1out>>8),(uint8_t)k1out);
        flipper_format_update_string_cstr(ff, "Key", s);

        snprintf(s,sizeof(s),"00 00 00 00 00 00 %02X %02X",
                 (unsigned int)((inst->key2_low>>8)&0xFF), (unsigned int)(inst->key2_low&0xFF));
        flipper_format_update_string_cstr(ff, "Key_2", s);

        inst->is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    furi_string_free(tmp);
    return ret;
}

void subghz_protocol_encoder_psa2_stop(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* inst = context;
    inst->is_running         = false;
    inst->encoder.is_running = false;
}

LevelDuration subghz_protocol_encoder_psa2_yield(void* context) {
    furi_assert(context);
    SubGhzProtocolEncoderPSA* inst = context;
    if(!inst->is_running || inst->encoder.size_upload == 0) {
        inst->is_running = false;
        return level_duration_reset();
    }
    LevelDuration ret = inst->encoder.upload[inst->encoder.front++];
    if(inst->encoder.front >= inst->encoder.size_upload) {
        inst->encoder.front = 0;
        if(--inst->encoder.repeat <= 0) inst->is_running = false;
    }
    return ret;
}
