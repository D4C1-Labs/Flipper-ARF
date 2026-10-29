#include "subghz_txrx_i.h"
#include <core/kernel.h>
#include "subghz_debug_log.h"

#include <math.h>
#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include "subghz_devices_lazy_compat.h"
#include "subghz_custom_btn_compat.h"
#include "subghz_lib_ext_compat.h"
#include "subghz_memmgr_pool_compat.h"
#include <loader/firmware_api/firmware_api.h>
#include <storage/storage.h>

#define TAG "SubGhzTxRx"

#define SUBGHZ_TXRX_WORKER_RAM_COST 11000

#define CC1101_EXT_STATUS_PATH EXT_PATH("subghz/.cc1101_ext_status")

static void subghz_txrx_radio_device_power_on(SubGhzTxRx* instance);
static void subghz_txrx_radio_device_power_off(SubGhzTxRx* instance);
static void subghz_txrx_ensure_external_device(SubGhzTxRx* instance);

#define SUBGHZ_TXRX_RECEIVER_REBUILD_FREE_HEAP 14000

#define SUBGHZ_TXRX_EXTERNAL_DEVICE_LOAD_FREE_HEAP 22000

#define SUBGHZ_TXRX_PROTOCOL_GROUP_RETRY_DELAY_MS 2000

static const SubGhzProtocolRegistry subghz_garage_empty_protocol_registry = {
    .items = NULL,
    .size = 0,
};

static void subghz_txrx_ensure_radio_init(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->radio_initialized && instance->radio_registry_loaded) {
        return;
    }

    FURI_LOG_I(TAG, "Radio init: free heap %zu", memmgr_get_free_heap());
    subghz_debug_log_write("Radio init: free heap %zu", memmgr_get_free_heap());

    if(!instance->radio_registry_loaded) {
        instance->radio_registry_loaded = true;

        subghz_debug_log_write("About to init device registry");
        subghz_garage_devices_init_radio_only();
        subghz_debug_log_write("Device registry init done");
        instance->radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        instance->radio_device_type = SubGhzRadioDeviceTypeInternal;

        subghz_txrx_ensure_external_device(instance);
        subghz_txrx_radio_device_power_on(instance);
        const SubGhzDevice* ext_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        bool is_connected = ext_device && subghz_devices_is_connect(ext_device);
        if(is_connected) {
            instance->radio_device = ext_device;
            subghz_devices_begin(instance->radio_device);
            instance->radio_device_type = SubGhzRadioDeviceTypeExternalCC1101;
        } else {
            subghz_txrx_radio_device_power_off(instance);
        }
        subghz_debug_log_write("Radio init: external connected=%d", (int)is_connected);

        Storage* ext_flag_storage = furi_record_open(RECORD_STORAGE);
        File* ext_flag_file = storage_file_alloc(ext_flag_storage);
        if(storage_file_open(
               ext_flag_file, CC1101_EXT_STATUS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            char c = is_connected ? '1' : '0';
            storage_file_write(ext_flag_file, &c, 1);
            storage_file_close(ext_flag_file);
        }
        storage_file_free(ext_flag_file);
        furi_record_close(RECORD_STORAGE);
    }

    if(!instance->radio_initialized) {
        instance->radio_initialized = true;

        instance->environment = subghz_environment_alloc();
        subghz_environment_set_alutech_at_4n_rainbow_table_file_name(
            instance->environment, SUBGHZ_ALUTECH_AT_4N_DIR_NAME);
        subghz_environment_set_nice_flor_s_rainbow_table_file_name(
            instance->environment, SUBGHZ_NICE_FLOR_S_DIR_NAME);
        subghz_environment_set_protocol_registry(
            instance->environment, (void*)instance->protocol_registry);
        instance->receiver = subghz_receiver_alloc_init(instance->environment);

        if(instance->receiver_filter_set) {
            subghz_receiver_set_filter(instance->receiver, instance->receiver_filter);
        }
        if(instance->rx_callback) {
            subghz_receiver_set_rx_callback(
                instance->receiver, instance->rx_callback, instance->rx_callback_context);
        }
    }

    FURI_LOG_I(TAG, "Radio init done: free heap %zu", memmgr_get_free_heap());
    subghz_debug_log_write("Radio init done: free heap %zu", memmgr_get_free_heap());
}

void subghz_txrx_release_radio(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(!instance->radio_registry_loaded) {
        return;
    }

    FURI_LOG_I(TAG, "Radio release: free heap %zu", memmgr_get_free_heap());
    subghz_debug_log_write("Radio release: free heap %zu", memmgr_get_free_heap());

    if(instance->radio_device_type != SubGhzRadioDeviceTypeInternal) {
        subghz_txrx_radio_device_power_off(instance);
        subghz_devices_end(instance->radio_device);
    }

    subghz_debug_log_write("About to deinit device registry");
    subghz_devices_deinit();
    subghz_debug_log_write("Device registry deinit done");

    instance->radio_device = NULL;
    instance->radio_device_type = SubGhzRadioDeviceTypeInternal;
    instance->radio_registry_loaded = false;
    instance->external_device_loaded = false;

    FURI_LOG_I(TAG, "Radio release done: free heap %zu", memmgr_get_free_heap());
    subghz_debug_log_write("Radio release done: free heap %zu", memmgr_get_free_heap());
}

static void subghz_txrx_unload_protocol_plugin(SubGhzTxRx* instance) {
    furi_assert(instance);

    instance->protocol_plugin = NULL;
    instance->protocol_registry = &subghz_garage_empty_protocol_registry;

    bool had_plugin = instance->protocol_plugin_manager != NULL;

    furi_kernel_lock();
    if(instance->protocol_plugin_manager) {
        plugin_manager_free(instance->protocol_plugin_manager);
        instance->protocol_plugin_manager = NULL;
    }
    if(instance->protocol_plugin_resolver) {
        composite_api_resolver_free(instance->protocol_plugin_resolver);
        instance->protocol_plugin_resolver = NULL;
    }
    furi_kernel_unlock();

    if(had_plugin) {
        FURI_LOG_I(
            TAG,
            "Protocol group plugin unloaded: pool free %zu, pool max block %zu",
            subghz_garage_pool_get_free(),
            subghz_garage_pool_get_max_block());
        subghz_debug_log_write(
            "Protocol group plugin unloaded: pool free %zu, pool max block %zu",
            subghz_garage_pool_get_free(),
            subghz_garage_pool_get_max_block());
    }
}

void subghz_txrx_ensure_keystore(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_txrx_ensure_radio_init(instance);

    if(instance->keystore_loaded) {
        return;
    }
    instance->keystore_loaded = true;

    FURI_LOG_I(TAG, "Loading keystore: free heap %zu", memmgr_get_free_heap());
    subghz_environment_load_keystore(instance->environment, SUBGHZ_KEYSTORE_DIR_NAME);
    subghz_environment_load_keystore(instance->environment, SUBGHZ_KEYSTORE_DIR_USER_NAME);
    FURI_LOG_I(TAG, "Keystore loaded: free heap %zu", memmgr_get_free_heap());
}

static const SubGhzGarageProtocolPlugin* subghz_txrx_load_garage_plugin(
    const char* path,
    PluginManager** out_manager,
    CompositeApiResolver** out_resolver) {
    CompositeApiResolver* resolver = composite_api_resolver_alloc();
    if(!resolver) {
        FURI_LOG_E(TAG, "Failed to allocate plugin resolver");
        return NULL;
    }
    composite_api_resolver_add(resolver, firmware_api_interface);

    PluginManager* manager = plugin_manager_alloc(
        SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID,
        SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION,
        composite_api_resolver_get(resolver));
    if(!manager) {
        FURI_LOG_E(TAG, "Failed to allocate plugin manager");
        composite_api_resolver_free(resolver);
        return NULL;
    }

    FURI_LOG_I(
        TAG,
        "Loading %s: free heap %zu, max free block %zu, pool free %zu, pool max block %zu",
        path,
        memmgr_get_free_heap(),
        memmgr_heap_get_max_free_block(),
        subghz_garage_pool_get_free(),
        subghz_garage_pool_get_max_block());
    subghz_debug_log_write(
        "Loading %s: free heap %zu, max free block %zu, pool free %zu, pool max block %zu",
        path,
        memmgr_get_free_heap(),
        memmgr_heap_get_max_free_block(),
        subghz_garage_pool_get_free(),
        subghz_garage_pool_get_max_block());

    PluginManagerError error = plugin_manager_load_single(manager, path);
    if(error != PluginManagerErrorNone) {
        FURI_LOG_E(TAG, "Failed to load plugin: %d, free heap %zu, pool free %zu", (int)error,
            memmgr_get_free_heap(), subghz_garage_pool_get_free());
        subghz_debug_log_write(
            "Failed to load plugin %s: error %d, free heap %zu, pool free %zu",
            path,
            (int)error,
            memmgr_get_free_heap(),
            subghz_garage_pool_get_free());
        furi_kernel_lock();
        plugin_manager_free(manager);
        composite_api_resolver_free(resolver);
        furi_kernel_unlock();
        return NULL;
    }

    FURI_LOG_I(
        TAG,
        "%s loaded: pool free %zu, pool max block %zu",
        path,
        subghz_garage_pool_get_free(),
        subghz_garage_pool_get_max_block());
    subghz_debug_log_write(
        "%s loaded: pool free %zu, pool max block %zu",
        path,
        subghz_garage_pool_get_free(),
        subghz_garage_pool_get_max_block());

    const SubGhzGarageProtocolPlugin* plugin = plugin_manager_get_ep(manager, 0U);
    if(!plugin) {
        FURI_LOG_E(TAG, "Plugin entry point is invalid");
        furi_kernel_lock();
        plugin_manager_free(manager);
        composite_api_resolver_free(resolver);
        furi_kernel_unlock();
        return NULL;
    }

    *out_manager = manager;
    *out_resolver = resolver;
    return plugin;
}

static const SubGhzGarageProtocolPlugin* subghz_txrx_try_load_protocol_group(
    SubGhzTxRx* instance,
    SubGhzGarageProtocolGroup group) {
    PluginManager* manager = NULL;
    CompositeApiResolver* resolver = NULL;
    const SubGhzGarageProtocolPlugin* plugin = subghz_txrx_load_garage_plugin(
        subghz_garage_protocol_group_paths[group], &manager, &resolver);
    if(!plugin || !plugin->registry) {
        return NULL;
    }
    FURI_LOG_I(TAG, "Plugin .fal loaded: free heap %zu", memmgr_get_free_heap());

    if(memmgr_get_free_heap() < SUBGHZ_TXRX_RECEIVER_REBUILD_FREE_HEAP) {
        FURI_LOG_E(
            TAG,
            "Not enough free heap to rebuild receiver (%zu < %d)",
            memmgr_get_free_heap(),
            SUBGHZ_TXRX_RECEIVER_REBUILD_FREE_HEAP);
        furi_kernel_lock();
        plugin_manager_free(manager);
        composite_api_resolver_free(resolver);
        furi_kernel_unlock();
        return NULL;
    }

    instance->protocol_plugin_resolver = resolver;
    instance->protocol_plugin_manager = manager;
    instance->protocol_plugin = plugin;
    instance->protocol_registry = plugin->registry;
    subghz_environment_set_protocol_registry(
        instance->environment, (void*)instance->protocol_registry);

    instance->receiver = subghz_receiver_alloc_init(instance->environment);
    FURI_LOG_I(TAG, "Receiver rebuilt against new group: free heap %zu", memmgr_get_free_heap());

    if(instance->worker) {
        subghz_garage_worker_set_pair_callback(
            instance->worker, (SubGhzGarageWorkerPairCallback)subghz_receiver_decode);
        subghz_garage_worker_set_context(instance->worker, instance->receiver);
    }

    if(instance->receiver_filter_set) {
        subghz_receiver_set_filter(instance->receiver, instance->receiver_filter);
    }
    if(instance->rx_callback) {
        subghz_receiver_set_rx_callback(
            instance->receiver, instance->rx_callback, instance->rx_callback_context);
    }

    return plugin;
}

const SubGhzGarageProtocolPlugin*
    subghz_txrx_ensure_protocol_group(SubGhzTxRx* instance, SubGhzGarageProtocolGroup group) {
    furi_assert(instance);
    subghz_txrx_ensure_radio_init(instance);

    if(instance->protocol_plugin && instance->active_protocol_group == group) {
        return instance->protocol_plugin;
    }

    if(instance->protocol_plugin_load_failed && instance->active_protocol_group == group) {

        return NULL;
    }

    if(instance->receiver) {
        subghz_receiver_free(instance->receiver);
        instance->receiver = NULL;
    }
    if(instance->protocol_plugin) {
        subghz_txrx_unload_protocol_plugin(instance);
    }
    instance->active_protocol_group = group;

    const SubGhzGarageProtocolPlugin* plugin =
        subghz_txrx_try_load_protocol_group(instance, group);
    if(!plugin) {
        FURI_LOG_W(
            TAG,
            "Group %d load failed, free heap %zu - waiting %dms to retry once",
            (int)group,
            memmgr_get_free_heap(),
            SUBGHZ_TXRX_PROTOCOL_GROUP_RETRY_DELAY_MS);
        furi_delay_ms(SUBGHZ_TXRX_PROTOCOL_GROUP_RETRY_DELAY_MS);
        FURI_LOG_I(TAG, "Retrying group %d load: free heap %zu", (int)group, memmgr_get_free_heap());
        plugin = subghz_txrx_try_load_protocol_group(instance, group);
    }

    if(!plugin) {
        FURI_LOG_E(TAG, "Group %d load failed again after retry - giving up", (int)group);
        instance->protocol_plugin_load_failed = true;

        instance->protocol_registry = &subghz_garage_empty_protocol_registry;
        subghz_environment_set_protocol_registry(
            instance->environment, (void*)instance->protocol_registry);
        instance->receiver = subghz_receiver_alloc_init(instance->environment);
        return NULL;
    }

    instance->protocol_plugin_load_failed = false;
    return plugin;
}

const SubGhzGarageProtocolPlugin* subghz_txrx_ensure_protocol_plugin(SubGhzTxRx* instance) {
    furi_assert(instance);
    return subghz_txrx_ensure_protocol_group(instance, instance->active_protocol_group);
}

static void subghz_txrx_unload_tx_protocol_plugin(SubGhzTxRx* instance) {
    furi_assert(instance);

    instance->tx_protocol_plugin = NULL;

    furi_kernel_lock();
    if(instance->tx_protocol_plugin_manager) {
        plugin_manager_free(instance->tx_protocol_plugin_manager);
        instance->tx_protocol_plugin_manager = NULL;
    }
    if(instance->tx_protocol_plugin_resolver) {
        composite_api_resolver_free(instance->tx_protocol_plugin_resolver);
        instance->tx_protocol_plugin_resolver = NULL;
    }
    furi_kernel_unlock();
}

const SubGhzGarageProtocolPlugin* subghz_txrx_ensure_tx_protocol_plugin(
    SubGhzTxRx* instance,
    SubGhzGarageTxProtocol tx_protocol) {
    furi_assert(instance);

    if(instance->tx_protocol_plugin_loaded && instance->active_tx_protocol == tx_protocol) {
        if(instance->tx_protocol_plugin) {
            subghz_environment_set_protocol_registry(
                instance->environment, (void*)instance->tx_protocol_plugin->registry);
        }
        return instance->tx_protocol_plugin;
    }

    if(instance->tx_protocol_plugin_loaded) {
        subghz_txrx_unload_tx_protocol_plugin(instance);
    }
    instance->tx_protocol_plugin_loaded = true;
    instance->active_tx_protocol = tx_protocol;

    PluginManager* manager = NULL;
    CompositeApiResolver* resolver = NULL;
    const SubGhzGarageProtocolPlugin* plugin = subghz_txrx_load_garage_plugin(
        subghz_garage_tx_protocol_paths[tx_protocol], &manager, &resolver);
    if(!plugin) {
        return NULL;
    }

    instance->tx_protocol_plugin_resolver = resolver;
    instance->tx_protocol_plugin_manager = manager;
    instance->tx_protocol_plugin = plugin;

    subghz_environment_set_protocol_registry(instance->environment, (void*)plugin->registry);

    return plugin;
}

void subghz_txrx_restore_rx_protocol_registry(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_environment_set_protocol_registry(
        instance->environment, (void*)instance->protocol_registry);
}

void subghz_txrx_set_protocol_group(SubGhzTxRx* instance, SubGhzGarageProtocolGroup group) {
    furi_assert(instance);

    if(instance->active_protocol_group == group && instance->protocol_plugin) {
        return;
    }

    if(instance->protocol_plugin) {
        subghz_receiver_free(instance->receiver);
        subghz_txrx_unload_protocol_plugin(instance);

        subghz_environment_set_protocol_registry(
            instance->environment, (void*)instance->protocol_registry);
        instance->receiver = subghz_receiver_alloc_init(instance->environment);
    }
    if(instance->active_protocol_group != group) {
        instance->protocol_plugin_load_failed = false;
    }
    instance->active_protocol_group = group;
}

void subghz_txrx_release_protocol_group(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(!instance->protocol_plugin) {
        return;
    }

    subghz_receiver_free(instance->receiver);
    subghz_txrx_unload_protocol_plugin(instance);
    subghz_environment_set_protocol_registry(
        instance->environment, (void*)instance->protocol_registry);
    instance->receiver = subghz_receiver_alloc_init(instance->environment);
}

SubGhzGarageProtocolGroup subghz_txrx_get_protocol_group(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->active_protocol_group;
}

void subghz_txrx_reset_protocol_load_failed(SubGhzTxRx* instance) {
    furi_assert(instance);
    instance->protocol_plugin_load_failed = false;
}

const SubGhzProtocolRegistry* subghz_txrx_get_protocol_registry(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_txrx_ensure_protocol_plugin(instance);
    return instance->protocol_registry;
}

static void subghz_txrx_radio_device_power_on(SubGhzTxRx* instance) {
    UNUSED(instance);
    uint8_t attempts = 0;
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();

        furi_delay_ms(10);
    }
}

static void subghz_txrx_radio_device_power_off(SubGhzTxRx* instance) {
    UNUSED(instance);
    if(furi_hal_power_is_otg_enabled()) furi_hal_power_disable_otg();
}

SubGhzTxRx* subghz_txrx_alloc(void) {
    FURI_LOG_I(TAG, "txrx_alloc start: free heap %zu", memmgr_get_free_heap());

    SubGhzTxRx* instance = malloc(sizeof(SubGhzTxRx));
    instance->protocol_plugin_load_failed = false;
    instance->setting = subghz_setting_alloc();

    subghz_setting_load(instance->setting, EXT_PATH("subghz/assets/setting_garage_full"));

    FURI_LOG_I(TAG, "txrx_alloc after setting_load: free heap %zu", memmgr_get_free_heap());

    instance->preset = malloc(sizeof(SubGhzRadioPreset));
    instance->preset->name = furi_string_alloc();
    subghz_txrx_set_default_preset(instance, 0);

    instance->txrx_state = SubGhzTxRxStateSleep;

    subghz_txrx_hopper_set_state(instance, SubGhzHopperStateOFF);
    subghz_txrx_speaker_set_state(instance, SubGhzSpeakerStateDisable);
    subghz_txrx_set_debug_pin_state(instance, false);

    instance->worker = NULL;
    instance->fff_data = flipper_format_string_alloc();

    FURI_LOG_I(TAG, "txrx_alloc after fff_data: free heap %zu", memmgr_get_free_heap());

    {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        instance->is_database_loaded = storage_file_exists(storage, SUBGHZ_KEYSTORE_DIR_NAME);
        furi_record_close(RECORD_STORAGE);
    }
    instance->keystore_loaded = false;

    instance->active_protocol_group = SubGhzGarageProtocolGroup1;
    instance->protocol_plugin = NULL;
    instance->protocol_plugin_manager = NULL;
    instance->protocol_plugin_resolver = NULL;
    instance->protocol_registry = &subghz_garage_empty_protocol_registry;
    instance->tx_protocol_plugin_loaded = false;
    instance->tx_protocol_plugin = NULL;
    instance->tx_protocol_plugin_manager = NULL;
    instance->tx_protocol_plugin_resolver = NULL;
    instance->receiver_filter_set = false;
    instance->rx_callback = NULL;
    instance->rx_callback_context = NULL;

    instance->radio_device = NULL;
    instance->radio_device_type = SubGhzRadioDeviceTypeInternal;
    instance->environment = NULL;
    instance->receiver = NULL;
    instance->radio_initialized = false;

    instance->radio_registry_loaded = false;
    instance->external_device_loaded = false;

    FURI_LOG_I(TAG, "txrx_alloc end: free heap %zu", memmgr_get_free_heap());

    return instance;
}

void subghz_txrx_free(SubGhzTxRx* instance) {
    furi_assert(instance);

    if(instance->radio_initialized) {

        subghz_txrx_release_radio(instance);

        if(instance->worker) {
            subghz_garage_worker_free(instance->worker);
        }

        subghz_receiver_free(instance->receiver);

        furi_kernel_lock();
        subghz_environment_free(instance->environment);
        furi_kernel_unlock();
    }

    subghz_txrx_unload_protocol_plugin(instance);
    if(instance->tx_protocol_plugin_loaded) {
        subghz_txrx_unload_tx_protocol_plugin(instance);
    }

    furi_kernel_lock();
    flipper_format_free(instance->fff_data);
    furi_string_free(instance->preset->name);
    subghz_setting_free(instance->setting);

    free(instance->preset);
    free(instance);
    furi_kernel_unlock();
}

bool subghz_txrx_is_database_loaded(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->is_database_loaded;
}

void subghz_txrx_set_preset(
    SubGhzTxRx* instance,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size) {
    furi_assert(instance);
    furi_string_set(instance->preset->name, preset_name);

    SubGhzRadioPreset* preset = instance->preset;
    preset->frequency = frequency;
    preset->data = preset_data;
    preset->data_size = preset_data_size;
}

uint8_t*
    subghz_txrx_set_tx_power(uint8_t* preset_data, size_t preset_data_size, uint8_t tx_power) {
#define PRESET_POWER_OFFSET_FM 8
#define PRESET_POWER_OFFSET_AM 7
#define TX_PATABLE_OFFSET_AM   8
#define TX_PATABLE_COUNT       17

    const uint8_t tx_pa_table[TX_PATABLE_COUNT] = {
        0,
        0xC0,
        0xCD,
        0x86,
        0x50,
        0x26,
        0x1D,
        0x17,
        0x03,
        0xC0,
        0xC8,
        0x84,
        0x60,
        0x34,
        0x1D,
        0x0E,
        0x12,
    };

    uint8_t fm_byte = preset_data[preset_data_size - PRESET_POWER_OFFSET_FM];
    uint8_t am_byte = preset_data[preset_data_size - PRESET_POWER_OFFSET_AM];

    if(fm_byte && !am_byte) {

        if(tx_power) {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_FM] =
                tx_pa_table[TX_PATABLE_OFFSET_AM + tx_power];
        } else {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_FM] =
                tx_pa_table[1];
        }
    } else if(am_byte && !fm_byte) {

        if(tx_power) {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_AM] = tx_pa_table[tx_power];
        } else {
            preset_data[preset_data_size - PRESET_POWER_OFFSET_AM] =
                tx_pa_table[1];
        }
    }

    return preset_data;
}

const char* subghz_txrx_get_preset_name(SubGhzTxRx* instance, const char* preset) {
    UNUSED(instance);
    const char* preset_name = "";
    if(!strcmp(preset, "FuriHalSubGhzPresetOok270Async")) {
        preset_name = "AM270";
    } else if(!strcmp(preset, "FuriHalSubGhzPresetOok650Async")) {
        preset_name = "AM650";
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev238Async")) {
        preset_name = "FM238";
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev12KAsync")) {
        preset_name = "FM12K";
    } else if(!strcmp(preset, "FuriHalSubGhzPreset2FSKDev476Async")) {
        preset_name = "FM476";
    } else if(!strcmp(preset, "FuriHalSubGhzPresetCustom")) {
        preset_name = "CUSTOM";
    } else {
        FURI_LOG_E(TAG, "Unknown preset");
    }
    return preset_name;
}

SubGhzRadioPreset subghz_txrx_get_preset(SubGhzTxRx* instance) {
    furi_assert(instance);
    return *instance->preset;
}

void subghz_txrx_get_frequency_and_modulation(
    SubGhzTxRx* instance,
    FuriString* frequency,
    FuriString* modulation,
    bool long_name) {
    furi_assert(instance);
    SubGhzRadioPreset* preset = instance->preset;
    if(frequency != NULL) {
        furi_string_printf(
            frequency,
            "%03ld.%02ld",
            preset->frequency / 1000000 % 1000,
            preset->frequency / 10000 % 100);
    }
    if(modulation != NULL) {
        if(long_name) {
            furi_string_printf(modulation, "%s", furi_string_get_cstr(preset->name));
        } else {
            furi_string_printf(modulation, "%.2s", furi_string_get_cstr(preset->name));
        }
    }
}

static void subghz_txrx_reverify_external_or_fallback(SubGhzTxRx* instance) {
    if(instance->radio_device_type == SubGhzRadioDeviceTypeExternalCC1101 &&
       !subghz_devices_is_connect(instance->radio_device)) {
        FURI_LOG_W(TAG, "External CC1101 stopped answering - falling back to Internal");
        subghz_debug_log_write("External CC1101 stopped answering - falling back to Internal");
        subghz_txrx_radio_device_power_off(instance);
        subghz_devices_end(instance->radio_device);
        instance->radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        instance->radio_device_type = SubGhzRadioDeviceTypeInternal;
        Storage* fallback_storage = furi_record_open(RECORD_STORAGE);
        File* fallback_file = storage_file_alloc(fallback_storage);
        if(storage_file_open(
               fallback_file, CC1101_EXT_STATUS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            char fallback_c = '0';
            storage_file_write(fallback_file, &fallback_c, 1);
            storage_file_close(fallback_file);
        }
        storage_file_free(fallback_file);
        furi_record_close(RECORD_STORAGE);
    }
}

static void subghz_txrx_begin(SubGhzTxRx* instance, uint8_t* preset_data) {
    furi_assert(instance);

    subghz_txrx_reverify_external_or_fallback(instance);

    FURI_LOG_I(TAG, "txrx_begin: radio_device=%p preset_data=%p",
        (void*)instance->radio_device, (void*)preset_data);
    subghz_debug_log_write(
        "txrx_begin: radio_device=%p preset_data=%p",
        (void*)instance->radio_device,
        (void*)preset_data);
    subghz_devices_reset(instance->radio_device);
    FURI_LOG_I(TAG, "txrx_begin: reset done");
    subghz_debug_log_write("txrx_begin: reset done");
    subghz_devices_idle(instance->radio_device);
    FURI_LOG_I(TAG, "txrx_begin: idle done, loading preset");
    subghz_debug_log_write("txrx_begin: idle done, loading preset");
    subghz_devices_load_preset(instance->radio_device, FuriHalSubGhzPresetCustom, preset_data);
    FURI_LOG_I(TAG, "txrx_begin: preset loaded");
    subghz_debug_log_write("txrx_begin: preset loaded");
    instance->txrx_state = SubGhzTxRxStateIDLE;
}

static bool subghz_txrx_ensure_worker(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->worker) {
        FURI_LOG_I(TAG, "ensure_worker: already allocated (%p), skipping", (void*)instance->worker);
        return true;
    }

    size_t max_block = memmgr_heap_get_max_free_block();
    if(max_block < SUBGHZ_TXRX_WORKER_RAM_COST) {
        FURI_LOG_E(
            TAG,
            "ensure_worker: refusing alloc, max free block %zu < %d",
            max_block,
            SUBGHZ_TXRX_WORKER_RAM_COST);
        subghz_debug_log_write(
            "ensure_worker: refusing alloc, max free block %zu < %d",
            max_block,
            SUBGHZ_TXRX_WORKER_RAM_COST);
        return false;
    }
    FURI_LOG_I(TAG, "Allocating worker: free heap %zu, max free block %zu, receiver=%p",
        memmgr_get_free_heap(), max_block, (void*)instance->receiver);
    subghz_debug_log_write(
        "Allocating worker: free heap %zu, max free block %zu, receiver=%p",
        memmgr_get_free_heap(),
        max_block,
        (void*)instance->receiver);
    instance->worker = subghz_garage_worker_alloc();
    FURI_LOG_I(TAG, "ensure_worker: alloc'd %p, wiring callbacks", (void*)instance->worker);
    subghz_debug_log_write("ensure_worker: alloc'd %p, wiring callbacks", (void*)instance->worker);
    subghz_garage_worker_set_overrun_callback(
        instance->worker, (SubGhzGarageWorkerOverrunCallback)subghz_receiver_reset);
    subghz_garage_worker_set_pair_callback(
        instance->worker, (SubGhzGarageWorkerPairCallback)subghz_receiver_decode);
    subghz_garage_worker_set_context(instance->worker, instance->receiver);
    FURI_LOG_I(TAG, "ensure_worker: done");
    subghz_debug_log_write("ensure_worker: done");
    return true;
}

static bool subghz_txrx_rx(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);
    furi_assert(
        instance->txrx_state != SubGhzTxRxStateRx && instance->txrx_state != SubGhzTxRxStateSleep);

    subghz_txrx_reverify_external_or_fallback(instance);

    FURI_LOG_I(TAG, "txrx_rx: freq=%lu, ensuring worker", (unsigned long)frequency);
    subghz_debug_log_write("txrx_rx: freq=%lu, ensuring worker", (unsigned long)frequency);

    if(!subghz_txrx_ensure_worker(instance)) {

        FURI_LOG_E(TAG, "txrx_rx: worker not ready, aborting rx start");
        subghz_debug_log_write("txrx_rx: worker not ready, aborting rx start");
        return false;
    }
    FURI_LOG_I(TAG, "txrx_rx: worker=%p, setting frequency", (void*)instance->worker);
    subghz_debug_log_write("txrx_rx: worker=%p, setting frequency", (void*)instance->worker);

    subghz_devices_idle(instance->radio_device);

    uint32_t value = subghz_devices_set_frequency(instance->radio_device, frequency);
    FURI_LOG_I(TAG, "txrx_rx: frequency set (%lu), flushing rx", (unsigned long)value);
    subghz_debug_log_write("txrx_rx: frequency set (%lu), flushing rx", (unsigned long)value);
    subghz_devices_flush_rx(instance->radio_device);
    subghz_txrx_speaker_on(instance);
    FURI_LOG_I(TAG, "txrx_rx: starting async rx");
    subghz_debug_log_write("txrx_rx: starting async rx");

    subghz_devices_start_async_rx(
        instance->radio_device, subghz_garage_worker_rx_callback, instance->worker);
    FURI_LOG_I(TAG, "txrx_rx: async rx started, starting worker");
    subghz_debug_log_write("txrx_rx: async rx started, starting worker");
    subghz_garage_worker_start(instance->worker);
    instance->txrx_state = SubGhzTxRxStateRx;
    FURI_LOG_I(TAG, "txrx_rx: worker started, done");
    subghz_debug_log_write("txrx_rx: worker started, done");
    return true;
}

static void subghz_txrx_idle(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->txrx_state != SubGhzTxRxStateSleep) {
        subghz_txrx_reverify_external_or_fallback(instance);
        subghz_devices_idle(instance->radio_device);
        subghz_txrx_speaker_off(instance);
        instance->txrx_state = SubGhzTxRxStateIDLE;
    }
}

static void subghz_txrx_rx_end(SubGhzTxRx* instance, bool release_worker) {
    furi_assert(instance);
    furi_assert(instance->txrx_state == SubGhzTxRxStateRx);
    FURI_LOG_I(TAG, "rx_end: release_worker=%d worker=%p radio_device=%p",
        (int)release_worker, (void*)instance->worker, (void*)instance->radio_device);

    if(instance->worker && subghz_garage_worker_is_running(instance->worker)) {
        FURI_LOG_I(TAG, "rx_end: stopping worker");
        subghz_garage_worker_stop(instance->worker);
        FURI_LOG_I(TAG, "rx_end: worker stopped, stopping async rx");
        subghz_devices_stop_async_rx(instance->radio_device);
        FURI_LOG_I(TAG, "rx_end: async rx stopped");
    }
    if(release_worker && instance->worker) {

        FURI_LOG_I(TAG, "rx_end: freeing worker");
        subghz_garage_worker_free(instance->worker);
        instance->worker = NULL;
        FURI_LOG_I(TAG, "rx_end: worker freed");
    }

    subghz_txrx_reverify_external_or_fallback(instance);
    subghz_devices_idle(instance->radio_device);
    subghz_txrx_speaker_off(instance);
    instance->txrx_state = SubGhzTxRxStateIDLE;
    FURI_LOG_I(TAG, "rx_end: done");
}

void subghz_txrx_sleep(SubGhzTxRx* instance) {
    furi_assert(instance);

    if(instance->radio_initialized) {
        subghz_devices_sleep(instance->radio_device);
    }
    instance->txrx_state = SubGhzTxRxStateSleep;
}

static bool subghz_txrx_tx(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);
    furi_assert(instance->txrx_state != SubGhzTxRxStateSleep);

    subghz_txrx_reverify_external_or_fallback(instance);

    subghz_devices_idle(instance->radio_device);
    subghz_devices_set_frequency(instance->radio_device, frequency);

    bool ret = subghz_devices_set_tx(instance->radio_device);
    if(ret) {
        subghz_txrx_speaker_on(instance);
        instance->txrx_state = SubGhzTxRxStateTx;
    }

    return ret;
}

SubGhzTxRxStartTxState subghz_txrx_tx_start(SubGhzTxRx* instance, FlipperFormat* flipper_format) {
    furi_assert(instance);
    furi_assert(flipper_format);

    subghz_txrx_ensure_protocol_plugin(instance);

    subghz_txrx_ensure_keystore(instance);
    subghz_txrx_stop(instance);

    SubGhzTxRxStartTxState ret = SubGhzTxRxStartTxStateErrorParserOthers;
    FuriString* temp_str = furi_string_alloc();
    do {
        if(!flipper_format_rewind(flipper_format)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }
        if(!flipper_format_read_string(flipper_format, "Protocol", temp_str)) {
            FURI_LOG_E(TAG, "Missing Protocol");
            break;
        }
        ret = SubGhzTxRxStartTxStateOk;

        SubGhzRadioPreset* preset = instance->preset;

        SubGhzGarageTxProtocol tx_protocol;
        bool using_tx_plugin_registry =
            subghz_garage_tx_protocol_for_name(furi_string_get_cstr(temp_str), &tx_protocol) &&
            subghz_txrx_ensure_tx_protocol_plugin(instance, tx_protocol) != NULL;

        instance->transmitter =
            subghz_transmitter_alloc_init(instance->environment, furi_string_get_cstr(temp_str));

        if(using_tx_plugin_registry) {
            subghz_txrx_restore_rx_protocol_registry(instance);
        }

        if(instance->transmitter) {
            if(subghz_transmitter_deserialize(instance->transmitter, flipper_format) ==
               SubGhzProtocolStatusOk) {
                if(strcmp(furi_string_get_cstr(preset->name), "") != 0) {
                    subghz_txrx_begin(
                        instance,
                        subghz_setting_get_preset_data_by_name(
                            instance->setting, furi_string_get_cstr(preset->name)));
                    if(preset->frequency) {
                        if(!subghz_txrx_tx(instance, preset->frequency)) {
                            FURI_LOG_E(TAG, "Only Rx");
                            ret = SubGhzTxRxStartTxStateErrorOnlyRx;
                        }
                    } else {
                        ret = SubGhzTxRxStartTxStateErrorParserOthers;
                    }

                } else {
                    FURI_LOG_E(
                        TAG, "Unknown name preset \" %s \"", furi_string_get_cstr(preset->name));
                    ret = SubGhzTxRxStartTxStateErrorParserOthers;
                }

                if(ret == SubGhzTxRxStartTxStateOk) {

                    subghz_devices_start_async_tx(
                        instance->radio_device, subghz_transmitter_yield, instance->transmitter);
                }
            } else {
                ret = SubGhzTxRxStartTxStateErrorParserOthers;
            }
        } else {
            ret = SubGhzTxRxStartTxStateErrorParserOthers;
        }
        if(ret != SubGhzTxRxStartTxStateOk) {
            if(instance->transmitter) {
                subghz_transmitter_free(instance->transmitter);
                instance->transmitter = NULL;
            }
            if(instance->txrx_state != SubGhzTxRxStateIDLE) {
                subghz_txrx_idle(instance);
            }
        }

    } while(false);
    furi_string_free(temp_str);
    return ret;
}

bool subghz_txrx_rx_start(SubGhzTxRx* instance) {
    furi_assert(instance);
    FURI_LOG_I(TAG, "rx_start: enter, preset name=\"%s\" freq=%lu",
        instance->preset && instance->preset->name ? furi_string_get_cstr(instance->preset->name) : "(null)",
        (unsigned long)(instance->preset ? instance->preset->frequency : 0));
    subghz_debug_log_write(
        "rx_start: enter, preset name=\"%s\" freq=%lu",
        instance->preset && instance->preset->name ? furi_string_get_cstr(instance->preset->name) : "(null)",
        (unsigned long)(instance->preset ? instance->preset->frequency : 0));
    bool protocol_ready = subghz_txrx_ensure_protocol_plugin(instance) != NULL;
    FURI_LOG_I(TAG, "rx_start: protocol_ready=%d, calling stop()", (int)protocol_ready);
    subghz_debug_log_write("rx_start: protocol_ready=%d, calling stop()", (int)protocol_ready);
    subghz_txrx_stop(instance);
    if(protocol_ready) {
        uint8_t* preset_data = subghz_setting_get_preset_data_by_name(
            subghz_txrx_get_setting(instance), furi_string_get_cstr(instance->preset->name));
        FURI_LOG_I(TAG, "rx_start: preset_data=%p, calling begin()", (void*)preset_data);
        subghz_debug_log_write("rx_start: preset_data=%p, calling begin()", (void*)preset_data);
        subghz_txrx_begin(instance, preset_data);
        FURI_LOG_I(TAG, "rx_start: begin() done, calling rx() at freq=%lu",
            (unsigned long)instance->preset->frequency);
        subghz_debug_log_write(
            "rx_start: begin() done, calling rx() at freq=%lu",
            (unsigned long)instance->preset->frequency);

        protocol_ready = subghz_txrx_rx(instance, instance->preset->frequency);
        FURI_LOG_I(TAG, "rx_start: rx() done");
        subghz_debug_log_write("rx_start: rx() done");
    }
    FURI_LOG_I(TAG, "rx_start: returning %d", (int)protocol_ready);
    subghz_debug_log_write("rx_start: returning %d", (int)protocol_ready);
    return protocol_ready;
}

void subghz_txrx_set_need_save_callback(
    SubGhzTxRx* instance,
    SubGhzTxRxNeedSaveCallback callback,
    void* context) {
    furi_assert(instance);
    instance->need_save_callback = callback;
    instance->need_save_context = context;
}

static void subghz_txrx_tx_stop(SubGhzTxRx* instance) {
    furi_assert(instance);
    furi_assert(instance->txrx_state == SubGhzTxRxStateTx);

    subghz_devices_stop_async_tx(instance->radio_device);
    subghz_transmitter_stop(instance->transmitter);
    subghz_transmitter_free(instance->transmitter);

    if(instance->decoder_result->protocol->type == SubGhzProtocolTypeDynamic) {
        if(instance->need_save_callback) {
            instance->need_save_callback(instance->need_save_context);
        }
    }
    subghz_txrx_idle(instance);
    subghz_txrx_speaker_off(instance);
}

FlipperFormat* subghz_txrx_get_fff_data(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->fff_data;
}

SubGhzSetting* subghz_txrx_get_setting(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->setting;
}

void subghz_txrx_stop(SubGhzTxRx* instance) {
    furi_assert(instance);
    FURI_LOG_I(TAG, "txrx_stop: state=%d worker=%p", (int)instance->txrx_state, (void*)instance->worker);

    switch(instance->txrx_state) {
    case SubGhzTxRxStateTx:
        subghz_txrx_tx_stop(instance);
        subghz_txrx_speaker_unmute(instance);
        break;
    case SubGhzTxRxStateRx:
        subghz_txrx_rx_end(instance, true);
        subghz_txrx_speaker_mute(instance);
        break;

    default:
        break;
    }
    FURI_LOG_I(TAG, "txrx_stop: done");
}

void subghz_txrx_hopper_update(SubGhzTxRx* instance, float stay_threshold) {
    furi_assert(instance);

    switch(instance->hopper_state) {
    case SubGhzHopperStateOFF:
    case SubGhzHopperStatePause:
        return;
    case SubGhzHopperStateRSSITimeOut:
        if(instance->hopper_timeout != 0) {
            instance->hopper_timeout--;
            return;
        }
        break;
    default:
        break;
    }
    if(instance->hopper_state != SubGhzHopperStateRSSITimeOut) {

        float rssi = subghz_devices_get_rssi(instance->radio_device);

        if(rssi > stay_threshold) {
            instance->hopper_timeout = 10;
            instance->hopper_state = SubGhzHopperStateRSSITimeOut;
            return;
        }
    } else {
        instance->hopper_state = SubGhzHopperStateRunning;
    }

    if(instance->hopper_idx_frequency <
       subghz_setting_get_hopper_frequency_count(instance->setting) - 1) {
        instance->hopper_idx_frequency++;
    } else {
        instance->hopper_idx_frequency = 0;
    }

    if(instance->txrx_state == SubGhzTxRxStateRx) {
        subghz_txrx_rx_end(instance, false);
    }
    if(instance->txrx_state == SubGhzTxRxStateIDLE) {
        subghz_receiver_reset(instance->receiver);
        instance->preset->frequency =
            subghz_setting_get_hopper_frequency(instance->setting, instance->hopper_idx_frequency);

        subghz_txrx_rx(instance, instance->preset->frequency);
    }
}

SubGhzHopperState subghz_txrx_hopper_get_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->hopper_state;
}

void subghz_txrx_hopper_set_state(SubGhzTxRx* instance, SubGhzHopperState state) {
    furi_assert(instance);
    instance->hopper_state = state;
}

void subghz_txrx_hopper_unpause(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->hopper_state == SubGhzHopperStatePause) {
        instance->hopper_state = SubGhzHopperStateRunning;
    }
}

void subghz_txrx_hopper_pause(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->hopper_state == SubGhzHopperStateRunning) {
        instance->hopper_state = SubGhzHopperStatePause;
    }
}

void subghz_txrx_speaker_on(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_ibutton);
    }

    if(instance->speaker_state == SubGhzSpeakerStateEnable) {
        if(furi_hal_speaker_acquire(30)) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_speaker);
            }
        } else {
            instance->speaker_state = SubGhzSpeakerStateDisable;
        }
    }
}

void subghz_txrx_speaker_off(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
    }
    if(instance->speaker_state != SubGhzSpeakerStateDisable) {
        if(furi_hal_speaker_is_mine()) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
            }
            furi_hal_speaker_release();
            if(instance->speaker_state == SubGhzSpeakerStateShutdown)
                instance->speaker_state = SubGhzSpeakerStateDisable;
        }
    }
}

void subghz_txrx_speaker_mute(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
    }
    if(instance->speaker_state == SubGhzSpeakerStateEnable) {
        if(furi_hal_speaker_is_mine()) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, NULL);
            }
        }
    }
}

void subghz_txrx_speaker_unmute(SubGhzTxRx* instance) {
    furi_assert(instance);
    if(instance->debug_pin_state) {
        subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_ibutton);
    }
    if(instance->speaker_state == SubGhzSpeakerStateEnable) {
        if(furi_hal_speaker_is_mine()) {
            if(!instance->debug_pin_state) {
                subghz_devices_set_async_mirror_pin(instance->radio_device, &gpio_speaker);
            }
        }
    }
}

void subghz_txrx_speaker_set_state(SubGhzTxRx* instance, SubGhzSpeakerState state) {
    furi_assert(instance);
    instance->speaker_state = state;
}

SubGhzSpeakerState subghz_txrx_speaker_get_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->speaker_state;
}

bool subghz_txrx_load_decoder_by_name_protocol(SubGhzTxRx* instance, const char* name_protocol) {
    furi_assert(instance);
    furi_assert(name_protocol);
    subghz_txrx_ensure_protocol_plugin(instance);
    bool res = false;
    instance->decoder_result =
        subghz_receiver_search_decoder_base_by_name(instance->receiver, name_protocol);
    if(instance->decoder_result) {
        res = true;
    }
    return res;
}

SubGhzProtocolDecoderBase* subghz_txrx_get_decoder(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->decoder_result;
}

bool subghz_txrx_protocol_is_serializable(SubGhzTxRx* instance) {
    furi_assert(instance);
    return (instance->decoder_result->protocol->flag & SubGhzProtocolFlag_Save) ==
           SubGhzProtocolFlag_Save;
}

bool subghz_txrx_protocol_is_transmittable(SubGhzTxRx* instance, bool check_type) {
    furi_assert(instance);
    const SubGhzProtocol* protocol = instance->decoder_result->protocol;

    if((protocol->flag & SubGhzProtocolFlag_Send) != SubGhzProtocolFlag_Send) {
        return false;
    }
    if(check_type && protocol->type != SubGhzProtocolTypeStatic) {
        return false;
    }

    SubGhzGarageTxProtocol tx_protocol;
    if(subghz_garage_tx_protocol_for_name(protocol->name, &tx_protocol)) {
        return true;
    }
    return protocol->encoder->deserialize != NULL;
}

void subghz_txrx_receiver_set_filter(SubGhzTxRx* instance, SubGhzProtocolFlag filter) {
    furi_assert(instance);

    instance->receiver_filter = filter;
    instance->receiver_filter_set = true;

    if(instance->receiver) {
        subghz_receiver_set_filter(instance->receiver, filter);
    }
}

void subghz_txrx_set_rx_callback(
    SubGhzTxRx* instance,
    SubGhzReceiverCallback callback,
    void* context) {

    instance->rx_callback = callback;
    instance->rx_callback_context = context;
    if(instance->receiver) {
        subghz_receiver_set_rx_callback(instance->receiver, callback, context);
    }
}

void subghz_txrx_set_raw_file_encoder_worker_callback_end(
    SubGhzTxRx* instance,
    SubGhzProtocolEncoderRAWCallbackEnd callback,
    void* context) {
    subghz_protocol_raw_file_encoder_worker_set_callback_end(
        (SubGhzProtocolEncoderRAW*)subghz_transmitter_get_protocol_instance(instance->transmitter),
        callback,
        context);
}

static void subghz_txrx_ensure_external_device(SubGhzTxRx* instance) {
    furi_assert(instance);

    if(instance->external_device_loaded) {
        return;
    }

    if(memmgr_get_free_heap() < SUBGHZ_TXRX_EXTERNAL_DEVICE_LOAD_FREE_HEAP) {
        FURI_LOG_W(
            TAG,
            "Refusing to load external device plugin, low free heap (%zu < %d), will retry later",
            memmgr_get_free_heap(),
            SUBGHZ_TXRX_EXTERNAL_DEVICE_LOAD_FREE_HEAP);
        subghz_debug_log_write(
            "Refusing to load external device plugin, low free heap (%zu < %d), will retry later",
            memmgr_get_free_heap(),
            SUBGHZ_TXRX_EXTERNAL_DEVICE_LOAD_FREE_HEAP);
        return;
    }
    instance->external_device_loaded = true;

    FURI_LOG_I(TAG, "Loading external radio device plugin: free heap %zu", memmgr_get_free_heap());
    bool loaded = subghz_garage_devices_load_external();
    FURI_LOG_I(
        TAG,
        "External radio device plugin load %s: free heap %zu",
        loaded ? "succeeded" : "failed",
        memmgr_get_free_heap());
    if(!loaded) {
        subghz_debug_log_write(
            "External radio device plugin load failed: free heap %zu", memmgr_get_free_heap());
    }
}

bool subghz_txrx_radio_device_is_external_connected(SubGhzTxRx* instance, const char* name) {
    furi_assert(instance);
    subghz_txrx_ensure_radio_init(instance);

    subghz_txrx_ensure_external_device(instance);

    bool is_connect = false;
    bool is_otg_enabled = furi_hal_power_is_otg_enabled();

    if(!is_otg_enabled) {
        subghz_txrx_radio_device_power_on(instance);
    }

    const SubGhzDevice* device = subghz_devices_get_by_name(name);
    if(device) {
        is_connect = subghz_devices_is_connect(device);
    }

    if(!is_otg_enabled) {
        subghz_txrx_radio_device_power_off(instance);
    }
    return is_connect;
}

SubGhzRadioDeviceType
    subghz_txrx_radio_device_set(SubGhzTxRx* instance, SubGhzRadioDeviceType radio_device_type) {
    furi_assert(instance);
    subghz_txrx_ensure_radio_init(instance);

    if(radio_device_type == SubGhzRadioDeviceTypeExternalCC1101 &&
       subghz_txrx_radio_device_is_external_connected(instance, SUBGHZ_DEVICE_CC1101_EXT_NAME)) {
        if(instance->radio_device_type != SubGhzRadioDeviceTypeExternalCC1101) {
            subghz_txrx_radio_device_power_on(instance);
            instance->radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
            subghz_devices_begin(instance->radio_device);
            instance->radio_device_type = SubGhzRadioDeviceTypeExternalCC1101;
        }
    } else {
        subghz_txrx_radio_device_power_off(instance);
        if(instance->radio_device_type != SubGhzRadioDeviceTypeInternal) {
            subghz_devices_end(instance->radio_device);
        }
        instance->radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
        instance->radio_device_type = SubGhzRadioDeviceTypeInternal;
    }

    subghz_debug_log_write(
        "radio_device_set: requested %d, now %d",
        (int)radio_device_type,
        (int)instance->radio_device_type);
    return instance->radio_device_type;
}

SubGhzRadioDeviceType subghz_txrx_radio_device_get(SubGhzTxRx* instance) {
    furi_assert(instance);

    subghz_txrx_ensure_radio_init(instance);
    return instance->radio_device_type;
}

float subghz_txrx_radio_device_get_rssi(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_txrx_ensure_radio_init(instance);
    return subghz_devices_get_rssi(instance->radio_device);
}

const char* subghz_txrx_radio_device_get_name(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_txrx_ensure_radio_init(instance);
    return subghz_devices_get_name(instance->radio_device);
}

bool subghz_txrx_radio_device_is_frequency_valid(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);
    subghz_txrx_ensure_radio_init(instance);
    return subghz_devices_is_frequency_valid(instance->radio_device, frequency);
}

bool subghz_txrx_radio_device_is_tx_allowed(SubGhzTxRx* instance, uint32_t frequency) {

    furi_assert(instance);
    UNUSED(frequency);

    return true;
}

void subghz_txrx_set_debug_pin_state(SubGhzTxRx* instance, bool state) {
    furi_assert(instance);
    instance->debug_pin_state = state;
}

bool subghz_txrx_get_debug_pin_state(SubGhzTxRx* instance) {
    furi_assert(instance);
    return instance->debug_pin_state;
}

void subghz_txrx_reset_dynamic_and_custom_btns(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_txrx_ensure_radio_init(instance);
    subghz_garage_env_reset_keeloq(instance->environment);

    if(instance->protocol_plugin && instance->protocol_plugin->faac_slh_reset_prog_mode) {
        instance->protocol_plugin->faac_slh_reset_prog_mode();
    }

    subghz_custom_btns_reset();
}

SubGhzReceiver* subghz_txrx_get_receiver(SubGhzTxRx* instance) {
    furi_assert(instance);
    subghz_txrx_ensure_protocol_plugin(instance);
    return instance->receiver;
}

void subghz_txrx_set_default_preset(SubGhzTxRx* instance, uint32_t frequency) {
    furi_assert(instance);

    const char* default_modulation = "AM650";
    if(frequency == 0) {
        frequency = subghz_setting_get_default_frequency(subghz_txrx_get_setting(instance));
    }
    subghz_txrx_set_preset(instance, default_modulation, frequency, NULL, 0);
}

const char* subghz_txrx_set_preset_internal(
    SubGhzTxRx* instance,
    uint32_t frequency,
    uint8_t index,
    uint8_t tx_power) {
    furi_assert(instance);

    SubGhzSetting* setting = subghz_txrx_get_setting(instance);
    const char* preset_name = subghz_setting_get_preset_name(setting, index);
    subghz_garage_setting_mark_default_frequency(setting, frequency);

    uint8_t* preset_data = subghz_setting_get_preset_data(setting, index);
    size_t preset_data_size = subghz_setting_get_preset_data_size(setting, index);

    subghz_txrx_set_tx_power(preset_data, preset_data_size, tx_power);

    subghz_txrx_set_preset(instance, preset_name, frequency, preset_data, preset_data_size);

    return preset_name;
}
