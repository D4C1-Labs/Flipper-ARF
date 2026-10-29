#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g8_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_alutech_at_4n,
    &subghz_protocol_ansonic,
    &subghz_protocol_clemsa,
    &subghz_protocol_mastercode,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g8 = {
    .items = subghz_garage_protocol_registry_g8_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g8_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g8 = {
    .registry = &subghz_garage_protocol_registry_g8,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g8_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g8,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g8_ep(void) {
    return &subghz_garage_protocol_plugin_g8_descriptor;
}
