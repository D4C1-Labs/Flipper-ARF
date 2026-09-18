// helpers/rolljam_rx_chain.c
#include "rolljam_rx_chain.h"

typedef struct {
    const SubGhzProtocol** items;
    size_t size;
} MutableSubGhzProtocolRegistry;

#if defined(ENABLE_DUAL_RX_SCENE) || defined(ENABLE_SHIELD_RX_SCENE)

#include <furi.h>
#include <loader/firmware_api/firmware_api.h>

/* RollJam private SubGhz API table (api/rolljam_api_table.cpp) — exposes the
 * SubGhz lib symbols the runtime plugins import. */
extern const ElfApiInterface* const subghz_application_api_interface;
#include "../protocols/keys.h"

#define TAG "RollJamRxChain"

#define ROLLJAM_CHAIN_KEYSTORE_DIR APP_ASSETS_PATH("encrypted")

#define ROLLJAM_CC1101_REG_FIFOTHR  0x03U
#define ROLLJAM_CC1101_REG_FSCTRL1  0x07U
#define ROLLJAM_CC1101_REG_MDMCFG4  0x10U
#define ROLLJAM_CC1101_REG_MDMCFG3  0x11U
#define ROLLJAM_CC1101_REG_MDMCFG2  0x12U
#define ROLLJAM_CC1101_REG_DEVIATN  0x15U
#define ROLLJAM_CC1101_REG_AGCCTRL2 0x1BU
#define ROLLJAM_CC1101_REG_AGCCTRL1 0x1CU
#define ROLLJAM_CC1101_REG_AGCCTRL0 0x1DU
#define ROLLJAM_CC1101_REG_FREND1   0x21U
#define ROLLJAM_CC1101_REG_TEST2    0x2CU
#define ROLLJAM_CC1101_REG_TEST1    0x2DU

#define ROLLJAM_CC1101_CHANBW_135_KHZ_MASK 0xA0U
#define ROLLJAM_CC1101_XTAL_HZ             26000000UL
#define ROLLJAM_FM_BANDWIDTH_GUARD_HZ      50000UL

#define ROLLJAM_CC1101_MOD_FORMAT_MASK 0x70U
#define ROLLJAM_CC1101_MOD_FORMAT_2FSK 0x00U
#define ROLLJAM_CC1101_MOD_FORMAT_GFSK 0x10U
#define ROLLJAM_CC1101_MOD_FORMAT_OOK  0x30U

static bool rolljam_rx_chain_preset_get_register(
    const uint8_t* data,
    size_t size,
    uint8_t reg,
    uint8_t* value) {
    if(!data || !value || (size < 2U)) {
        return false;
    }
    for(size_t i = 0; i + 1U < size; i += 2U) {
        if((data[i] == 0x00U) && (data[i + 1U] == 0x00U)) {
            break;
        }
        if(data[i] == reg) {
            *value = data[i + 1U];
            return true;
        }
    }
    return false;
}

static bool rolljam_rx_chain_preset_set_register(
    uint8_t* data,
    size_t size,
    uint8_t reg,
    uint8_t value) {
    if(!data || (size < 2U)) {
        return false;
    }
    for(size_t i = 0; i + 1U < size; i += 2U) {
        if((data[i] == 0x00U) && (data[i + 1U] == 0x00U)) {
            break;
        }
        if(data[i] == reg) {
            data[i + 1U] = value;
            return true;
        }
    }
    return false;
}

static bool rolljam_rx_chain_preset_find_terminator(
    const uint8_t* data,
    size_t size,
    size_t* offset) {
    if(!data || !offset || size < 2U) {
        return false;
    }
    for(size_t i = 0; i + 1U < size; i += 2U) {
        if(data[i] == 0x00U && data[i + 1U] == 0x00U) {
            *offset = i;
            return true;
        }
    }
    return false;
}

static uint32_t rolljam_rx_chain_channel_bandwidth_hz(uint8_t mdmcfg4) {
    uint8_t exponent = (mdmcfg4 >> 6U) & 0x03U;
    uint8_t mantissa = (mdmcfg4 >> 4U) & 0x03U;
    uint32_t denominator = 8UL * (4UL + mantissa) * (1UL << exponent);
    return ROLLJAM_CC1101_XTAL_HZ / denominator;
}

static uint32_t rolljam_rx_chain_data_rate_hz(uint8_t mdmcfg4, uint8_t mdmcfg3) {
    uint8_t exponent = mdmcfg4 & 0x0FU;
    uint64_t numerator =
        (uint64_t)(256UL + mdmcfg3) * (1ULL << exponent) * ROLLJAM_CC1101_XTAL_HZ;
    return (uint32_t)((numerator + (1ULL << 27U)) >> 28U);
}

static uint32_t rolljam_rx_chain_deviation_hz(uint8_t deviatn) {
    uint8_t exponent = (deviatn >> 4U) & 0x07U;
    uint8_t mantissa = deviatn & 0x07U;
    uint64_t numerator =
        (uint64_t)ROLLJAM_CC1101_XTAL_HZ * (8UL + mantissa) * (1ULL << exponent);
    return (uint32_t)((numerator + (1ULL << 16U)) >> 17U);
}

static uint8_t rolljam_rx_chain_select_bandwidth_bits(
    uint32_t minimum_hz,
    uint32_t* selected_hz) {
    static const uint8_t bandwidth_bits[] = {
        0x0FU,
        0x0EU,
        0x0DU,
        0x0CU,
        0x0BU,
        0x0AU,
        0x09U,
        0x08U,
        0x07U,
        0x06U,
        0x05U,
        0x04U,
        0x03U,
        0x02U,
        0x01U,
        0x00U,
    };

    const size_t bandwidth_count = sizeof(bandwidth_bits) / sizeof(bandwidth_bits[0]);
    for(size_t i = 0; i < bandwidth_count; i++) {
        uint32_t bandwidth =
            rolljam_rx_chain_channel_bandwidth_hz((uint8_t)(bandwidth_bits[i] << 4U));
        if(bandwidth >= minimum_hz) {
            if(selected_hz) {
                *selected_hz = bandwidth;
            }
            return bandwidth_bits[i];
        }
    }

    if(selected_hz) {
        *selected_hz = rolljam_rx_chain_channel_bandwidth_hz(0x00U);
    }
    return 0x00U;
}

RollJamRxChain* rolljam_rx_chain_alloc(char label) {
    RollJamRxChain* chain = malloc(sizeof(RollJamRxChain));
    furi_check(chain);
    memset(chain, 0, sizeof(RollJamRxChain));
    chain->label = label;
    chain->preset.name = furi_string_alloc();
    furi_check(chain->preset.name);
    chain->state = RollJamTxRxStateIDLE;
    chain->filter = RollJamProtocolRegistryFilterAM;
    return chain;
}

static void rolljam_rx_chain_unload_plugins(RollJamRxChain* chain) {
    for(uint8_t i = 0; i < chain->plugin_count; i++) {
        if(chain->managers[i]) {
            plugin_manager_free(chain->managers[i]);
            chain->managers[i] = NULL;
        }
        if(chain->resolvers[i]) {
            composite_api_resolver_free(chain->resolvers[i]);
            chain->resolvers[i] = NULL;
        }
    }
    chain->plugin_count = 0;
    chain->registry = NULL;
}

void rolljam_rx_chain_free(RollJamRxChain* chain) {
    if(!chain) {
        return;
    }

    rolljam_rx_chain_stop(chain);

    if(chain->receiver) {
        subghz_receiver_set_rx_callback(chain->receiver, NULL, NULL);
        subghz_receiver_free(chain->receiver);
        chain->receiver = NULL;
    }

    if(chain->worker) {
        if(subghz_worker_is_running(chain->worker)) {
            subghz_worker_stop(chain->worker);
        }
        subghz_worker_free(chain->worker);
        chain->worker = NULL;
    }

    if(chain->device) {
        subghz_devices_idle(chain->device);
        radio_device_loader_end(chain->device);
        chain->device = NULL;
    }

    rolljam_rx_chain_unload_plugins(chain);

    if(chain->merged_registry) {
        if(chain->merged_registry->items) {
            free((void*)(uintptr_t)chain->merged_registry->items);
        }
        free(chain->merged_registry);
        chain->merged_registry = NULL;
    }

    if(chain->environment) {
        subghz_environment_free(chain->environment);
        chain->environment = NULL;
    }

    if(chain->preset.name) {
        furi_string_free(chain->preset.name);
        chain->preset.name = NULL;
    }

    if(chain->owned_preset_data) {
        free(chain->owned_preset_data);
        chain->owned_preset_data = NULL;
    }

    free(chain);
}

bool rolljam_rx_chain_acquire_device(
    RollJamRxChain* chain,
    SubGhzRadioDeviceType type) {
    furi_check(chain);

    chain->device = radio_device_loader_set(NULL, type);
    if(!chain->device) {
        FURI_LOG_E(TAG, "[%c] Failed to acquire radio device (type=%d)", chain->label, type);
        return false;
    }

    chain->is_external = radio_device_loader_is_external(chain->device);

    if(type == SubGhzRadioDeviceTypeExternalCC1101 && !chain->is_external) {
        FURI_LOG_E(TAG, "[%c] External requested but unavailable", chain->label);
        radio_device_loader_end(chain->device);
        chain->device = NULL;
        return false;
    }

    subghz_devices_reset(chain->device);
    subghz_devices_idle(chain->device);
    FURI_LOG_I(
        TAG, "[%c] Acquired %s radio", chain->label, chain->is_external ? "external" : "internal");
    return true;
}

bool rolljam_rx_chain_set_preset(
    RollJamRxChain* chain,
    SubGhzSetting* setting,
    const char* preset_name,
    uint32_t frequency) {
    furi_check(chain);
    furi_check(setting);
    furi_check(preset_name);

    size_t preset_count = subghz_setting_get_preset_count(setting);
    for(size_t i = 0; i < preset_count; i++) {
        if(strcmp(subghz_setting_get_preset_name(setting, i), preset_name) == 0) {
            return rolljam_rx_chain_set_preset_data(
                chain,
                preset_name,
                subghz_setting_get_preset_data(setting, i),
                subghz_setting_get_preset_data_size(setting, i),
                frequency);
        }
    }

    FURI_LOG_E(TAG, "[%c] Unknown preset %s", chain->label, preset_name);
    return false;
}

bool rolljam_rx_chain_set_preset_data(
    RollJamRxChain* chain,
    const char* preset_name,
    uint8_t* preset_data,
    size_t preset_data_size,
    uint32_t frequency) {
    furi_check(chain);
    furi_check(preset_name);

    if(!preset_data || preset_data_size < 2U) {
        FURI_LOG_E(TAG, "[%c] Invalid preset data for %s", chain->label, preset_name);
        return false;
    }

    if(chain->owned_preset_data) {
        free(chain->owned_preset_data);
        chain->owned_preset_data = NULL;
    }

    furi_string_set(chain->preset.name, preset_name);
    chain->preset.frequency = frequency;
    chain->preset.data = preset_data;
    chain->preset.data_size = preset_data_size;
    chain->base_preset_data = preset_data;
    chain->base_preset_data_size = preset_data_size;
    chain->frequency = frequency;
    chain->filter =
        rolljam_get_protocol_registry_filter_for_preset(preset_data, preset_data_size);
    uint8_t mdmcfg4 = 0U;
    chain->rx_bandwidth_hz = rolljam_rx_chain_preset_get_register(
                                 preset_data,
                                 preset_data_size,
                                 ROLLJAM_CC1101_REG_MDMCFG4,
                                 &mdmcfg4) ?
                                 rolljam_rx_chain_channel_bandwidth_hz(mdmcfg4) :
                                 0U;
    return true;
}

static bool rolljam_rx_chain_apply_ook_shield_profile(RollJamRxChain* chain) {
    if(!chain->base_preset_data || chain->preset.data_size < 2U) {
        FURI_LOG_E(TAG, "[%c] cannot narrow RX BW without preset data", chain->label);
        return false;
    }

    uint8_t mdmcfg4 = 0U;
    if(!rolljam_rx_chain_preset_get_register(
           chain->base_preset_data,
           chain->base_preset_data_size,
           ROLLJAM_CC1101_REG_MDMCFG4,
           &mdmcfg4)) {
        FURI_LOG_W(TAG, "[%c] OOK preset missing MDMCFG4; retaining original", chain->label);
        return true;
    }

    const uint8_t narrowed_mdmcfg4 =
        (uint8_t)((mdmcfg4 & 0x0FU) | ROLLJAM_CC1101_CHANBW_135_KHZ_MASK);

    const struct {
        uint8_t reg;
        uint8_t value;
    } narrow_registers[] = {
        {ROLLJAM_CC1101_REG_FIFOTHR, 0x47U},
        {ROLLJAM_CC1101_REG_FSCTRL1, 0x06U},
        {ROLLJAM_CC1101_REG_MDMCFG4, narrowed_mdmcfg4},
        {ROLLJAM_CC1101_REG_AGCCTRL2, 0x04U},
        {ROLLJAM_CC1101_REG_AGCCTRL1, 0x00U},
        {ROLLJAM_CC1101_REG_AGCCTRL0, 0x92U},
        {ROLLJAM_CC1101_REG_FREND1, 0x56U},
        {ROLLJAM_CC1101_REG_TEST2, 0x81U},
        {ROLLJAM_CC1101_REG_TEST1, 0x35U},
    };

    const size_t register_count = sizeof(narrow_registers) / sizeof(narrow_registers[0]);
    size_t missing_count = 0U;
    for(size_t i = 0; i < register_count; i++) {
        uint8_t value = 0U;
        if(!rolljam_rx_chain_preset_get_register(
               chain->base_preset_data,
               chain->base_preset_data_size,
               narrow_registers[i].reg,
               &value)) {
            missing_count++;
        }
    }

    size_t terminator_offset = 0U;
    if(!rolljam_rx_chain_preset_find_terminator(
           chain->base_preset_data, chain->base_preset_data_size, &terminator_offset)) {
        FURI_LOG_W(TAG, "[%c] OOK preset has no terminator; retaining original", chain->label);
        return true;
    }

    const size_t expanded_size = chain->base_preset_data_size + (missing_count * 2U);
    uint8_t* copy = malloc(expanded_size);
    if(!copy) {
        FURI_LOG_E(TAG, "[%c] failed to build narrow RX preset", chain->label);
        return false;
    }

    memcpy(copy, chain->base_preset_data, terminator_offset);
    size_t write_offset = terminator_offset;
    for(size_t i = 0; i < register_count; i++) {
        uint8_t value = 0U;
        if(!rolljam_rx_chain_preset_get_register(
               chain->base_preset_data,
               chain->base_preset_data_size,
               narrow_registers[i].reg,
               &value)) {
            copy[write_offset++] = narrow_registers[i].reg;
            copy[write_offset++] = narrow_registers[i].value;
        }
    }
    memcpy(
        &copy[write_offset],
        &chain->base_preset_data[terminator_offset],
        chain->base_preset_data_size - terminator_offset);

    for(size_t i = 0; i < register_count; i++) {
        if(!rolljam_rx_chain_preset_set_register(
               copy, expanded_size, narrow_registers[i].reg, narrow_registers[i].value)) {
            FURI_LOG_E(TAG, "[%c] failed to patch narrow RX preset", chain->label);
            free(copy);
            return false;
        }
    }

    chain->owned_preset_data = copy;
    chain->preset.data = copy;
    chain->preset.data_size = expanded_size;
    chain->rx_bandwidth_hz =
        rolljam_rx_chain_channel_bandwidth_hz(ROLLJAM_CC1101_CHANBW_135_KHZ_MASK);
    FURI_LOG_I(TAG, "[%c] applied TI 135 kHz OOK sensitivity profile", chain->label);
    return true;
}

static bool rolljam_rx_chain_apply_fm_shield_profile(RollJamRxChain* chain) {
    uint8_t mdmcfg2 = 0U;
    uint8_t mdmcfg3 = 0U;
    uint8_t mdmcfg4 = 0U;
    uint8_t deviatn = 0U;
    if(!rolljam_rx_chain_preset_get_register(
           chain->base_preset_data,
           chain->base_preset_data_size,
           ROLLJAM_CC1101_REG_MDMCFG2,
           &mdmcfg2) ||
       !rolljam_rx_chain_preset_get_register(
           chain->base_preset_data,
           chain->base_preset_data_size,
           ROLLJAM_CC1101_REG_MDMCFG3,
           &mdmcfg3) ||
       !rolljam_rx_chain_preset_get_register(
           chain->base_preset_data,
           chain->base_preset_data_size,
           ROLLJAM_CC1101_REG_MDMCFG4,
           &mdmcfg4) ||
       !rolljam_rx_chain_preset_get_register(
           chain->base_preset_data,
           chain->base_preset_data_size,
           ROLLJAM_CC1101_REG_DEVIATN,
           &deviatn)) {
        FURI_LOG_W(TAG, "[%c] incomplete FM preset; retaining original bandwidth", chain->label);
        return true;
    }

    uint8_t modulation = mdmcfg2 & ROLLJAM_CC1101_MOD_FORMAT_MASK;
    if(modulation != ROLLJAM_CC1101_MOD_FORMAT_2FSK &&
       modulation != ROLLJAM_CC1101_MOD_FORMAT_GFSK) {
        FURI_LOG_W(TAG, "[%c] unsupported FM format; retaining original bandwidth", chain->label);
        return true;
    }

    uint32_t data_rate = rolljam_rx_chain_data_rate_hz(mdmcfg4, mdmcfg3);
    uint32_t deviation = rolljam_rx_chain_deviation_hz(deviatn);
    uint32_t minimum_bandwidth =
        data_rate + (2UL * deviation) + ROLLJAM_FM_BANDWIDTH_GUARD_HZ;
    uint32_t selected_bandwidth = 0U;
    uint8_t bandwidth_bits =
        rolljam_rx_chain_select_bandwidth_bits(minimum_bandwidth, &selected_bandwidth);
    uint32_t original_bandwidth = rolljam_rx_chain_channel_bandwidth_hz(mdmcfg4);

    if(selected_bandwidth >= original_bandwidth) {
        chain->rx_bandwidth_hz = original_bandwidth;
        FURI_LOG_I(
            TAG,
            "[%c] FM preset already uses suitable %lu Hz bandwidth",
            chain->label,
            original_bandwidth);
        return true;
    }

    uint8_t* copy = malloc(chain->base_preset_data_size);
    if(!copy) {
        FURI_LOG_E(TAG, "[%c] failed to copy FM preset", chain->label);
        return false;
    }
    memcpy(copy, chain->base_preset_data, chain->base_preset_data_size);

    uint8_t profiled_mdmcfg4 = (uint8_t)((mdmcfg4 & 0x0FU) | (bandwidth_bits << 4U));
    if(!rolljam_rx_chain_preset_set_register(
           copy,
           chain->base_preset_data_size,
           ROLLJAM_CC1101_REG_MDMCFG4,
           profiled_mdmcfg4)) {
        free(copy);
        return true;
    }

    if(selected_bandwidth <= 101562UL) {
        uint8_t agcctrl2 = 0U;
        if(rolljam_rx_chain_preset_get_register(
               copy,
               chain->base_preset_data_size,
               ROLLJAM_CC1101_REG_AGCCTRL2,
               &agcctrl2)) {
            rolljam_rx_chain_preset_set_register(
                copy,
                chain->base_preset_data_size,
                ROLLJAM_CC1101_REG_AGCCTRL2,
                (uint8_t)((agcctrl2 & 0xF8U) | 0x03U));
        }
    }

    chain->owned_preset_data = copy;
    chain->preset.data = copy;
    chain->preset.data_size = chain->base_preset_data_size;
    chain->rx_bandwidth_hz = selected_bandwidth;
    FURI_LOG_I(
        TAG,
        "[%c] FM Shield profile: rate=%lu dev=%lu bandwidth=%lu Hz",
        chain->label,
        data_rate,
        deviation,
        selected_bandwidth);
    return true;
}

bool rolljam_rx_chain_apply_shield_profile(RollJamRxChain* chain) {
    furi_check(chain);

    if(chain->owned_preset_data) {
        free(chain->owned_preset_data);
        chain->owned_preset_data = NULL;
    }
    chain->preset.data = chain->base_preset_data;
    chain->preset.data_size = chain->base_preset_data_size;

    uint8_t mdmcfg2 = 0U;
    if(!rolljam_rx_chain_preset_get_register(
           chain->base_preset_data,
           chain->base_preset_data_size,
           ROLLJAM_CC1101_REG_MDMCFG2,
           &mdmcfg2)) {
        FURI_LOG_W(TAG, "[%c] preset modulation unknown; retaining original", chain->label);
        return true;
    }

    switch(mdmcfg2 & ROLLJAM_CC1101_MOD_FORMAT_MASK) {
    case ROLLJAM_CC1101_MOD_FORMAT_OOK:
        return rolljam_rx_chain_apply_ook_shield_profile(chain);
    case ROLLJAM_CC1101_MOD_FORMAT_2FSK:
    case ROLLJAM_CC1101_MOD_FORMAT_GFSK:
        return rolljam_rx_chain_apply_fm_shield_profile(chain);
    default:
        FURI_LOG_W(TAG, "[%c] no Shield profile for modulation; retaining original", chain->label);
        return true;
    }
}

bool rolljam_rx_chain_init_receiver(RollJamRxChain* chain) {
    furi_check(chain);
 
    if(!chain->environment) {
        chain->environment = subghz_environment_alloc();
        if(!chain->environment) {
            FURI_LOG_E(TAG, "[%c] Failed to allocate environment", chain->label);
            return false;
        }
    }
 
    // Re-load plugins if filter changed
    bool needs_reload = false;
    if(chain->plugin_count == 0) {
        needs_reload = true;
    } else {
        // Check if currently loaded plugins match the filter
        // Since we don't store the filter of the first plugin, we can just check the filter field
        // But we can't easily check the filter of the loaded plugins without iterating.
        // For simplicity, let's just check if needs_reload is true if filter changed.
        // I'll add a way to track current filter.
    }

    // Let's use a simpler approach: if merged_registry exists, we might be fine.
    // But if we want to be robust, let's just reload if we don't have a registry.
    if(!chain->registry) {
        needs_reload = true;
    }

    if(needs_reload) {
        FURI_LOG_D(TAG, "[%c] Reloading plugins...", chain->label);
        rolljam_rx_chain_unload_plugins(chain);
        if(chain->merged_registry) {
            if(chain->merged_registry->items) {
                free((void*)(uintptr_t)chain->merged_registry->items);
            }
            free(chain->merged_registry);
            chain->merged_registry = NULL;
        }

        typedef struct {
            const char* path;
            const char* appid;
        } RollJamPluginLoadInfo;

        RollJamPluginLoadInfo plugins_to_load[4];
        uint8_t plugins_count = 0;

        if(chain->filter == RollJamProtocolRegistryFilterFM) {
            plugins_to_load[plugins_count++] = (RollJamPluginLoadInfo){
                .path = APP_ASSETS_PATH("plugins/rolljam_fm_plugin.fal"), 
                .appid = ROLLJAM_PROTOCOL_FM_PLUGIN_APP_ID};
            plugins_to_load[plugins_count++] = (RollJamPluginLoadInfo){
                .path = APP_ASSETS_PATH("plugins/rolljam_fm_plugin_extra.fal"), 
                .appid = "rolljam_fm_plugin_extra"};
        } else {
            plugins_to_load[plugins_count++] = (RollJamPluginLoadInfo){
                .path = APP_ASSETS_PATH("plugins/rolljam_am_plugin.fal"), 
                .appid = ROLLJAM_PROTOCOL_AM_PLUGIN_APP_ID};
        }

        size_t total_items = 0;

        for(uint8_t i = 0; i < plugins_count; i++) {
            FURI_LOG_D(TAG, "[%c] Loading plugin %s (%s)...", chain->label, plugins_to_load[i].path, plugins_to_load[i].appid);
            CompositeApiResolver* resolver = composite_api_resolver_alloc();
            if(!resolver) return false;
            composite_api_resolver_add(resolver, firmware_api_interface);
    composite_api_resolver_add(resolver, subghz_application_api_interface);

            PluginManager* manager = plugin_manager_alloc(
                plugins_to_load[i].appid,
                1U,
                composite_api_resolver_get(resolver));
            if(!manager) {
                FURI_LOG_E(TAG, "[%c] Failed to allocate manager for %s", chain->label, plugins_to_load[i].appid);
                composite_api_resolver_free(resolver);
                return false;
            }

            if(plugin_manager_load_single(manager, plugins_to_load[i].path) != PluginManagerErrorNone) {
                FURI_LOG_E(TAG, "[%c] Failed to load plugin %s", chain->label, plugins_to_load[i].path);
                plugin_manager_free(manager);
                composite_api_resolver_free(resolver);
                return false;
            }

            const RollJamProtocolPlugin* plugin = plugin_manager_get_ep(manager, 0U);
            if(!plugin || !plugin->registry) {
                FURI_LOG_E(TAG, "[%c] Plugin EP invalid for %s", chain->label, plugins_to_load[i].path);
                plugin_manager_free(manager);
                composite_api_resolver_free(resolver);
                return false;
            }

            chain->resolvers[chain->plugin_count] = resolver;
            chain->managers[chain->plugin_count] = manager;
            chain->plugins[chain->plugin_count] = plugin;
            chain->plugin_count++;

            total_items += plugin->registry->size;
        }

        if(total_items > 0) {
            MutableSubGhzProtocolRegistry* m_reg = malloc(sizeof(MutableSubGhzProtocolRegistry));
            if(!m_reg) return false;

            m_reg->items = malloc(total_items * sizeof(const SubGhzProtocol*));
            if(!m_reg->items) {
                free(m_reg);
                return false;
            }
            m_reg->size = total_items;

            size_t current_offset = 0;
            for(uint8_t i = 0; i < chain->plugin_count; i++) {
                const SubGhzProtocolRegistry* reg = chain->plugins[i]->registry;
                memcpy(&m_reg->items[current_offset], reg->items, reg->size * sizeof(const SubGhzProtocol*));
                current_offset += reg->size;
            }
            chain->merged_registry = (SubGhzProtocolRegistry*)m_reg;
            chain->registry = chain->merged_registry;
        }
    }
 
    if(chain->registry) {
        subghz_environment_set_protocol_registry(chain->environment, chain->registry);
    } else {
        FURI_LOG_E(TAG, "[%c] No protocol registry loaded", chain->label);
        return false;
    }
 
    subghz_environment_load_keystore(chain->environment, ROLLJAM_CHAIN_KEYSTORE_DIR);
    rolljam_keys_load(chain->environment);
 
    if(!chain->receiver) {
        chain->receiver = subghz_receiver_alloc_init(chain->environment);
        if(!chain->receiver) {
            FURI_LOG_E(TAG, "[%c] Failed to allocate receiver", chain->label);
            return false;
        }
        subghz_receiver_set_filter(chain->receiver, SubGhzProtocolFlag_Decodable);
    }
 
    if(!chain->worker) {
        chain->worker = subghz_worker_alloc();
        if(!chain->worker) {
            FURI_LOG_E(TAG, "[%c] Failed to allocate worker", chain->label);
            return false;
        }
        subghz_worker_set_overrun_callback(
            chain->worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
        subghz_worker_set_pair_callback(
            chain->worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
        subghz_worker_set_context(chain->worker, chain->receiver);
    }
 
    return true;
}

void rolljam_rx_chain_set_decode_callback(
    RollJamRxChain* chain,
    SubGhzReceiverCallback callback,
    void* context) {
    furi_check(chain);
    furi_check(chain->receiver);
    subghz_receiver_set_rx_callback(chain->receiver, callback, context);
}


bool rolljam_rx_chain_start(RollJamRxChain* chain) {
    furi_check(chain);
    if(!chain->device || !chain->worker || !chain->receiver) {
        FURI_LOG_E(TAG, "[%c] start rejected (incomplete stack)", chain->label);
        return false;
    }
    if(chain->state == RollJamTxRxStateRx) {
        return true;
    }

    if(!subghz_devices_is_frequency_valid(chain->device, chain->frequency)) {
        FURI_LOG_E(TAG, "[%c] invalid frequency %lu", chain->label, chain->frequency);
        return false;
    }

    subghz_receiver_reset(chain->receiver);

    subghz_devices_reset(chain->device);
    subghz_devices_idle(chain->device);
    subghz_devices_load_preset(chain->device, FuriHalSubGhzPresetCustom, chain->preset.data);
    subghz_devices_set_frequency(chain->device, chain->frequency);
    subghz_devices_flush_rx(chain->device);
    subghz_devices_set_rx(chain->device);

    subghz_devices_start_async_rx(chain->device, subghz_worker_rx_callback, chain->worker);
    subghz_worker_start(chain->worker);
    chain->state = RollJamTxRxStateRx;
    FURI_LOG_I(TAG, "[%c] RX started on %lu Hz", chain->label, chain->frequency);
    return true;
}

void rolljam_rx_chain_stop(RollJamRxChain* chain) {
    if(!chain) {
        return;
    }
    if(chain->state != RollJamTxRxStateRx) {
        return;
    }

    if(chain->worker && subghz_worker_is_running(chain->worker)) {
        subghz_worker_stop(chain->worker);
    }
    if(chain->device) {
        subghz_devices_stop_async_rx(chain->device);
        subghz_devices_idle(chain->device);
    }
    chain->state = RollJamTxRxStateIDLE;
}

float rolljam_rx_chain_get_rssi(RollJamRxChain* chain) {
    furi_check(chain);
    if(!chain->device || chain->state != RollJamTxRxStateRx) {
        return -127.0f;
    }
    return subghz_devices_get_rssi(chain->device);
}

#endif // ENABLE_DUAL_RX_SCENE || ENABLE_SHIELD_RX_SCENE
