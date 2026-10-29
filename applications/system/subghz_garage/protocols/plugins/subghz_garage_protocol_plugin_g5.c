#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g5_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_beninca_arc,
    &subghz_protocol_came,
    &subghz_protocol_came_atomo,
    &subghz_protocol_came_twee,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g5 = {
    .items = subghz_garage_protocol_registry_g5_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g5_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g5 = {
    .registry = &subghz_garage_protocol_registry_g5,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g5_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g5,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g5_ep(void) {
    return &subghz_garage_protocol_plugin_g5_descriptor;
}
