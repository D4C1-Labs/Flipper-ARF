#pragma once
/*
 * ARF-Cars — automotive protocol catalog.
 *
 * This app carries its OWN copies of the protocol sources (under protocols/)
 * because individual `subghz_protocol_*` symbols are NOT exported to FAPs via
 * targets/f7/api_symbols.csv — only the generic lib/subghz blocks API is.
 *
 * To add a protocol:
 *   1. Copy <name>.c/.h from lib/subghz/protocols/ into this folder.
 *   2. Fix its includes: "../blocks/X.h"        -> <lib/subghz/blocks/X.h>
 *                        "../blocks/custom_btn_i.h" -> "custom_btn_i.h"
 *                        "../subghz_keystore_i.h"   -> "subghz_keystore_i.h"
 *                        "../types.h"           -> <lib/subghz/types.h>
 *   3. Add its #include below and an entry in protocol_items.c.
 *   4. Add the .c to `sources` in application.fam.
 */

#include <lib/subghz/registry.h>
#include <lib/subghz/subghz_protocol_registry.h>

#include "raw.h"
#include "bin_raw.h"

#include "vag.h"
#include "ford_v0.h"
#include "ford_v1.h"
#include "ford_v2.h"
#include "ford_v3.h"
#include "psa.h"
#include "fiat_v0.h"
#include "fiat_v1.h"
#include "fiat_v2.h"
#include "renault_v0.h"
#include "renault_v1.h"
#include "subaru.h"
#include "kia_v0.h"
#include "kia_v1.h"
#include "kia_v2.h"
#include "kia_v3_v4.h"
#include "kia_v5.h"
#include "kia_v6.h"
#include "kia_v7.h"
#include "suzuki.h"
#include "mitsubishi_v0.h"
#include "star_line.h"
#include "scher_khan.h"
#include "sheriff_cfm.h"
#include "chrysler.h"
#include "mazda_v0.h"
#include "honda_static.h"
#include "honda_v1.h"
#include "honda_v2.h"

/* [ARF_PORT 2026-10-26] Imported from doctor-firmware / FoxFW. */
#include "gm_v0.h"
#include "gm_v1.h"
#include "toyota_v0.h"
#include "toyota_v1.h"
#include "mazda_v1.h"
#include "mazda_v2.h"
#include "mercedes_v0.h"
#include "mercedes_v1.h"
#include "opel_v1.h"
#include "bmw.h"

/* Registry consumed by subghz_environment_set_protocol_registry(). */
extern const SubGhzProtocolRegistry arf_cars_protocol_registry;

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

