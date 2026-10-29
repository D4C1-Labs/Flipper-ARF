#pragma once

#include <furi.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/types.h>
#include <lib/subghz/blocks/const.h>
#include <lib/subghz/blocks/decoder.h>
#include <lib/subghz/blocks/encoder.h>
#include <lib/subghz/blocks/generic.h>
#include <lib/subghz/blocks/math.h>
#include <flipper_format/flipper_format.h>
#include <lib/toolbox/manchester_decoder.h>

#include <m-array.h>

#define bit(x, n) (((x) >> (n)) & 1)

#define g5(x, a, b, c, d, e) \
    (bit(x, a) + bit(x, b) * 2 + bit(x, c) * 4 + bit(x, d) * 8 + bit(x, e) * 16)

struct SubGhzKeystore {
    SubGhzKeyArray_t data;
    const char* mfname;
    uint8_t kl_type;
};

#define KEELOQ_NLF 0x3A5C742E

#define KEELOQ_LEARNING_UNKNOWN             0u
#define KEELOQ_LEARNING_SIMPLE              1u
#define KEELOQ_LEARNING_NORMAL              2u
#define KEELOQ_LEARNING_SECURE              3u
#define KEELOQ_LEARNING_MAGIC_XOR_TYPE_1    4u
#define KEELOQ_LEARNING_FAAC                5u
#define KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_1 6u
#define KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_2 7u
#define KEELOQ_LEARNING_MAGIC_SERIAL_TYPE_3 8u

#define KEELOQ_LEARNING_SIMPLE_KINGGATES    10u
#define KEELOQ_LEARNING_NORMAL_JAROLIFT     11u

static inline uint32_t
    subghz_protocol_keeloq_common_encrypt(const uint32_t data, const uint64_t key) {
    uint32_t x = data, r;
    for(r = 0; r < 528; r++)
        x = (x >> 1) ^ ((bit(x, 0) ^ bit(x, 16) ^ (uint32_t)bit(key, r & 63) ^
                         bit(KEELOQ_NLF, g5(x, 1, 9, 20, 26, 31)))
                        << 31);
    return x;
}

static inline uint32_t
    subghz_protocol_keeloq_common_decrypt(const uint32_t data, const uint64_t key) {
    uint32_t x = data, r;
    for(r = 0; r < 528; r++)
        x = (x << 1) ^ bit(x, 31) ^ bit(x, 15) ^ (uint32_t)bit(key, (15 - r) & 63) ^
            bit(KEELOQ_NLF, g5(x, 0, 8, 19, 25, 30));
    return x;
}

static inline uint64_t
    subghz_protocol_keeloq_common_normal_learning(uint32_t data, const uint64_t key) {
    uint32_t k1, k2;

    data &= 0x0FFFFFFF;
    data |= 0x20000000;
    k1 = subghz_protocol_keeloq_common_decrypt(data, key);

    data &= 0x0FFFFFFF;
    data |= 0x60000000;
    k2 = subghz_protocol_keeloq_common_decrypt(data, key);

    return ((uint64_t)k2 << 32) | k1;
}

static inline uint64_t
    subghz_protocol_keeloq_common_magic_xor_type1_learning(uint32_t data, uint64_t xor) {
    data &= 0x0FFFFFFF;
    return (((uint64_t)data << 32) | data) ^ xor;
}

static inline uint64_t
    subghz_protocol_keeloq_common_faac_learning(const uint32_t seed, const uint64_t key) {
    uint16_t hs = seed >> 16;
    const uint16_t ending = 0x544D;
    uint32_t lsb = (uint32_t)hs << 16 | ending;
    uint64_t man = (uint64_t)subghz_protocol_keeloq_common_encrypt(seed, key) << 32 |
                   subghz_protocol_keeloq_common_encrypt(lsb, key);
    return man;
}

static inline uint64_t
    subghz_protocol_keeloq_common_magic_serial_type1_learning(uint32_t data, uint64_t man) {
    return (man & 0xFFFFFFFF) | ((uint64_t)data << 40) |
           ((uint64_t)(((data & 0xff) + ((data >> 8) & 0xFF)) & 0xFF) << 32);
}

static inline uint64_t
    subghz_protocol_keeloq_common_magic_serial_type2_learning(uint32_t data, uint64_t man) {
    uint8_t* p = (uint8_t*)&data;
    uint8_t* m = (uint8_t*)&man;
    m[7] = p[0];
    m[6] = p[1];
    m[5] = p[2];
    m[4] = p[3];
    return man;
}

static inline uint64_t
    subghz_protocol_keeloq_common_magic_serial_type3_learning(uint32_t data, uint64_t man) {
    return (man & 0xFFFFFFFFFF000000) | (data & 0xFFFFFF);
}
