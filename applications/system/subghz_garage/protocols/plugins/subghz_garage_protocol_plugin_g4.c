#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g4_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_linear,
    &subghz_protocol_linear_delta3,
    &subghz_protocol_megacode,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g4 = {
    .items = subghz_garage_protocol_registry_g4_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g4_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g4 = {
    .registry = &subghz_garage_protocol_registry_g4,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g4_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g4,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g4_ep(void) {
    return &subghz_garage_protocol_plugin_g4_descriptor;
}
