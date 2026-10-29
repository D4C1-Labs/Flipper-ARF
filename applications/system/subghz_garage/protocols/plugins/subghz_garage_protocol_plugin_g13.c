#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g13_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_ditec_gol4,
    &subghz_protocol_doorbell32,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g13 = {
    .items = subghz_garage_protocol_registry_g13_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g13_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g13 = {
    .registry = &subghz_garage_protocol_registry_g13,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g13_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g13,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g13_ep(void) {
    return &subghz_garage_protocol_plugin_g13_descriptor;
}
