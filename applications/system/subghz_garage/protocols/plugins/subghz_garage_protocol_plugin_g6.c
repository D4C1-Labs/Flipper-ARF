#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g6_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_kinggates_stylo_4k,
    &subghz_protocol_nice_flo,
    &subghz_protocol_nice_flor_s,
    &subghz_protocol_phoenix_v2,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g6 = {
    .items = subghz_garage_protocol_registry_g6_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g6_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g6 = {
    .registry = &subghz_garage_protocol_registry_g6,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g6_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g6,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g6_ep(void) {
    return &subghz_garage_protocol_plugin_g6_descriptor;
}
