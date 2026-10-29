#include "../subghz_garage_protocol_plugin.h"

static const SubGhzProtocol* const subghz_garage_tx_keyfinder_registry_items[] = {
    &subghz_protocol_keyfinder,
};

static const SubGhzProtocolRegistry subghz_garage_tx_keyfinder_registry = {
    .items = subghz_garage_tx_keyfinder_registry_items,
    .size = COUNT_OF(subghz_garage_tx_keyfinder_registry_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_tx_keyfinder_plugin = {
    .registry = &subghz_garage_tx_keyfinder_registry,
};

static const FlipperAppPluginDescriptor subghz_garage_tx_keyfinder_plugin_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_tx_keyfinder_plugin,
};

const FlipperAppPluginDescriptor* subghz_garage_tx_keyfinder_plugin_ep(void) {
    return &subghz_garage_tx_keyfinder_plugin_descriptor;
}
