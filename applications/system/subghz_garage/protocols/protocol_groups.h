#pragma once
#include <lib/subghz/registry.h>

#define SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT 13

typedef enum {
    SubGhzGarageProtocolGroup1 = 0,
    SubGhzGarageProtocolGroup2 = 1,
    SubGhzGarageProtocolGroup3 = 2,
    SubGhzGarageProtocolGroup4 = 3,
    SubGhzGarageProtocolGroup5 = 4,
    SubGhzGarageProtocolGroup6 = 5,
    SubGhzGarageProtocolGroup7 = 6,
    SubGhzGarageProtocolGroup8 = 7,
    SubGhzGarageProtocolGroup9 = 8,
    SubGhzGarageProtocolGroup10 = 9,
    SubGhzGarageProtocolGroup11 = 10,
    SubGhzGarageProtocolGroup12 = 11,
    SubGhzGarageProtocolGroup13 = 12,
} SubGhzGarageProtocolGroup;

extern const char* const subghz_garage_protocol_group_paths[SUBGHZ_GARAGE_PROTOCOL_GROUP_COUNT];

bool subghz_garage_protocol_group_next_enabled(
    const uint8_t* enabled_groups,
    uint8_t start_index,
    SubGhzGarageProtocolGroup* out_group);

bool subghz_garage_protocol_group_for_name(
    const char* protocol_name,
    SubGhzGarageProtocolGroup* out_group);

extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g1;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g2;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g3;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g4;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g5;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g6;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g7;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g8;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g9;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g10;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g11;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g12;
extern const SubGhzProtocolRegistry subghz_garage_protocol_registry_g13;

typedef enum {
    SubGhzGarageTxProtocolAlutechAt4n = 0,
    SubGhzGarageTxProtocolSomfyTelis,
    SubGhzGarageTxProtocolJarolift,
    SubGhzGarageTxProtocolNiceFlo,
    SubGhzGarageTxProtocolCameTwee,
    SubGhzGarageTxProtocolSecPlusV1,
    SubGhzGarageTxProtocolSmc5326,
    SubGhzGarageTxProtocolDickertMahs,
    SubGhzGarageTxProtocolRoger,
    SubGhzGarageTxProtocolKeyFinder,
    SubGhzGarageTxProtocolSecPlusV2,
    SubGhzGarageTxProtocolPrinceton,
    SubGhzGarageTxProtocolGangQi,
    SubGhzGarageTxProtocolHay21,
    SubGhzGarageTxProtocolDooya,
    SubGhzGarageTxProtocolHoltek,
    SubGhzGarageTxProtocolCame,
    SubGhzGarageTxProtocolNiceFlorS,
    SubGhzGarageTxProtocolBenincaArc,
    SubGhzGarageTxProtocolMarantec,
    SubGhzGarageTxProtocolHoltekHt12x,
    SubGhzGarageTxProtocolReversRb2,
    SubGhzGarageTxProtocolMegaCode,
    SubGhzGarageTxProtocolMarantec24,
    SubGhzGarageTxProtocolFaacSlh,
    SubGhzGarageTxProtocolCameAtomo,
    SubGhzGarageTxProtocolChambCode,
    SubGhzGarageTxProtocolMastercode,
    SubGhzGarageTxProtocolLinear,
    SubGhzGarageTxProtocolLinearDelta3,
    SubGhzGarageTxProtocolGateTx,
    SubGhzGarageTxProtocolKingGatesStylo4k,
    SubGhzGarageTxProtocolSomfyKeytis,
    SubGhzGarageTxProtocolPhoenixV2,
    SubGhzGarageTxProtocolClemsa,
    SubGhzGarageTxProtocolAnsonic,
    SubGhzGarageTxProtocolDoitrand,
    SubGhzGarageTxProtocolHormann,
    SubGhzGarageTxProtocolX10,
    SubGhzGarageTxProtocolTelcomaEdge,

    SubGhzGarageTxProtocolRaw,
    SubGhzGarageTxProtocolBinRaw,
    SubGhzGarageTxProtocolKeyFinder2,
    SubGhzGarageTxProtocolDoorbell32,
    SubGhzGarageTxProtocolCount,
} SubGhzGarageTxProtocol;

extern const char* const subghz_garage_tx_protocol_paths[SubGhzGarageTxProtocolCount];

bool subghz_garage_tx_protocol_for_name(
    const char* protocol_name,
    SubGhzGarageTxProtocol* out_tx_protocol);
