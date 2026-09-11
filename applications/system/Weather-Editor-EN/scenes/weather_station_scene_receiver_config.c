#include "../weather_station_app_i.h"
#include <string.h>

enum WSSettingIndex {
    WSSettingIndexFrequency,
    WSSettingIndexHopping,
    WSSettingIndexModulation,
    WSSettingIndexLock,
};

#define HOPPING_COUNT 2
#define WEATHER_EDITOR_RECEIVER_CONFIG_REFRESH 150U
const char* const hopping_text[HOPPING_COUNT] = {
    "OFF",
    "ON",
};
const uint32_t hopping_value[HOPPING_COUNT] = {
    WSHopperStateOFF,
    WSHopperStateRunnig,
};

uint8_t weather_station_scene_receiver_config_next_frequency(const uint32_t value, void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->setting) return 0;
    uint8_t index = 0;
    for(uint8_t i = 0; i < subghz_setting_get_frequency_count(app->setting); i++) {
        if(value == subghz_setting_get_frequency(app->setting, i)) {
            index = i;
            break;
        } else {
            index = subghz_setting_get_frequency_default_index(app->setting);
        }
    }
    return index;
}

uint8_t weather_station_scene_receiver_config_next_preset(const char* preset_name, void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->setting || !preset_name) return 0;
    uint8_t index = 0;
    for(uint8_t i = 0; i < subghz_setting_get_preset_count(app->setting); i++) {
        if(!strcmp(subghz_setting_get_preset_name(app->setting, i), preset_name)) {
            index = i;
            break;
        } else {
            //  index = subghz_setting_get_frequency_default_index(app ->setting);
        }
    }
    return index;
}

uint8_t weather_station_scene_receiver_config_hopper_value_index(
    const uint32_t value,
    const uint32_t values[],
    uint8_t values_count,
    void* context) {
    UNUSED(context);
    if(!values || values_count == 0U) return 0U;
    return value == values[0] ? 0U : 1U;
}

static void weather_station_scene_receiver_config_set_frequency(VariableItem* item) {
    if(!item) return;
    WeatherStationApp* app = variable_item_get_context(item);
    if(!app || !app->txrx || !app->txrx->preset || !app->setting) return;
    const uint8_t count = subghz_setting_get_frequency_count(app->setting);
    if(count == 0U) return;
    uint8_t index = variable_item_get_current_value_index(item);
    if(index >= count) index = subghz_setting_get_frequency_default_index(app->setting);
    if(index >= count) index = 0U;

    if(app->txrx->hopper_state == WSHopperStateOFF) {
        char text_buf[10] = {0};
        snprintf(
            text_buf,
            sizeof(text_buf),
            "%lu.%02lu",
            subghz_setting_get_frequency(app->setting, index) / 1000000,
            (subghz_setting_get_frequency(app->setting, index) % 1000000) / 10000);
        variable_item_set_current_value_text(item, text_buf);
        app->txrx->preset->frequency = subghz_setting_get_frequency(app->setting, index);
    } else {
        variable_item_set_current_value_index(
            item, subghz_setting_get_frequency_default_index(app->setting));
    }
}

static void weather_station_scene_receiver_config_set_preset(VariableItem* item) {
    if(!item) return;
    WeatherStationApp* app = variable_item_get_context(item);
    if(!app || !app->txrx || !app->txrx->preset || !app->setting) return;
    const uint8_t count = subghz_setting_get_preset_count(app->setting);
    if(count == 0U) return;
    uint8_t index = variable_item_get_current_value_index(item);
    if(index >= count) index = 0U;
    variable_item_set_current_value_text(
        item, subghz_setting_get_preset_name(app->setting, index));
    ws_preset_init(
        app,
        subghz_setting_get_preset_name(app->setting, index),
        app->txrx->preset->frequency,
        subghz_setting_get_preset_data(app->setting, index),
        subghz_setting_get_preset_data_size(app->setting, index));
}

static void weather_station_scene_receiver_config_set_hopping_running(VariableItem* item) {
    if(!item) return;
    WeatherStationApp* app = variable_item_get_context(item);
    if(!app || !app->txrx || !app->txrx->preset || !app->setting || !app->scene_manager) return;
    uint8_t index = variable_item_get_current_value_index(item);
    if(index >= HOPPING_COUNT) index = 0U;
    variable_item_set_current_value_text(item, hopping_text[index]);
    app->txrx->hopper_state = hopping_value[index];
    if(app->txrx->hopper_state == WSHopperStateOFF) {
        app->txrx->preset->frequency = subghz_setting_get_default_frequency(app->setting);
    }
    view_dispatcher_send_custom_event(
        app->view_dispatcher, WEATHER_EDITOR_RECEIVER_CONFIG_REFRESH);
}

static void
    weather_station_scene_receiver_config_var_list_enter_callback(void* context, uint32_t index) {
    WeatherStationApp* app = context;
    if(!app || !app->view_dispatcher) return;
    if(index == WSSettingIndexLock) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WSCustomEventSceneSettingLock);
    }
}

static void weather_station_scene_receiver_config_build_list(WeatherStationApp* app) {
    if(!app || !app->variable_item_list || !app->setting || !app->txrx ||
       !app->txrx->preset) {
        return;
    }
    VariableItemList* list = app->variable_item_list;
    variable_item_list_reset(list);
    VariableItem* item;
    uint8_t value_index;

    item = variable_item_list_add(
        list,
        "Frequency:",
        subghz_setting_get_frequency_count(app->setting),
        weather_station_scene_receiver_config_set_frequency,
        app);
    value_index =
        weather_station_scene_receiver_config_next_frequency(app->txrx->preset->frequency, app);
    variable_item_set_current_value_index(item, value_index);
    char text_buf[10] = {0};
    if(app->txrx->hopper_state == WSHopperStateOFF) {
        snprintf(
            text_buf,
            sizeof(text_buf),
            "%lu.%02lu",
            subghz_setting_get_frequency(app->setting, value_index) / 1000000,
            (subghz_setting_get_frequency(app->setting, value_index) % 1000000) / 10000);
    } else {
        snprintf(text_buf, sizeof(text_buf), " -----");
    }
    variable_item_set_current_value_text(item, text_buf);

    item = variable_item_list_add(
        list,
        "Hopping:",
        HOPPING_COUNT,
        weather_station_scene_receiver_config_set_hopping_running,
        app);
    value_index = weather_station_scene_receiver_config_hopper_value_index(
        app->txrx->hopper_state, hopping_value, HOPPING_COUNT, app);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, hopping_text[value_index]);

    item = variable_item_list_add(
        list,
        "Modulation:",
        subghz_setting_get_preset_count(app->setting),
        weather_station_scene_receiver_config_set_preset,
        app);
    value_index = weather_station_scene_receiver_config_next_preset(
        furi_string_get_cstr(app->txrx->preset->name), app);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(
        item, subghz_setting_get_preset_name(app->setting, value_index));

    variable_item_list_add(list, "Lock controls", 1, NULL, NULL);
    variable_item_list_set_enter_callback(
        list, weather_station_scene_receiver_config_var_list_enter_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewVariableItemList);
    weather_station_release_inactive_gui_views(app, WeatherStationViewVariableItemList);
}

void weather_station_scene_receiver_config_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !app->scene_manager || !app->view_dispatcher ||
       !weather_station_ensure_radio_core(app) ||
       !weather_station_ensure_variable_item_list(app)) {
        return;
    }
    weather_station_scene_receiver_config_build_list(app);
}

bool weather_station_scene_receiver_config_on_event(void* context, SceneManagerEvent event) {
    WeatherStationApp* app = context;
    if(!app || !app->scene_manager) return false;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WEATHER_EDITOR_RECEIVER_CONFIG_REFRESH) {
            const uint8_t selected = app->variable_item_list ?
                                         variable_item_list_get_selected_item_index(
                                             app->variable_item_list) :
                                         0U;
            weather_station_scene_receiver_config_build_list(app);
            if(app->variable_item_list) variable_item_list_set_selected_item(
                app->variable_item_list, selected);
            consumed = true;
        } else if(event.event == WSCustomEventSceneSettingLock) {
            app->lock = WSLockOn;
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }
    return consumed;
}

void weather_station_scene_receiver_config_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(app && app->variable_item_list) {
        variable_item_list_set_selected_item(app->variable_item_list, 0);
        variable_item_list_reset(app->variable_item_list);
    }
}
