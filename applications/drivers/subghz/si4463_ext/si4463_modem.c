#include "si4463_modem.h"
#include "si4463_wds.h"
#include <string.h>

#define CC1101_MDMCFG4        0x10
#define CC1101_MDMCFG3        0x11
#define CC1101_MDMCFG2        0x12
#define CC1101_DEVIATN        0x15
#define CC1101_FREND0         0x22
#define CC1101_PATABLE_COUNT  8
#define SI4463_PA_PWR_LVL_MAX 0x7F

#define CC1101_XO_HZ 26000000ull
#define SI4463_XO_HZ 30000000ull

static const uint8_t si4463_prop_pa[] = {0x11, 0x22, 0x04, 0x00, 0x08, 0x7F, 0x00, 0x3D};

typedef struct {
    const uint8_t* cmd;
    size_t len;
} Si4463Prop;

#define SI4463_PROP(p) {p, sizeof(p)}

static const Si4463Prop si4463_base[] = {
    SI4463_PROP(si4463_prop_pa),
};

static bool si4463_stream(Si4463CmdSend send, void* context, const uint8_t* data, size_t size) {
    size_t pos = 0;
    while(pos < size) {
        size_t len = data[pos++];
        if(!len) return pos == size;
        if(len > 16 || len > size - pos || !send(data + pos, len, context)) return false;
        pos += len;
    }
    return false;
}
#define STREAM(name) si4463_stream(send, context, si4463_wds_##name, sizeof(si4463_wds_##name))

bool si4463_modem_load_patch(Si4463CmdSend send, void* context) {
    return STREAM(patch);
}

bool si4463_modem_calibrate(Si4463CmdSend send, void* context) {
    const uint8_t ready[] = {0x34, 0x03};
    return STREAM(calibration) && send(ready, sizeof(ready), context);
}

bool si4463_modem_load_base(Si4463CmdSend send, void* context) {
    for(size_t i = 0; i < sizeof(si4463_base) / sizeof(si4463_base[0]); i++) {
        if(!send(si4463_base[i].cmd, si4463_base[i].len, context)) return false;
    }
    return true;
}

typedef struct {
    uint8_t patable;
    int8_t dbm;
} Si4463PaStep;

static const Si4463PaStep si4463_pa_steps[] = {
    {0xCD, 7},
    {0xC8, 7},
    {0x86, 5},
    {0x84, 5},
    {0x50, 0},
    {0x60, 0},
    {0x26, -10},
    {0x34, -10},
    {0x1D, -15},
    {0x17, -20},
    {0x0E, -20},
    {0x03, -30},
    {0x12, -30},
};

static const struct {
    int8_t dbm;
    uint8_t level;
} si4463_pa_levels[] = {
    {7, 0x1C},
    {5, 0x17},
    {0, 0x0D},
    {-10, 0x04},
    {-15, 0x02},
    {-20, 0x01},
    {-30, 0x00},
};

static uint8_t si4463_pa_level_from_patable(uint8_t patable) {
    for(size_t i = 0; i < sizeof(si4463_pa_steps) / sizeof(si4463_pa_steps[0]); i++) {
        if(si4463_pa_steps[i].patable != patable) continue;
        for(size_t j = 0; j < sizeof(si4463_pa_levels) / sizeof(si4463_pa_levels[0]); j++) {
            if(si4463_pa_levels[j].dbm == si4463_pa_steps[i].dbm) return si4463_pa_levels[j].level;
        }
    }
    return SI4463_PA_PWR_LVL_MAX;
}

void si4463_modem_from_cc1101(const uint8_t* preset, Si4463Modem* modem) {
    uint8_t mdmcfg2 = 0x30;
    uint8_t mdmcfg3 = 0x32;
    uint8_t mdmcfg4 = 0x67;
    uint8_t deviatn = 0x00;
    uint8_t frend0 = 0x11;
    modem->pa_level = SI4463_PA_PWR_LVL_MAX;
    modem->wds_profile = -1;
    modem->forced_profile = -1;

    if(preset) {
        const uint8_t* data = preset;
        while(data[0] || data[1]) {
            const uint8_t reg = data[0];
            const uint8_t val = data[1];
            data += 2;
            if(reg == CC1101_MDMCFG2) {
                mdmcfg2 = val;
            } else if(reg == CC1101_MDMCFG3) {
                mdmcfg3 = val;
            } else if(reg == CC1101_MDMCFG4) {
                mdmcfg4 = val;
            } else if(reg == CC1101_DEVIATN) {
                deviatn = val;
            } else if(reg == CC1101_FREND0) {
                frend0 = val;
            } else if(reg == 0xFC) {
                modem->forced_profile = (val == 0xFF) ? -1 : (int8_t)val;
            }
        }
        const uint8_t* patable = data + 2;
        modem->pa_level =
            si4463_pa_level_from_patable(patable[frend0 & (CC1101_PATABLE_COUNT - 1)]);
    }

    switch((mdmcfg2 >> 4) & 0x07) {
    case 0x00:
    case 0x07:
        modem->mod_type = SI4463_MOD_2FSK;
        break;
    case 0x01:
        modem->mod_type = SI4463_MOD_2GFSK;
        break;
    default:
        modem->mod_type = SI4463_MOD_OOK;
        break;
    }

    const uint32_t drate_e = mdmcfg4 & 0x0F;
    modem->bps = (uint32_t)((((uint64_t)(256u + mdmcfg3) << drate_e) * CC1101_XO_HZ) >> 28);
    if(modem->bps == 0) modem->bps = 1000;

    const uint32_t dev_e = (deviatn >> 4) & 0x07;
    const uint32_t dev_m = deviatn & 0x07;
    modem->deviation_hz = (uint32_t)((CC1101_XO_HZ * (8u + dev_m) * (1ull << dev_e)) / 131072ull);

    const uint32_t bw_e = (mdmcfg4 >> 6) & 0x03;
    const uint32_t bw_m = (mdmcfg4 >> 4) & 0x03;
    modem->rx_bw_hz = (uint32_t)(CC1101_XO_HZ / (8u * (4u + bw_m) * (1u << bw_e)));

    if(si4463_modem_profile_valid(modem->forced_profile) &&
       si4463_modem_profile_is_ook(modem->forced_profile) == si4463_modem_is_ook(modem)) {
        modem->wds_profile = modem->forced_profile;
    } else {
        modem->wds_profile = si4463_modem_default_profile(modem);
    }
}

bool si4463_modem_is_ook(const Si4463Modem* modem) {
    return modem->mod_type == SI4463_MOD_OOK;
}

#define SI4463_WDS_PROFILE_COUNT (sizeof(si4463_wds_profiles) / sizeof(si4463_wds_profiles[0]))

int8_t si4463_modem_profile_find(const char* name) {
    for(size_t i = 0; name && i < SI4463_WDS_PROFILE_COUNT; i++) {
        if(!strcmp(si4463_wds_profiles[i].name, name)) return (int8_t)i;
    }
    return -1;
}

static bool si4463_modem_profile_known(int8_t profile) {
    return profile >= 0 && (size_t)profile < SI4463_WDS_PROFILE_COUNT;
}

bool si4463_modem_profile_valid(int8_t profile) {
    return si4463_modem_profile_known(profile) && si4463_wds_profiles[profile].stream;
}

uint8_t si4463_modem_profile_pkt_config1(int8_t profile) {
    uint8_t value = SI4463_PKT_CONFIG1;
    if(!si4463_modem_profile_valid(profile)) return value;
    const uint8_t* s = si4463_wds_profiles[profile].stream;
    for(size_t pos = 0; s[pos]; pos += 1 + s[pos]) {
        const uint8_t* cmd = &s[pos + 1];
        if(cmd[1] == 0x12 && cmd[3] <= 0x06 && 0x06 < cmd[3] + cmd[2])
            value = cmd[4 + 0x06 - cmd[3]];
    }
    return value;
}

bool si4463_modem_profile_is_ook(int8_t profile) {
    return si4463_modem_profile_valid(profile) && si4463_wds_profiles[profile].ook;
}

const char* si4463_modem_profile_name(int8_t profile) {
    return si4463_modem_profile_known(profile) ? si4463_wds_profiles[profile].name : "none";
}

#define SI4463_OOK_WIDE_HZ   460000u
#define SI4463_FSK_NARROW_HZ 10000u
#define SI4463_FSK_WIDE_HZ   30000u
#define SI4463_FSK_FAST_BPS  30000u
#define SI4463_FSK_MID_BPS   10000u

int8_t si4463_modem_default_profile(const Si4463Modem* modem) {
    if(si4463_modem_is_ook(modem))
        return si4463_modem_profile_find(modem->rx_bw_hz > SI4463_OOK_WIDE_HZ ? "AM800" : "AM650");
    if(modem->deviation_hz < SI4463_FSK_NARROW_HZ)
        return si4463_modem_profile_find("FM12K_BW200_AFC");
    if(modem->deviation_hz > SI4463_FSK_WIDE_HZ) return si4463_modem_profile_find("FM476_AFC");
    if(modem->bps > SI4463_FSK_FAST_BPS) return si4463_modem_profile_find("FM14K_BW200_AFC");
    if(modem->bps > SI4463_FSK_MID_BPS) return si4463_modem_profile_find("FM20K_AFC");
    return si4463_modem_profile_find("FM12K_BW200_AFC");
}

bool si4463_modem_apply_tx(Si4463CmdSend send, void* context, const Si4463Modem* modem) {
    uint32_t dev =
        si4463_modem_is_ook(modem) ?
            0 :
            (uint32_t)(((uint64_t)modem->deviation_hz * 524288ull * 8ull) / (2ull * SI4463_XO_HZ));
    if(dev > 0x1FFFFu) dev = 0x1FFFFu;
    const uint8_t deviation[] = {
        0x11, 0x20, 0x03, 0x0A, (uint8_t)(dev >> 16), (uint8_t)(dev >> 8), (uint8_t)dev};
    const uint8_t pa[] = {0x11, 0x22, 0x01, 0x01, modem->pa_level};
    return send(deviation, sizeof(deviation), context) && send(pa, sizeof(pa), context);
}

bool si4463_modem_apply(Si4463CmdSend send, void* context, const Si4463Modem* modem) {
    if(!si4463_modem_profile_valid(modem->wds_profile)) return false;
    const Si4463WdsProfile* profile = &si4463_wds_profiles[modem->wds_profile];
    if(!STREAM(neutral)) return false;
    if(!si4463_stream(send, context, profile->stream, profile->size)) return false;
    const uint8_t mod[] = {
        0x11, 0x20, 0x01, 0x00, profile->ook ? SI4463_MOD_OOK : SI4463_MOD_2FSK};
    return send(mod, sizeof(mod), context) && si4463_modem_apply_tx(send, context, modem);
}
