#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g2_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_x10,
    &subghz_protocol_keyfinder,
    &subghz_protocol_keyfinder2,
    &subghz_protocol_keeloq,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g2 = {
    .items = subghz_garage_protocol_registry_g2_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g2_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g2 = {
    .registry = &subghz_garage_protocol_registry_g2,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g2_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g2,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g2_ep(void) {
    return &subghz_garage_protocol_plugin_g2_descriptor;
}
