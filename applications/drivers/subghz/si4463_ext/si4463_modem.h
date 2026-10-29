#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef bool (*Si4463CmdSend)(const uint8_t* cmd, size_t len, void* context);

#define SI4463_MOD_OOK   0x89
#define SI4463_MOD_2FSK  0x8A
#define SI4463_MOD_2GFSK 0x8B

#define SI4463_PKT_CONFIG1 0x40

typedef struct {
    uint8_t mod_type;
    uint32_t bps;
    uint32_t deviation_hz;
    uint32_t rx_bw_hz;
    uint8_t pa_level;
    int8_t wds_profile;
    int8_t forced_profile;
} Si4463Modem;

int8_t si4463_modem_profile_find(const char* name);

bool si4463_modem_profile_is_ook(int8_t profile);

bool si4463_modem_profile_valid(int8_t profile);
uint8_t si4463_modem_profile_pkt_config1(int8_t profile);

const char* si4463_modem_profile_name(int8_t profile);

int8_t si4463_modem_default_profile(const Si4463Modem* modem);

void si4463_modem_from_cc1101(const uint8_t* preset, Si4463Modem* modem);

bool si4463_modem_load_base(Si4463CmdSend send, void* context);

bool si4463_modem_is_ook(const Si4463Modem* modem);

bool si4463_modem_apply(Si4463CmdSend send, void* context, const Si4463Modem* modem);

bool si4463_modem_load_patch(Si4463CmdSend send, void* context);

bool si4463_modem_calibrate(Si4463CmdSend send, void* context);

bool si4463_modem_apply_tx(Si4463CmdSend send, void* context, const Si4463Modem* modem);
