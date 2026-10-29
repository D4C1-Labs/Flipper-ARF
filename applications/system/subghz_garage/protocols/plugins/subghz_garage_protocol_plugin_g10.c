#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g10_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_gangqi,
    &subghz_protocol_gate_tx,
    &subghz_protocol_hay21,
    &subghz_protocol_revers_rb2,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g10 = {
    .items = subghz_garage_protocol_registry_g10_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g10_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g10 = {
    .registry = &subghz_garage_protocol_registry_g10,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g10_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g10,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g10_ep(void) {
    return &subghz_garage_protocol_plugin_g10_descriptor;
}
