#include "../subghz_garage_protocol_plugin.h"

static const SubGhzProtocol* const subghz_garage_tx_came_atomo_registry_items[] = {
    &subghz_protocol_came_atomo,
};

static const SubGhzProtocolRegistry subghz_garage_tx_came_atomo_registry = {
    .items = subghz_garage_tx_came_atomo_registry_items,
    .size = COUNT_OF(subghz_garage_tx_came_atomo_registry_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_tx_came_atomo_plugin = {
    .registry = &subghz_garage_tx_came_atomo_registry,
};

static const FlipperAppPluginDescriptor subghz_garage_tx_came_atomo_plugin_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_tx_came_atomo_plugin,
};

const FlipperAppPluginDescriptor* subghz_garage_tx_came_atomo_plugin_ep(void) {
    return &subghz_garage_tx_came_atomo_plugin_descriptor;
}
