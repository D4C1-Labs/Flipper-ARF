#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g9_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_dickert_mahs,
    &subghz_protocol_hormann,
    &subghz_protocol_marantec,
    &subghz_protocol_marantec24,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g9 = {
    .items = subghz_garage_protocol_registry_g9_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g9_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g9 = {
    .registry = &subghz_garage_protocol_registry_g9,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g9_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g9,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g9_ep(void) {
    return &subghz_garage_protocol_plugin_g9_descriptor;
}
