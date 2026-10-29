#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

// Group 12 (new 2026-10-03): small Unleashed gate decoders split out of g7 to
// keep each group's .fal within the ~20 KB size envelope (load-time RAM cost).
// RX-only in garage (encoders compiled out under SUBGHZ_GARAGE_WITH_ENCODER).
static const SubGhzProtocol* const subghz_garage_protocol_registry_g12_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_nord_ice,
    &subghz_protocol_allstar_firefly,
    &subghz_protocol_prastel,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g12 = {
    .items = subghz_garage_protocol_registry_g12_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g12_items)};

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g12 = {
    .registry = &subghz_garage_protocol_registry_g12,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g12_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g12,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g12_ep(void) {
    return &subghz_garage_protocol_plugin_g12_descriptor;
}
