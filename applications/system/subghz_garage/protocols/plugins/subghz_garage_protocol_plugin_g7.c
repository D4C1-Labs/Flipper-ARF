#include "../subghz_garage_protocol_plugin.h"
#include "../protocol_groups.h"

static const SubGhzProtocol* const subghz_garage_protocol_registry_g7_items[] = {
    &subghz_protocol_raw,
    &subghz_protocol_bin_raw,
    &subghz_protocol_faac_slh,
    &subghz_protocol_roger,
    &subghz_protocol_doitrand,
    &subghz_protocol_telcoma_edge,
};

const SubGhzProtocolRegistry subghz_garage_protocol_registry_g7 = {
    .items = subghz_garage_protocol_registry_g7_items,
    .size = COUNT_OF(subghz_garage_protocol_registry_g7_items)};

static void subghz_garage_faac_slh_reset_prog_mode(void) {
    faac_slh_reset_prog_mode();
}

static const SubGhzGarageProtocolPlugin subghz_garage_protocol_plugin_g7 = {
    .registry = &subghz_garage_protocol_registry_g7,
    .faac_slh_reset_prog_mode = subghz_garage_faac_slh_reset_prog_mode,
};

static const FlipperAppPluginDescriptor subghz_garage_protocol_plugin_g7_descriptor = {
    .appid = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
    .ep_api_version = SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
    .entry_point = &subghz_garage_protocol_plugin_g7,
};

const FlipperAppPluginDescriptor* subghz_garage_protocol_plugin_g7_ep(void) {
    return &subghz_garage_protocol_plugin_g7_descriptor;
}
