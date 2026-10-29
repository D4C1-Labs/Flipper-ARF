#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g3_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_chamb_code,
    &subghz_protocol_secplus_v1,
    &subghz_protocol_secplus_v2,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g3 = {
    .items = subghz_garage_protocol_registry_g3_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g3_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g3 = {
    .registry = &subghz_garage_protocol_registry_g3,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g3_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g3,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g3_ep(void) {
    return &subghz_garage_protocol_plugin_g3_descriptor;
}
