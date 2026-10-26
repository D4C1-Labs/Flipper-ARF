#pragma once
/*
 * ARF-Garage — garage door / gate / remote protocol catalog.
 *
 * This app carries its OWN copies of the protocol sources (under protocols/)
 * because individual `subghz_protocol_*` symbols are NOT exported to FAPs via
 * targets/f7/api_symbols.csv — only the generic lib/subghz blocks API is.
 *
 * To add a protocol:
 *   1. Copy <name>.c/.h from lib/subghz/protocols/ into this folder.
 *   2. Fix its includes: "../blocks/X.h"             -> <lib/subghz/blocks/X.h>
 *                        "../blocks/custom_btn_i.h"  -> "custom_btn_i.h"
 *                        "../subghz_keystore_i.h"    -> "subghz_keystore_i.h"
 *                        "base.h"                    -> <lib/subghz/protocols/base.h>
 *   3. Add its #include below and an entry in protocol_items.c.
 *   4. Add the .c to `sources` in application.fam.
 */

#include <lib/subghz/registry.h>
#include <lib/subghz/subghz_protocol_registry.h>

#include "raw.h"
#include "bin_raw.h"
#include "gate_tx.h"
#include "keeloq.h"
#include "nice_flo.h"
#include "nice_flor_s.h"
#include "came.h"
#include "came_twee.h"
#include "came_atomo.h"
#include "faac_slh.h"
#include "hormann.h"
#include "somfy_telis.h"
#include "somfy_keytis.h"
#include "princeton.h"
#include "linear.h"
#include "linear_delta3.h"
#include "secplus_v1.h"
#include "secplus_v2.h"
#include "megacode.h"
#include "holtek.h"
#include "holtek_ht12x.h"
#include "chamberlain_code.h"
#include "marantec.h"
#include "marantec24.h"
#include "doitrand.h"
#include "phoenix_v2.h"
#include "clemsa.h"
#include "ansonic.h"
#include "smc5326.h"
#include "dooya.h"
#include "alutech_at_4n.h"
#include "kinggates_stylo_4k.h"
#include "mastercode.h"
#include "dickert_mahs.h"
#include "gangqi.h"
#include "hay21.h"
#include "revers_rb2.h"
#include "roger.h"
#include "telcoma_edge.h"
#include "beninca_arc.h"
#include "keyfinder.h"
#include "nord_ice.h"
#include "allstar_firefly.h"
#include "ditec_gol4.h"
#include "bett.h"
#include "power_smart.h"
#include "nero_sketch.h"
#include "nero_radio.h"
#include "ido.h"
#include "intertechno_v3.h"
#include "magellan.h"
#include "honeywell.h"
#include "honeywell_wdb.h"
#include "legrand.h"
#include "hollarm.h"
#include "feron.h"
#include "elplast.h"
#include "treadmill37.h"
#include "jarolift.h"

/* [ARF_PORT 2026-10-26] Imported from doctor-firmware / FoxFW. */
#include "urmet.h"
#include "x10.h"

/* Registry consumed by subghz_environment_set_protocol_registry(). */
extern const SubGhzProtocolRegistry arf_garage_protocol_registry;

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

