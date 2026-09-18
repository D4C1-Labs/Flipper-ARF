// rolljam_app_i.c
#include "rolljam_app_i.h"
#include "protocols/protocol_items.h"
#include <loader/firmware_api/firmware_api.h>

/* RollJam private SubGhz API table (api/rolljam_api_table.cpp) — exposes the
 * SubGhz lib symbols the runtime plugins import. */
extern const ElfApiInterface* const subghz_application_api_interface;
#include <stdio.h>

#define TAG "RollJamTxRx"

static const char* rolljam_get_registry_plugin_path(RollJamProtocolRegistryFilter filter) {
    return (filter == RollJamProtocolRegistryFilterFM) ?
               APP_ASSETS_PATH("plugins/rolljam_fm_plugin.fal") :
               APP_ASSETS_PATH("plugins/rolljam_am_plugin.fal");
}

void rolljam_unload_protocol_plugin(RollJamTxRx* txrx) {
    furi_check(txrx);

    txrx->protocol_plugin = NULL;
    txrx->protocol_registry = NULL;

    if(txrx->protocol_plugin_manager) {
        plugin_manager_free(txrx->protocol_plugin_manager);
        txrx->protocol_plugin_manager = NULL;
    }

    if(txrx->plugin_resolver) {
        composite_api_resolver_free(txrx->plugin_resolver);
        txrx->plugin_resolver = NULL;
    }
}

static void rolljam_teardown_receiver_stack_for_registry_switch(RollJamApp* app) {
    furi_check(app);
    furi_check(app->txrx);

    if(app->txrx->txrx_state == RollJamTxRxStateRx) {
        rolljam_rx_end(app);
    }

    if(app->txrx->receiver) {
        subghz_receiver_set_rx_callback(app->txrx->receiver, NULL, NULL);
        subghz_receiver_free(app->txrx->receiver);
        app->txrx->receiver = NULL;
    }

    if(app->txrx->worker) {
        if(subghz_worker_is_running(app->txrx->worker)) {
            subghz_worker_stop(app->txrx->worker);
        }
        subghz_worker_free(app->txrx->worker);
        app->txrx->worker = NULL;
    }

    if(app->txrx->radio_device && app->txrx->txrx_state != RollJamTxRxStateTx) {
        subghz_devices_idle(app->txrx->radio_device);
        app->txrx->txrx_state = RollJamTxRxStateIDLE;
    }
}

static bool rolljam_ensure_protocol_registry_plugin(
    RollJamApp* app,
    RollJamProtocolRegistryFilter filter,
    const SubGhzProtocolRegistry** registry) {
    furi_check(app);
    furi_check(app->txrx);
    furi_check(registry);

    *registry = NULL;

    if(!app->txrx->environment) {
        FURI_LOG_E(TAG, "Cannot load protocol plugin without radio environment");
        return false;
    }

    if(app->txrx->protocol_plugin && app->txrx->protocol_plugin->registry &&
       app->txrx->protocol_registry_filter == filter) {
        *registry = app->txrx->protocol_plugin->registry;
        return true;
    }

    if(app->txrx->protocol_plugin || app->txrx->protocol_plugin_manager ||
       app->txrx->plugin_resolver) {
        rolljam_unload_protocol_plugin(app->txrx);
    }
    furi_delay_ms(10);

    CompositeApiResolver* resolver = composite_api_resolver_alloc();
    if(!resolver) {
        FURI_LOG_E(TAG, "Failed to allocate protocol plugin resolver");
        return false;
    }
    composite_api_resolver_add(resolver, firmware_api_interface);
    composite_api_resolver_add(resolver, subghz_application_api_interface);

    const char* appid = (filter == RollJamProtocolRegistryFilterFM) ? 
        ROLLJAM_PROTOCOL_FM_PLUGIN_APP_ID : ROLLJAM_PROTOCOL_AM_PLUGIN_APP_ID;

    PluginManager* manager = plugin_manager_alloc(
        appid,
        ROLLJAM_PROTOCOL_PLUGIN_API_VERSION,
        composite_api_resolver_get(resolver));
    if(!manager) {
        FURI_LOG_E(TAG, "Failed to allocate protocol plugin manager");
        composite_api_resolver_free(resolver);
        return false;
    }

    const char* plugin_path = rolljam_get_registry_plugin_path(filter);
    PluginManagerError error = plugin_manager_load_single(manager, plugin_path);
    if(error != PluginManagerErrorNone) {
        FURI_LOG_E(TAG, "Failed to load protocol plugin %s: %d", plugin_path, (int)error);
        plugin_manager_free(manager);
        composite_api_resolver_free(resolver);
        return false;
    }

    const RollJamProtocolPlugin* plugin = plugin_manager_get_ep(manager, 0U);
    if(!plugin || !plugin->registry) {
        FURI_LOG_E(TAG, "Protocol plugin entry point is invalid");
        plugin_manager_free(manager);
        composite_api_resolver_free(resolver);
        return false;
    }

    if(plugin->filter != filter) {
        FURI_LOG_E(
            TAG, "Protocol plugin filter mismatch (expected %d got %d)", filter, plugin->filter);
        plugin_manager_free(manager);
        composite_api_resolver_free(resolver);
        return false;
    }

    app->txrx->plugin_resolver = resolver;
    app->txrx->protocol_plugin_manager = manager;
    app->txrx->protocol_plugin = plugin;
    app->txrx->protocol_registry_filter = filter;
    *registry = plugin->registry;
    return true;
}

bool rolljam_refresh_protocol_registry(RollJamApp* app, bool ensure_receiver_ready) {
    furi_check(app);
    furi_check(app->txrx);

    if(!app->txrx->environment || !app->txrx->preset) {
        return true;
    }

    RollJamProtocolRegistryFilter filter = rolljam_get_protocol_registry_filter_for_preset(
        app->txrx->preset->data, app->txrx->preset->data_size);
    bool filter_changed = !app->txrx->protocol_plugin ||
                          (app->txrx->protocol_registry_filter != filter);

    if(filter_changed) {
        rolljam_teardown_receiver_stack_for_registry_switch(app);
    } else if(ensure_receiver_ready && !app->txrx->receiver) {
        rolljam_teardown_receiver_stack_for_registry_switch(app);
    }

    const SubGhzProtocolRegistry* registry = NULL;
    if(!rolljam_ensure_protocol_registry_plugin(app, filter, &registry) || !registry) {
        FURI_LOG_E(
            TAG,
            "Failed to resolve %s protocol registry plugin",
            rolljam_get_protocol_registry_filter_name(filter));
        return false;
    }

    const bool registry_already_bound = (app->txrx->protocol_registry == registry);
    if(!registry_already_bound) {
        FURI_LOG_I(
            TAG,
            "Using %s protocol registry (%zu protocols)",
            rolljam_get_protocol_registry_filter_name(filter),
            registry->size);
        subghz_environment_set_protocol_registry(app->txrx->environment, registry);
        app->txrx->protocol_registry = registry;
    }

    if(!ensure_receiver_ready) {
        return true;
    }

    if(app->txrx->receiver) {
        return true;
    }

    app->txrx->receiver = subghz_receiver_alloc_init(app->txrx->environment);
    if(!app->txrx->receiver) {
        FURI_LOG_E(
            TAG,
            "Failed to allocate receiver for %s registry",
            rolljam_get_protocol_registry_filter_name(filter));
        return false;
    }

    subghz_receiver_set_filter(app->txrx->receiver, SubGhzProtocolFlag_Decodable);
    return true;
}

bool rolljam_apply_protocol_registry_for_preset_data(
    RollJamApp* app,
    const uint8_t* preset_data,
    size_t preset_data_size) {
    furi_check(app);
    furi_check(app->txrx);

    if(!app->txrx->environment) {
        return false;
    }

    RollJamProtocolRegistryFilter filter =
        rolljam_get_protocol_registry_filter_for_preset(preset_data, preset_data_size);

    bool filter_changed = !app->txrx->protocol_plugin ||
                          (app->txrx->protocol_registry_filter != filter);

    if(filter_changed) {
        rolljam_teardown_receiver_stack_for_registry_switch(app);
    }

    const SubGhzProtocolRegistry* registry = NULL;
    if(!rolljam_ensure_protocol_registry_plugin(app, filter, &registry) || !registry) {
        FURI_LOG_E(
            TAG,
            "Failed to resolve %s registry plugin for preset apply",
            rolljam_get_protocol_registry_filter_name(filter));
        return false;
    }

    if(app->txrx->protocol_registry == registry) {
        return true;
    }

    FURI_LOG_I(
        TAG,
        "Switching active protocol registry to %s (%zu protocols)",
        rolljam_get_protocol_registry_filter_name(filter),
        registry->size);
    subghz_environment_set_protocol_registry(app->txrx->environment, registry);
    app->txrx->protocol_registry = registry;
    return true;
}

void rolljam_preset_init(
    void* context,
    const char* preset_name,
    uint32_t frequency,
    uint8_t* preset_data,
    size_t preset_data_size) {
    furi_check(context);
    RollJamApp* app = context;
    furi_string_set(app->txrx->preset->name, preset_name);
    app->txrx->preset->frequency = frequency;
    app->txrx->preset->data = preset_data;
    app->txrx->preset->data_size = preset_data_size;
}

void rolljam_get_frequency_modulation_str(
    RollJamApp* app,
    char* frequency,
    size_t frequency_size,
    char* modulation,
    size_t modulation_size) {
    furi_check(app);

    if(frequency && frequency_size > 0) {
        unsigned long mhz = (unsigned long)((app->txrx->preset->frequency / 1000000UL) % 1000UL);
        unsigned long khz = (unsigned long)((app->txrx->preset->frequency / 10000UL) % 100UL);
        snprintf(frequency, frequency_size, "%03lu.%02lu", mhz, khz);
    }

    if(modulation && modulation_size > 0) {
        snprintf(
            modulation, modulation_size, "%.2s", furi_string_get_cstr(app->txrx->preset->name));
    }
}

void rolljam_get_frequency_modulation(
    RollJamApp* app,
    FuriString* frequency,
    FuriString* modulation) {
    furi_check(app);

    char frequency_buf[16] = {0};
    char modulation_buf[8] = {0};
    rolljam_get_frequency_modulation_str(
        app, frequency_buf, sizeof(frequency_buf), modulation_buf, sizeof(modulation_buf));

    if(frequency != NULL) {
        furi_string_set_str(frequency, frequency_buf);
    }
    if(modulation != NULL) {
        furi_string_set_str(modulation, modulation_buf);
    }
}

void rolljam_begin(RollJamApp* app, uint8_t* preset_data) {
    furi_check(app);
    if(!app->txrx->radio_device) {
        FURI_LOG_W(TAG, "begin requested without radio device");
        app->txrx->txrx_state = RollJamTxRxStateIDLE;
        return;
    }
    subghz_devices_reset(app->txrx->radio_device);
    subghz_devices_idle(app->txrx->radio_device);
    subghz_devices_load_preset(app->txrx->radio_device, FuriHalSubGhzPresetCustom, preset_data);
    app->txrx->txrx_state = RollJamTxRxStateIDLE;
}

uint32_t rolljam_rx(RollJamApp* app, uint32_t frequency) {
    furi_check(app);
    furi_check(app->txrx);
    if(!app->radio_initialized || !app->txrx->radio_device || !app->txrx->worker) {
        FURI_LOG_E(
            TAG,
            "RX start rejected (radio_initialized=%d, radio=%p, worker=%p)",
            app->radio_initialized,
            app->txrx->radio_device,
            app->txrx->worker);
        app->txrx->txrx_state = RollJamTxRxStateIDLE;
        return 0;
    }

    if(!subghz_devices_is_frequency_valid(app->txrx->radio_device, frequency)) {
        furi_crash("RollJam: Incorrect RX frequency.");
    }
    if(app->txrx->txrx_state == RollJamTxRxStateRx ||
       app->txrx->txrx_state == RollJamTxRxStateSleep) {
        FURI_LOG_W(TAG, "RX start ignored in state %d", app->txrx->txrx_state);
        return app->txrx->preset ? app->txrx->preset->frequency : 0;
    }

    subghz_devices_idle(app->txrx->radio_device);
    uint32_t value = subghz_devices_set_frequency(app->txrx->radio_device, frequency);
    subghz_devices_flush_rx(app->txrx->radio_device);
    subghz_devices_set_rx(app->txrx->radio_device);

    subghz_devices_start_async_rx(
        app->txrx->radio_device, subghz_worker_rx_callback, app->txrx->worker);

    subghz_worker_start(app->txrx->worker);
    app->txrx->txrx_state = RollJamTxRxStateRx;
    return value;
}

void rolljam_idle(RollJamApp* app) {
    furi_check(app);
    furi_check(app->txrx->txrx_state != RollJamTxRxStateSleep);
    if(app->txrx->radio_device) {
        subghz_devices_idle(app->txrx->radio_device);
    } else {
        FURI_LOG_W(TAG, "idle requested without radio device");
    }
    app->txrx->txrx_state = RollJamTxRxStateIDLE;
}

void rolljam_rx_end(RollJamApp* app) {
    furi_check(app);
    if(!app->txrx || app->txrx->txrx_state != RollJamTxRxStateRx) {
        return;
    }

    if(app->txrx->worker && subghz_worker_is_running(app->txrx->worker)) {
        subghz_worker_stop(app->txrx->worker);
    }

    if(app->txrx->radio_device) {
        subghz_devices_stop_async_rx(app->txrx->radio_device);
        subghz_devices_idle(app->txrx->radio_device);
    }

    app->txrx->txrx_state = RollJamTxRxStateIDLE;
}

void rolljam_sleep(RollJamApp* app) {
    furi_check(app);
    subghz_devices_sleep(app->txrx->radio_device);
    app->txrx->txrx_state = RollJamTxRxStateSleep;
}

void rolljam_release_shared_radio_state(RollJamApp* app) {
    furi_check(app);
    furi_check(app->txrx);

    if(app->rolljam_receiver) {
        rolljam_view_receiver_reset_menu(app->rolljam_receiver);
    }

    rolljam_radio_deinit(app);
}

void rolljam_rx_stack_suspend_for_tx(RollJamApp* app) {
    if(!app || !app->radio_initialized) {
        return;
    }

    if(app->txrx->txrx_state == RollJamTxRxStateRx) {
        rolljam_rx_end(app);
    }

    if(app->txrx->worker && subghz_worker_is_running(app->txrx->worker)) {
        subghz_worker_stop(app->txrx->worker);
    }

    if(app->txrx->receiver) {
        subghz_receiver_set_rx_callback(app->txrx->receiver, NULL, NULL);
    }

    if(app->txrx->radio_device && app->txrx->txrx_state != RollJamTxRxStateTx) {
        subghz_devices_idle(app->txrx->radio_device);
        app->txrx->txrx_state = RollJamTxRxStateIDLE;
    }
}

void rolljam_rx_stack_resume_after_tx(RollJamApp* app) {
    if(!app || !app->radio_initialized || !app->txrx->environment) {
        return;
    }

    if(!rolljam_refresh_protocol_registry(app, true)) {
        FURI_LOG_E(TAG, "rx_stack_resume: failed to restore RX stack");
    }
}

void rolljam_hopper_update(RollJamApp* app) {
    furi_check(app);

    switch(app->txrx->hopper_state) {
    case RollJamHopperStateOFF:
    case RollJamHopperStatePause:
        return;
    case RollJamHopperStateRSSITimeOut:
        if(app->txrx->hopper_timeout != 0) {
            app->txrx->hopper_timeout--;
            return;
        }
        break;
    default:
        break;
    }
    float rssi = -127.0f;
    if(app->txrx->hopper_state != RollJamHopperStateRSSITimeOut) {
        rssi = subghz_devices_get_rssi(app->txrx->radio_device);

        if(rssi > -90.0f) {
            app->txrx->hopper_timeout = 10;
            app->txrx->hopper_state = RollJamHopperStateRSSITimeOut;
            return;
        }
    } else {
        app->txrx->hopper_state = RollJamHopperStateRunning;
    }

    const size_t hopper_count = subghz_setting_get_hopper_frequency_count(app->setting);
    if(hopper_count == 0) {
        app->txrx->hopper_state = RollJamHopperStateOFF;
        app->txrx->hopper_idx_frequency = 0;
        return;
    }
    if(app->txrx->hopper_idx_frequency < hopper_count - 1) {
        app->txrx->hopper_idx_frequency++;
    } else {
        app->txrx->hopper_idx_frequency = 0;
    }

    if(app->txrx->txrx_state == RollJamTxRxStateRx) {
        rolljam_rx_end(app);
    }
    if(app->txrx->txrx_state == RollJamTxRxStateIDLE && app->txrx->receiver) {
        subghz_receiver_reset(app->txrx->receiver);
        app->txrx->preset->frequency =
            subghz_setting_get_hopper_frequency(app->setting, app->txrx->hopper_idx_frequency);
        rolljam_rx(app, app->txrx->preset->frequency);
    }
}

void rolljam_tx(RollJamApp* app, uint32_t frequency) {
    furi_check(app);
    if(!subghz_devices_is_frequency_valid(app->txrx->radio_device, frequency)) {
        return;
    }

    furi_check(app->txrx->txrx_state == RollJamTxRxStateIDLE);

    subghz_devices_idle(app->txrx->radio_device);
    subghz_devices_set_frequency(app->txrx->radio_device, frequency);
    subghz_devices_set_tx(app->txrx->radio_device);

    app->txrx->txrx_state = RollJamTxRxStateTx;
}

void rolljam_tx_stop(RollJamApp* app) {
    furi_check(app);
    furi_check(app->txrx->txrx_state == RollJamTxRxStateTx);

    subghz_devices_idle(app->txrx->radio_device);
    app->txrx->txrx_state = RollJamTxRxStateIDLE;
}
