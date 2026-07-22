#pragma once
#include "../registry.h"
#include "../subghz_protocol_registry.h"

#include "princeton.h"
#include "keeloq.h"
#include "nice_flo.h"
#include "came.h"
#include "faac_slh.h"
#include "nice_flor_s.h"
#include "came_twee.h"
#include "came_atomo.h"
#include "nero_sketch.h"
#include "ido.h"
#include "hormann.h"
#include "nero_radio.h"
#include "somfy_telis.h"
#include "somfy_keytis.h"
#include "gate_tx.h"
#include "raw.h"
#include "linear.h"
#include "linear_delta3.h"
#include "secplus_v2.h"
#include "secplus_v1.h"
#include "megacode.h"
#include "holtek.h"
#include "chamberlain_code.h"
#include "power_smart.h"
#include "marantec.h"
#include "bett.h"
#include "doitrand.h"
#include "phoenix_v2.h"
#include "honeywell_wdb.h"
#include "magellan.h"
#include "intertechno_v3.h"
#include "clemsa.h"
#include "ansonic.h"
#include "smc5326.h"
#include "holtek_ht12x.h"
#include "dooya.h"
#include "alutech_at_4n.h"
#include "kinggates_stylo_4k.h"
#include "bin_raw.h"
#include "mastercode.h"
#include "honeywell.h"
#include "legrand.h"
#include "dickert_mahs.h"
#include "gangqi.h"
#include "marantec24.h"
#include "hollarm.h"
#include "hay21.h"
#include "revers_rb2.h"
#include "feron.h"
#include "roger.h"
#include "elplast.h"
#include "treadmill37.h"
#include "beninca_arc.h"
#include "keyfinder.h"
#include "jarolift.h"
#include "vag.h"
#include "porsche_cayenne.h"
#include "ford_v0.h"
#include "psa.h"
#include "fiat_spa.h"
#include "fiat_marelli.h"
#include "fiat_v0.h"
#include "fiat_v1.h"
#include "fiat_v2.h"
#include "renault_v0.h"
#include "bmw_cas4.h"
#include "subaru.h"
#include "kia_generic.h"
#include "kia_v0.h"
#include "kia_v1.h"
#include "kia_v2.h"
#include "kia_v3_v4.h"
#include "kia_v5.h"
#include "kia_v6.h"
#include "suzuki.h"
#include "mitsubishi_v0.h"
#include "mazda_siemens.h"
#include "star_line.h"
#include "scher_khan.h"
#include "sheriff_cfm.h"
#include "chrysler.h"
#include "mazda_v0.h"
#include "kia_v7.h"
#include "ford_v1.h"
#include "ford_v2.h"
#include "ford_v3.h"
#include "land_rover_v0.h"
#include "toyota.h"
#include "honda_static.h"
#include "honda_v1.h"
#include "honda_v2.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SubGhzProtocolCatalogRouteAMDefault = 0,
    SubGhzProtocolCatalogRouteAMVag,
    SubGhzProtocolCatalogRouteFMDefault,
    SubGhzProtocolCatalogRouteFMF4,
    SubGhzProtocolCatalogRouteFMHonda1,
} SubGhzProtocolCatalogRoute;

typedef enum {
    SubGhzProtocolCatalogRoutePolicyAMDefault = 0,
    SubGhzProtocolCatalogRoutePolicyAMVag,
    SubGhzProtocolCatalogRoutePolicyFMDefault,
    SubGhzProtocolCatalogRoutePolicyFMF4,
    SubGhzProtocolCatalogRoutePolicyFMHonda1,
    SubGhzProtocolCatalogRoutePolicyByModulation,
} SubGhzProtocolCatalogRoutePolicy;

typedef struct {
    const char* canonical_name;
    SubGhzProtocolCatalogRoutePolicy route_policy;
    const char* tx_key;
} SubGhzProtocolCatalogEntry;

const SubGhzProtocolCatalogEntry*
    subghz_protocol_catalog_find(const char* protocol_name);
const char* subghz_protocol_catalog_canonical_name(const char* protocol_name);
bool subghz_protocol_catalog_can_tx(const char* protocol_name);
const char* subghz_protocol_catalog_tx_key(const char* protocol_name);
const char* subghz_protocol_catalog_display_name(const char* protocol_name, uint32_t protocol_type);
SubGhzProtocolCatalogRoute subghz_protocol_catalog_get_route(
    const char* preset_name,
    uint32_t frequency,
    const uint8_t* preset_data,
    size_t preset_data_size,
    const char* protocol_name);
const char* subghz_protocol_catalog_get_route_name(SubGhzProtocolCatalogRoute route);
const char* subghz_protocol_catalog_route_to_preset_name(SubGhzProtocolCatalogRoute route);

