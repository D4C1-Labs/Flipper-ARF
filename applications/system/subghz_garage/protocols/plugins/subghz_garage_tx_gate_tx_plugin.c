#include "../subghz_garage_protocol_plugin.h"

static const SubGhzProtocol* const subghz_garage_tx_gate_tx_registry_items[] = {
    &subghz_protocol_gate_tx,
};

static const SubGhzProtocolRegistry subghz_garage_tx_gate_tx_registry = {
    .items = subghz_garage_tx_gate_tx_registry_items,
    .size = COUNT_OF(subghz_garage_tx_gate_tx_registry_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_tx_gate_tx_plugin = {
    .registry = &subghz_garage_tx_gate_tx_registry,
};

static const FlipperAppPluginDescriptor subghz_garage_tx_gate_tx_plugin_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_tx_gate_tx_plugin,
};

const FlipperAppPluginDescriptor* subghz_garage_tx_gate_tx_plugin_ep(void) {
    return &subghz_garage_tx_gate_tx_plugin_descriptor;
}
