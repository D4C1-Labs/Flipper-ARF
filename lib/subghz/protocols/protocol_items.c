#include "protocol_items.h" // IWYU pragma: keep

#include <furi.h>
#include <string.h>

#define TAG "ProtocolCatalog"

#define SUBGHZ_PROTOCOL_CATALOG_CC1101_REG_MDMCFG2 0x12U
#define SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_MASK 0x70U
#define SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_2FSK 0x00U
#define SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_GFSK 0x10U
#define SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_ASK_OOK 0x30U
#define SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_4FSK 0x40U
#define SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_MSK 0x70U
#define SUBGHZ_PROTOCOL_CATALOG_VAG_FREQUENCY_MIN 434190000UL
#define SUBGHZ_PROTOCOL_CATALOG_VAG_FREQUENCY_MAX 434450000UL

#define SUBGHZ_PROTOCOL_CATALOG_COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

#define SUBGHZ_PROTOCOL_CATALOG_TX_KEY(key) key

const SubGhzProtocol* const subghz_protocol_registry_items[] = {
    //&subghz_protocol_gate_tx,
    //&subghz_protocol_keeloq,
    //&subghz_protocol_nice_flo,
    //&subghz_protocol_came,
    //&subghz_protocol_faac_slh,
    //&subghz_protocol_nice_flor_s,
    //&subghz_protocol_came_twee,
    //&subghz_protocol_came_atomo,
    //&subghz_protocol_nero_sketch,
    //&subghz_protocol_ido,
    //&subghz_protocol_hormann,
    //&subghz_protocol_nero_radio,
    //&subghz_protocol_somfy_telis,
    //&subghz_protocol_somfy_keytis,
    //&subghz_protocol_princeton,
    &subghz_protocol_raw,
    //&subghz_protocol_linear,
    //&subghz_protocol_secplus_v2,
    //&subghz_protocol_secplus_v1,
    //&subghz_protocol_megacode,
    //&subghz_protocol_holtek,        
    //&subghz_protocol_chamb_code,
    //&subghz_protocol_power_smart,   
    //&subghz_protocol_marantec,
    //&subghz_protocol_bett,          
    //&subghz_protocol_doitrand,
    //&subghz_protocol_phoenix_v2,    
    //&subghz_protocol_honeywell_wdb,
    //&subghz_protocol_magellan,      
    //&subghz_protocol_intertechno_v3,
    //&subghz_protocol_clemsa,        
    //&subghz_protocol_ansonic,
    //&subghz_protocol_smc5326,       
    //&subghz_protocol_holtek_th12x,
    //&subghz_protocol_linear_delta3, 
    //&subghz_protocol_dooya,
    //&subghz_protocol_alutech_at_4n, 
    //&subghz_protocol_kinggates_stylo_4k,
    &subghz_protocol_bin_raw,       
    //&subghz_protocol_mastercode,
    //&subghz_protocol_honeywell,     
    //&subghz_protocol_legrand,
    //&subghz_protocol_dickert_mahs,  
    //&subghz_protocol_gangqi,
    //&subghz_protocol_marantec24,    
    //&subghz_protocol_hollarm,
    //&subghz_protocol_hay21,         
    //&subghz_protocol_revers_rb2,
    //&subghz_protocol_feron,         
    //&subghz_protocol_roger,
    //&subghz_protocol_elplast,       
    //&subghz_protocol_treadmill37,
    //&subghz_protocol_beninca_arc,
    //&subghz_protocol_keyfinder,  
    //&subghz_protocol_jarolift,
    &subghz_protocol_vag,          
    &subghz_protocol_porsche_cayenne,  
    &subghz_protocol_ford_v0,
    &subghz_protocol_psa,
    &subghz_protocol_fiat_spa,       
    //&subghz_protocol_fiat_marelli,
    &fiat_protocol_v0,
    &fiat_v1_protocol,
    &fiat_v2_protocol,
    &renault_v0_protocol,
 // &subghz_protocol_bmw_cas4,
    &subghz_protocol_subaru, 
    &subghz_protocol_mazda_siemens,
    &subghz_protocol_kia_v0,       
    &subghz_protocol_kia_v1,
    &subghz_protocol_kia_v2,       
    &subghz_protocol_kia_v3_v4,
    &subghz_protocol_kia_v5,       
    &subghz_protocol_kia_v6,
    &subghz_protocol_suzuki, 
    &subghz_protocol_mitsubishi_v0,
    &subghz_protocol_star_line,
    &subghz_protocol_scher_khan,
    &subghz_protocol_sheriff_cfm,
    // until fix &subghz_protocol_honda,
    &subghz_protocol_chrysler,
    &subghz_protocol_kia_v7,
    &subghz_protocol_mazda_v0,
    &ford_protocol_v1,
    &ford_protocol_v2,
    &ford_protocol_v3,
    //&subghz_protocol_land_rover_v0,
    &subghz_protocol_toyota,
    &honda_static_protocol,
    &honda_v1_protocol,
    &honda_v2_protocol,


};

const SubGhzProtocolRegistry subghz_protocol_registry = {
    .items = subghz_protocol_registry_items,
    .size = COUNT_OF(subghz_protocol_registry_items)};

typedef enum {
    SubGhzProtocolCatalogModulationAM = 0,
    SubGhzProtocolCatalogModulationFM,
} SubGhzProtocolCatalogModulation;

typedef struct {
    const char* alias;
    const char* canonical_name;
} SubGhzProtocolCatalogAlias;

static const SubGhzProtocolCatalogEntry subghz_protocol_catalog[] = {
    {"Chrysler V0", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("chrysler_v0")},
    {"Fiat V0", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("fiat_v0")},
    {"Fiat V1", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("fiat_v1")},
    {"Fiat V2", SubGhzProtocolCatalogRoutePolicyAMDefault, NULL},
    {"Ford V0", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("ford_v0")},
    {"Ford V1", SubGhzProtocolCatalogRoutePolicyFMF4,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("ford_v1")},
    {"Ford V2", SubGhzProtocolCatalogRoutePolicyFMF4,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("ford_v2")},
    {"Ford V3", SubGhzProtocolCatalogRoutePolicyFMF4, NULL},
    {"Honda Static", SubGhzProtocolCatalogRoutePolicyFMHonda1,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("honda_static")},
    {"Honda V1", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("honda_v1")},
    {"Kia V0", SubGhzProtocolCatalogRoutePolicyFMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("kia_v0")},
    {"Kia V1", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("kia_v1")},
    {"Kia V2", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("kia_v2")},
    {"Kia V3/V4", SubGhzProtocolCatalogRoutePolicyFMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("kia_v3_v4")},
    {"Kia V5", SubGhzProtocolCatalogRoutePolicyFMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("kia_v5")},
    {"Kia V6", SubGhzProtocolCatalogRoutePolicyFMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("kia_v6")},
    {"Kia V7", SubGhzProtocolCatalogRoutePolicyFMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("kia_v7")},
    {"Honda V2", SubGhzProtocolCatalogRoutePolicyFMF4,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("honda_v2")},
    {"Mazda V0", SubGhzProtocolCatalogRoutePolicyByModulation,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("mazda_v0")},
    {"Mitsubishi V0", SubGhzProtocolCatalogRoutePolicyFMDefault, NULL},
    {"Porsche Touareg", SubGhzProtocolCatalogRoutePolicyAMDefault, NULL},
    {"PSA", SubGhzProtocolCatalogRoutePolicyByModulation,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("psa")},
    {"Renault V0", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("renault_v0")},
    {"Scher-Khan", SubGhzProtocolCatalogRoutePolicyFMDefault, NULL},
    {"Star Line", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("star_line")},
    {"Subaru", SubGhzProtocolCatalogRoutePolicyAMDefault,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("subaru")},
    {"VAG", SubGhzProtocolCatalogRoutePolicyAMVag,
     SUBGHZ_PROTOCOL_CATALOG_TX_KEY("vag")},
};

static const SubGhzProtocolCatalogAlias subghz_protocol_catalog_aliases[] = {
    {"StarLine", "Star Line"},
    {"Kia V3", "Kia V3/V4"},
    {"Kia V4", "Kia V3/V4"},
    {"KIA/HYU V3", "Kia V3/V4"},
    {"KIA/HYU V4", "Kia V3/V4"},
    {"Suzuki", "Kia V0"},
    {"Suzuki V0", "Kia V0"},
    {"Honda V0", "Kia V0"},
    {"Land Rover V0", "Honda V2"},
    {"VW", "VAG"},
};

static bool catalog_string_equal(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}

static bool catalog_string_contains(const char* haystack, const char* needle) {
    return haystack && needle && strstr(haystack, needle) != NULL;
}

static const SubGhzProtocolCatalogEntry*
    catalog_find_canonical(const char* canonical_name) {
    if(!canonical_name || canonical_name[0] == '\0') {
        return NULL;
    }
    for(size_t i = 0; i < SUBGHZ_PROTOCOL_CATALOG_COUNT_OF(subghz_protocol_catalog); i++) {
        if(catalog_string_equal(
               canonical_name, subghz_protocol_catalog[i].canonical_name)) {
            return &subghz_protocol_catalog[i];
        }
    }
    return NULL;
}

static const char* catalog_alias_to_canonical(const char* protocol_name) {
    if(!protocol_name || protocol_name[0] == '\0') {
        return NULL;
    }
    for(size_t i = 0; i < SUBGHZ_PROTOCOL_CATALOG_COUNT_OF(subghz_protocol_catalog_aliases); i++) {
        if(catalog_string_equal(
               protocol_name, subghz_protocol_catalog_aliases[i].alias)) {
            return subghz_protocol_catalog_aliases[i].canonical_name;
        }
    }
    return NULL;
}

static const SubGhzProtocolCatalogEntry*
    catalog_find_substring(const char* protocol_name) {
    if(!protocol_name || protocol_name[0] == '\0') {
        return NULL;
    }
    for(size_t i = 0; i < SUBGHZ_PROTOCOL_CATALOG_COUNT_OF(subghz_protocol_catalog); i++) {
        if(catalog_string_contains(
               protocol_name, subghz_protocol_catalog[i].canonical_name)) {
            return &subghz_protocol_catalog[i];
        }
    }
    for(size_t i = 0; i < SUBGHZ_PROTOCOL_CATALOG_COUNT_OF(subghz_protocol_catalog_aliases); i++) {
        if(catalog_string_contains(
               protocol_name, subghz_protocol_catalog_aliases[i].alias)) {
            return catalog_find_canonical(
                subghz_protocol_catalog_aliases[i].canonical_name);
        }
    }
    return NULL;
}

const SubGhzProtocolCatalogEntry*
    subghz_protocol_catalog_find(const char* protocol_name) {
    if(!protocol_name || protocol_name[0] == '\0') {
        return NULL;
    }
    const SubGhzProtocolCatalogEntry* entry =
        catalog_find_canonical(protocol_name);
    if(entry) {
        return entry;
    }

    const char* canonical_name = catalog_alias_to_canonical(protocol_name);
    if(canonical_name) {
        return catalog_find_canonical(canonical_name);
    }
    return NULL;
}

const char* subghz_protocol_catalog_canonical_name(const char* protocol_name) {
    const SubGhzProtocolCatalogEntry* entry = subghz_protocol_catalog_find(protocol_name);
    return entry ? entry->canonical_name : protocol_name;
}

bool subghz_protocol_catalog_can_tx(const char* protocol_name) {
    return subghz_protocol_catalog_tx_key(protocol_name) != NULL;
}

const char* subghz_protocol_catalog_tx_key(const char* protocol_name) {
    const SubGhzProtocolCatalogEntry* entry = subghz_protocol_catalog_find(protocol_name);
    return entry ? entry->tx_key : NULL;
}

const char*
    subghz_protocol_catalog_display_name(const char* protocol_name, uint32_t protocol_type) {
    if(!protocol_name) {
        return NULL;
    }

    if(catalog_string_equal(protocol_name, "Suzuki") ||
       catalog_string_equal(protocol_name, "Suzuki V0")) {
        return "Suzuki V0";
    }
    if(catalog_string_equal(protocol_name, "Honda V0")) {
        return "Honda V0";
    }

    const char* canonical_name = subghz_protocol_catalog_canonical_name(protocol_name);
    if(catalog_string_equal(canonical_name, "Kia V0")) {
        if(protocol_type == 2U) {
            return "Suzuki V0";
        }
        if(protocol_type == 3U) {
            return "Honda V0";
        }
    }

    return canonical_name;
}

static bool catalog_preset_try_get_register(
    const uint8_t* preset_data,
    size_t preset_data_size,
    uint8_t reg,
    uint8_t* value) {
    if(!preset_data || !value || (preset_data_size < 2U)) {
        return false;
    }

    for(size_t i = 0; i + 1U < preset_data_size; i += 2U) {
        const uint8_t address = preset_data[i];
        const uint8_t data = preset_data[i + 1U];

        if((address == 0x00U) && (data == 0x00U)) {
            break;
        }

        if(address == reg) {
            *value = data;
            return true;
        }
    }

    return false;
}

static SubGhzProtocolCatalogModulation catalog_get_modulation(
    const uint8_t* preset_data,
    size_t preset_data_size) {
    uint8_t mdmcfg2 = 0U;

    if(!catalog_preset_try_get_register(
           preset_data, preset_data_size, SUBGHZ_PROTOCOL_CATALOG_CC1101_REG_MDMCFG2, &mdmcfg2)) {
        FURI_LOG_W(TAG, "Preset missing MDMCFG2, defaulting to AM registry");
        return SubGhzProtocolCatalogModulationAM;
    }

    switch(mdmcfg2 & SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_MASK) {
    case SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_ASK_OOK:
        return SubGhzProtocolCatalogModulationAM;
    case SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_2FSK:
    case SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_GFSK:
    case SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_4FSK:
    case SUBGHZ_PROTOCOL_CATALOG_CC1101_MOD_FORMAT_MSK:
        return SubGhzProtocolCatalogModulationFM;
    default:
        FURI_LOG_W(TAG, "Unknown MDMCFG2 0x%02X, defaulting to AM registry", mdmcfg2);
        return SubGhzProtocolCatalogModulationAM;
    }
}

static bool catalog_frequency_in_vag_band(uint32_t frequency) {
    return frequency >= SUBGHZ_PROTOCOL_CATALOG_VAG_FREQUENCY_MIN &&
           frequency <= SUBGHZ_PROTOCOL_CATALOG_VAG_FREQUENCY_MAX;
}

static SubGhzProtocolCatalogRoute catalog_route_from_policy(
    SubGhzProtocolCatalogRoutePolicy policy,
    SubGhzProtocolCatalogModulation modulation) {
    switch(policy) {
    case SubGhzProtocolCatalogRoutePolicyAMVag:
        return SubGhzProtocolCatalogRouteAMVag;
    case SubGhzProtocolCatalogRoutePolicyFMDefault:
        return SubGhzProtocolCatalogRouteFMDefault;
    case SubGhzProtocolCatalogRoutePolicyFMF4:
        return SubGhzProtocolCatalogRouteFMF4;
    case SubGhzProtocolCatalogRoutePolicyFMHonda1:
        return SubGhzProtocolCatalogRouteFMHonda1;
    case SubGhzProtocolCatalogRoutePolicyByModulation:
        return (modulation == SubGhzProtocolCatalogModulationAM) ?
                   SubGhzProtocolCatalogRouteAMDefault :
                   SubGhzProtocolCatalogRouteFMDefault;
    case SubGhzProtocolCatalogRoutePolicyAMDefault:
    default:
        return SubGhzProtocolCatalogRouteAMDefault;
    }
}

SubGhzProtocolCatalogRoute subghz_protocol_catalog_get_route(
    const char* preset_name,
    uint32_t frequency,
    const uint8_t* preset_data,
    size_t preset_data_size,
    const char* protocol_name) {
    const SubGhzProtocolCatalogEntry* entry =
        subghz_protocol_catalog_find(protocol_name);
    if(!entry) {
        entry = catalog_find_substring(protocol_name);
    }

    if(entry && entry->route_policy != SubGhzProtocolCatalogRoutePolicyByModulation) {
        return catalog_route_from_policy(
            entry->route_policy, SubGhzProtocolCatalogModulationAM);
    }

    const SubGhzProtocolCatalogModulation modulation =
        catalog_get_modulation(preset_data, preset_data_size);

    if(entry) {
        return catalog_route_from_policy(entry->route_policy, modulation);
    }

    if(modulation == SubGhzProtocolCatalogModulationAM) {
        if(catalog_frequency_in_vag_band(frequency)) {
            return SubGhzProtocolCatalogRouteAMVag;
        }
        return SubGhzProtocolCatalogRouteAMDefault;
    }

    if(catalog_string_contains(preset_name, "F4")) {
        return SubGhzProtocolCatalogRouteFMF4;
    }
    if(catalog_string_contains(preset_name, "Honda1") ||
       catalog_string_contains(preset_name, "Honda 1")) {
        return SubGhzProtocolCatalogRouteFMHonda1;
    }
    return SubGhzProtocolCatalogRouteFMDefault;
}

const char* subghz_protocol_catalog_get_route_name(SubGhzProtocolCatalogRoute route) {
    switch(route) {
    case SubGhzProtocolCatalogRouteAMVag:
        return "AM_VAG";
    case SubGhzProtocolCatalogRouteFMDefault:
        return "FM_DEFAULT";
    case SubGhzProtocolCatalogRouteFMF4:
        return "FM_F4";
    case SubGhzProtocolCatalogRouteFMHonda1:
        return "FM_HONDA1";
    case SubGhzProtocolCatalogRouteAMDefault:
    default:
        return "AM_DEFAULT";
    }
}

const char* subghz_protocol_catalog_route_to_preset_name(SubGhzProtocolCatalogRoute route) {
    switch(route) {
    case SubGhzProtocolCatalogRouteAMVag:
        return "AM270";
    case SubGhzProtocolCatalogRouteFMDefault:
        return "FM476";
    case SubGhzProtocolCatalogRouteFMF4:
        return "FM12K";
    case SubGhzProtocolCatalogRouteFMHonda1:
        return "FM238";
    case SubGhzProtocolCatalogRouteAMDefault:
    default:
        return "AM650";
    }
}
