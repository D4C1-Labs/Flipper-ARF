#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g1_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_smc5326,
    &subghz_protocol_holtek,
    &subghz_protocol_holtek_th12x,
    &subghz_protocol_princeton,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g1 = {
    .items = subghz_garage_protocol_registry_g1_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g1_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g1 = {
    .registry = &subghz_garage_protocol_registry_g1,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g1_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g1,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g1_ep(void) {
    return &subghz_garage_protocol_plugin_g1_descriptor;
}
