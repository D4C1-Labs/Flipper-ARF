#include "../subghz_garage_protocol_plugin.h"

static const SubGhzProtocol* const subghz_garage_tx_megacode_registry_items[] = {
    &subghz_protocol_megacode,
};

static const SubGhzProtocolRegistry subghz_garage_tx_megacode_registry = {
    .items = subghz_garage_tx_megacode_registry_items,
    .size = COUNT_OF(subghz_garage_tx_megacode_registry_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_tx_megacode_plugin = {
    .registry = &subghz_garage_tx_megacode_registry,
};

static const FlipperAppPluginDescriptor subghz_garage_tx_megacode_plugin_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_tx_megacode_plugin,
};

const FlipperAppPluginDescriptor* subghz_garage_tx_megacode_plugin_ep(void) {
    return &subghz_garage_tx_megacode_plugin_descriptor;
}
