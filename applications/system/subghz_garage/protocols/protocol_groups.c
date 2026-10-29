#include "protocol_groups.h"
#include "protocol_items.h"
#include <storage/storage.h>
#include <string.h>

const char* const subghz_garage_protocol_group_paths[SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT] = {
    APP_ASSETS_PATH("plugins/grp_g1.fal"),
    APP_ASSETS_PATH("plugins/grp_g2.fal"),
    APP_ASSETS_PATH("plugins/grp_g3.fal"),
    APP_ASSETS_PATH("plugins/grp_g4.fal"),
    APP_ASSETS_PATH("plugins/grp_g5.fal"),
    APP_ASSETS_PATH("plugins/grp_g6.fal"),
    APP_ASSETS_PATH("plugins/grp_g7.fal"),
    APP_ASSETS_PATH("plugins/grp_g8.fal"),
    APP_ASSETS_PATH("plugins/grp_g9.fal"),
    APP_ASSETS_PATH("plugins/grp_g10.fal"),
    APP_ASSETS_PATH("plugins/grp_g11.fal"),
    APP_ASSETS_PATH("plugins/grp_g12.fal"),
    APP_ASSETS_PATH("plugins/grp_g13.fal"),
};

bool subghz_garage_protocol_group_next_enabled(
    const uint8_t* enabled_groups, uint8_t start_index, SubGhzGarageProtocolGroup* out_group) {
    for(uint8_t i = start_index; i < SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT; i++) {
        if(enabled_groups[i]) {
            *out_group = (SubGhzGarageProtocolGroup)i;
            return true;
        }
    }
    return false;
}

const char* const subghz_garage_tx_protocol_paths[SubGhzGarageTxProtocolCount] = {
    APP_ASSETS_PATH("plugins/tx_alutech_at_4n.fal"),
    APP_ASSETS_PATH("plugins/tx_somfy_telis.fal"),
    APP_ASSETS_PATH("plugins/tx_jarolift.fal"),
    APP_ASSETS_PATH("plugins/tx_nice_flo.fal"),
    APP_ASSETS_PATH("plugins/tx_came_twee.fal"),
    APP_ASSETS_PATH("plugins/tx_secplus_v1.fal"),
    APP_ASSETS_PATH("plugins/tx_smc5326.fal"),
    APP_ASSETS_PATH("plugins/tx_dickert_mahs.fal"),
    APP_ASSETS_PATH("plugins/tx_roger.fal"),
    APP_ASSETS_PATH("plugins/tx_keyfinder.fal"),
    APP_ASSETS_PATH("plugins/tx_secplus_v2.fal"),
    APP_ASSETS_PATH("plugins/tx_princeton.fal"),
    APP_ASSETS_PATH("plugins/tx_gangqi.fal"),
    APP_ASSETS_PATH("plugins/tx_hay21.fal"),
    APP_ASSETS_PATH("plugins/tx_dooya.fal"),
    APP_ASSETS_PATH("plugins/tx_holtek.fal"),
    APP_ASSETS_PATH("plugins/tx_came.fal"),
    APP_ASSETS_PATH("plugins/tx_nice_flor_s.fal"),
    APP_ASSETS_PATH("plugins/tx_beninca_arc.fal"),
    APP_ASSETS_PATH("plugins/tx_marantec.fal"),
    APP_ASSETS_PATH("plugins/tx_holtek_ht12x.fal"),
    APP_ASSETS_PATH("plugins/tx_revers_rb2.fal"),
    APP_ASSETS_PATH("plugins/tx_megacode.fal"),
    APP_ASSETS_PATH("plugins/tx_marantec24.fal"),
    APP_ASSETS_PATH("plugins/tx_faac_slh.fal"),
    APP_ASSETS_PATH("plugins/tx_came_atomo.fal"),
    APP_ASSETS_PATH("plugins/tx_chamb_code.fal"),
    APP_ASSETS_PATH("plugins/tx_mastercode.fal"),
    APP_ASSETS_PATH("plugins/tx_linear.fal"),
    APP_ASSETS_PATH("plugins/tx_linear_delta3.fal"),
    APP_ASSETS_PATH("plugins/tx_gate_tx.fal"),
    APP_ASSETS_PATH("plugins/tx_kinggates_stylo_4k.fal"),
    APP_ASSETS_PATH("plugins/tx_somfy_keytis.fal"),
    APP_ASSETS_PATH("plugins/tx_phoenix_v2.fal"),
    APP_ASSETS_PATH("plugins/tx_clemsa.fal"),
    APP_ASSETS_PATH("plugins/tx_ansonic.fal"),
    APP_ASSETS_PATH("plugins/tx_doitrand.fal"),
    APP_ASSETS_PATH("plugins/tx_hormann.fal"),
    APP_ASSETS_PATH("plugins/tx_x10.fal"),
    APP_ASSETS_PATH("plugins/tx_telcoma_edge.fal"),
    APP_ASSETS_PATH("plugins/tx_raw.fal"),
    APP_ASSETS_PATH("plugins/tx_bin_raw.fal"),
    APP_ASSETS_PATH("plugins/tx_keyfinder2.fal"),
    APP_ASSETS_PATH("plugins/tx_doorbell32.fal"),
};

typedef struct {
    const char* name;
    SubGhzGarageTxProtocol tx_protocol;
} SubGhzGarageTxProtocolMapEntry;

static const SubGhzGarageTxProtocolMapEntry subghz_garage_tx_protocol_map[] = {
    {SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME, SubGhzGarageTxProtocolAlutechAt4n},
    {SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME, SubGhzGarageTxProtocolSomfyTelis},
    {SUBGHZ_PROTOCOL_JAROLIFT_NAME, SubGhzGarageTxProtocolJarolift},
    {SUBGHZ_PROTOCOL_NICE_FLO_NAME, SubGhzGarageTxProtocolNiceFlo},
    {SUBGHZ_PROTOCOL_CAME_TWEE_NAME, SubGhzGarageTxProtocolCameTwee},
    {SUBGHZ_PROTOCOL_SECPLUS_V1_NAME, SubGhzGarageTxProtocolSecPlusV1},
    {SUBGHZ_PROTOCOL_SMC5326_NAME, SubGhzGarageTxProtocolSmc5326},
    {SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME, SubGhzGarageTxProtocolDickertMahs},
    {SUBGHZ_PROTOCOL_ROGER_NAME, SubGhzGarageTxProtocolRoger},
    {SUBGHZ_PROTOCOL_KEYFINDER_NAME, SubGhzGarageTxProtocolKeyFinder},
    {SUBGHZ_PROTOCOL_KEYFINDER2_NAME, SubGhzGarageTxProtocolKeyFinder2},
    {SUBGHZ_PROTOCOL_DOORBELL32_NAME, SubGhzGarageTxProtocolDoorbell32},
    {SUBGHZ_PROTOCOL_SECPLUS_V2_NAME, SubGhzGarageTxProtocolSecPlusV2},
    {SUBGHZ_PROTOCOL_PRINCETON_NAME, SubGhzGarageTxProtocolPrinceton},
    {SUBGHZ_PROTOCOL_GANGQI_NAME, SubGhzGarageTxProtocolGangQi},
    {SUBGHZ_PROTOCOL_HAY21_NAME, SubGhzGarageTxProtocolHay21},
    {SUBGHZ_PROTOCOL_DOOYA_NAME, SubGhzGarageTxProtocolDooya},
    {SUBGHZ_PROTOCOL_HOLTEK_NAME, SubGhzGarageTxProtocolHoltek},
    {SUBGHZ_PROTOCOL_CAME_NAME, SubGhzGarageTxProtocolCame},
    {SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME, SubGhzGarageTxProtocolNiceFlorS},
    {SUBGHZ_PROTOCOL_BENINCA_ARC_NAME, SubGhzGarageTxProtocolBenincaArc},
    {SUBGHZ_PROTOCOL_MARANTEC_NAME, SubGhzGarageTxProtocolMarantec},
    {SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME, SubGhzGarageTxProtocolHoltekHt12x},
    {SUBGHZ_PROTOCOL_REVERSRB2_NAME, SubGhzGarageTxProtocolReversRb2},
    {SUBGHZ_PROTOCOL_MEGACODE_NAME, SubGhzGarageTxProtocolMegaCode},
    {SUBGHZ_PROTOCOL_MARANTEC24_NAME, SubGhzGarageTxProtocolMarantec24},
    {SUBGHZ_PROTOCOL_FAAC_SLH_NAME, SubGhzGarageTxProtocolFaacSlh},
    {SUBGHZ_PROTOCOL_CAME_ATOMO_NAME, SubGhzGarageTxProtocolCameAtomo},
    {SUBGHZ_PROTOCOL_CHAMB_CODE_NAME, SubGhzGarageTxProtocolChambCode},
    {SUBGHZ_PROTOCOL_MASTERCODE_NAME, SubGhzGarageTxProtocolMastercode},
    {SUBGHZ_PROTOCOL_LINEAR_NAME, SubGhzGarageTxProtocolLinear},
    {SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME, SubGhzGarageTxProtocolLinearDelta3},
    {SUBGHZ_PROTOCOL_GATE_TX_NAME, SubGhzGarageTxProtocolGateTx},
    {SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME, SubGhzGarageTxProtocolKingGatesStylo4k},
    {SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME, SubGhzGarageTxProtocolSomfyKeytis},
    {SUBGHZ_PROTOCOL_PHOENIX_V2_NAME, SubGhzGarageTxProtocolPhoenixV2},
    {SUBGHZ_PROTOCOL_CLEMSA_NAME, SubGhzGarageTxProtocolClemsa},
    {SUBGHZ_PROTOCOL_ANSONIC_NAME, SubGhzGarageTxProtocolAnsonic},
    {SUBGHZ_PROTOCOL_DOITRAND_NAME, SubGhzGarageTxProtocolDoitrand},
    {SUBGHZ_PROTOCOL_HORMANN_HSM_NAME, SubGhzGarageTxProtocolHormann},
    {SUBGHZ_PROTOCOL_X10_NAME, SubGhzGarageTxProtocolX10},
    {SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME, SubGhzGarageTxProtocolTelcomaEdge},
    {SUBGHZ_PROTOCOL_RAW_NAME, SubGhzGarageTxProtocolRaw},
    {SUBGHZ_PROTOCOL_BIN_RAW_NAME, SubGhzGarageTxProtocolBinRaw},
};

bool subghz_garage_tx_protocol_for_name(
    const char* protocol_name,
    SubGhzGarageTxProtocol* out_tx_protocol) {
    for(size_t i = 0; i < COUNT_OF(subghz_garage_tx_protocol_map); i++) {
        if(strcmp(subghz_garage_tx_protocol_map[i].name, protocol_name) == 0) {
            *out_tx_protocol = subghz_garage_tx_protocol_map[i].tx_protocol;
            return true;
        }
    }
    return false;
}


typedef struct {
    const char* name;
    SubGhzGarageProtocolGroup group;
} SubGhzGarageProtocolGroupMapEntry;

static const SubGhzGarageProtocolGroupMapEntry subghz_garage_protocol_group_map[] = {
    {SUBGHZ_PROTOCOL_SMC5326_NAME, SubGhzGarageProtocolGroup1},
    {SUBGHZ_PROTOCOL_HOLTEK_NAME, SubGhzGarageProtocolGroup1},
    {SUBGHZ_PROTOCOL_HOLTEK_HT12X_NAME, SubGhzGarageProtocolGroup1},
    {SUBGHZ_PROTOCOL_PRINCETON_NAME, SubGhzGarageProtocolGroup1},
    {SUBGHZ_PROTOCOL_X10_NAME, SubGhzGarageProtocolGroup2},
    {SUBGHZ_PROTOCOL_KEYFINDER_NAME, SubGhzGarageProtocolGroup2},
    {SUBGHZ_PROTOCOL_KEYFINDER2_NAME, SubGhzGarageProtocolGroup2},
    {SUBGHZ_PROTOCOL_KEELOQ_NAME, SubGhzGarageProtocolGroup2},
    {SUBGHZ_PROTOCOL_CHAMB_CODE_NAME, SubGhzGarageProtocolGroup3},
    {SUBGHZ_PROTOCOL_SECPLUS_V1_NAME, SubGhzGarageProtocolGroup3},
    {SUBGHZ_PROTOCOL_SECPLUS_V2_NAME, SubGhzGarageProtocolGroup3},
    {SUBGHZ_PROTOCOL_LINEAR_NAME, SubGhzGarageProtocolGroup4},
    {SUBGHZ_PROTOCOL_LINEAR_DELTA3_NAME, SubGhzGarageProtocolGroup4},
    {SUBGHZ_PROTOCOL_MEGACODE_NAME, SubGhzGarageProtocolGroup4},
    {SUBGHZ_PROTOCOL_BENINCA_ARC_NAME, SubGhzGarageProtocolGroup5},
    {SUBGHZ_PROTOCOL_CAME_NAME, SubGhzGarageProtocolGroup5},
    {SUBGHZ_PROTOCOL_CAME_ATOMO_NAME, SubGhzGarageProtocolGroup5},
    {SUBGHZ_PROTOCOL_CAME_TWEE_NAME, SubGhzGarageProtocolGroup5},
    {SUBGHZ_PROTOCOL_KINGGATES_STYLO_4K_NAME, SubGhzGarageProtocolGroup6},
    {SUBGHZ_PROTOCOL_NICE_FLO_NAME, SubGhzGarageProtocolGroup6},
    {SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME, SubGhzGarageProtocolGroup6},
    {SUBGHZ_PROTOCOL_PHOENIX_V2_NAME, SubGhzGarageProtocolGroup6},
    {SUBGHZ_PROTOCOL_FAAC_SLH_NAME, SubGhzGarageProtocolGroup7},
    {SUBGHZ_PROTOCOL_ROGER_NAME, SubGhzGarageProtocolGroup7},
    {SUBGHZ_PROTOCOL_DOITRAND_NAME, SubGhzGarageProtocolGroup7},
    {SUBGHZ_PROTOCOL_TELCOMA_EDGE_NAME, SubGhzGarageProtocolGroup7},
    {SUBGHZ_PROTOCOL_ALUTECH_AT_4N_NAME, SubGhzGarageProtocolGroup8},
    {SUBGHZ_PROTOCOL_ANSONIC_NAME, SubGhzGarageProtocolGroup8},
    {SUBGHZ_PROTOCOL_CLEMSA_NAME, SubGhzGarageProtocolGroup8},
    {SUBGHZ_PROTOCOL_MASTERCODE_NAME, SubGhzGarageProtocolGroup8},
    {SUBGHZ_PROTOCOL_DICKERT_MAHS_NAME, SubGhzGarageProtocolGroup9},
    {SUBGHZ_PROTOCOL_HORMANN_HSM_NAME, SubGhzGarageProtocolGroup9},
    {SUBGHZ_PROTOCOL_MARANTEC_NAME, SubGhzGarageProtocolGroup9},
    {SUBGHZ_PROTOCOL_MARANTEC24_NAME, SubGhzGarageProtocolGroup9},
    {SUBGHZ_PROTOCOL_GANGQI_NAME, SubGhzGarageProtocolGroup10},
    {SUBGHZ_PROTOCOL_GATE_TX_NAME, SubGhzGarageProtocolGroup10},
    {SUBGHZ_PROTOCOL_HAY21_NAME, SubGhzGarageProtocolGroup10},
    {SUBGHZ_PROTOCOL_REVERSRB2_NAME, SubGhzGarageProtocolGroup10},
    {SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME, SubGhzGarageProtocolGroup11},
    {SUBGHZ_PROTOCOL_SOMFY_KEYTIS_NAME, SubGhzGarageProtocolGroup11},
    {SUBGHZ_PROTOCOL_JAROLIFT_NAME, SubGhzGarageProtocolGroup11},
    {SUBGHZ_PROTOCOL_DOOYA_NAME, SubGhzGarageProtocolGroup11},
    {SUBGHZ_PROTOCOL_NORD_ICE_NAME, SubGhzGarageProtocolGroup12},
    {SUBGHZ_PROTOCOL_ALLSTAR_FIREFLY_NAME, SubGhzGarageProtocolGroup12},
    {SUBGHZ_PROTOCOL_PRASTEL_NAME, SubGhzGarageProtocolGroup12},
    {SUBGHZ_PROTOCOL_DITEC_GOL4_NAME, SubGhzGarageProtocolGroup13},
    {SUBGHZ_PROTOCOL_DOORBELL32_NAME, SubGhzGarageProtocolGroup13},
};

bool subghz_garage_protocol_group_for_name(
    const char* protocol_name,
    SubGhzGarageProtocolGroup* out_group) {
    if(!protocol_name || !out_group) {
        return false;
    }
    for(size_t i = 0; i < COUNT_OF(subghz_garage_protocol_group_map); i++) {
        if(strcmp(subghz_garage_protocol_group_map[i].name, protocol_name) == 0) {
            *out_group = subghz_garage_protocol_group_map[i].group;
            return true;
        }
    }
    return false;
}
