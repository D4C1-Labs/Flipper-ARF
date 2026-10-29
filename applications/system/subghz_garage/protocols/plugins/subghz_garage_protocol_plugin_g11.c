#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g11_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_somfy_telis,
    &subghz_protocol_somfy_keytis,
    &subghz_protocol_jarolift,
    &subghz_protocol_dooya,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g11 = {
    .items = subghz_garage_protocol_registry_g11_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g11_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g11 = {
    .registry = &subghz_garage_protocol_registry_g11,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g11_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g11,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g11_ep(void) {
    return &subghz_garage_protocol_plugin_g11_descriptor;
}
