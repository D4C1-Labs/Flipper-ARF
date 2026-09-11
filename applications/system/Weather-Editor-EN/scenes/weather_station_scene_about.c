#include "../weather_station_app_i.h"
#include "../helpers/weather_station_types.h"
#include "../protocols/protocol_items.h"

void weather_station_scene_about_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    WeatherStationApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void weather_station_scene_about_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app || !weather_station_ensure_widget(app)) return;

    FuriString* temp_str = furi_string_alloc();
    furi_string_printf(temp_str, "\e#Information\n");
    furi_string_cat_printf(temp_str, "Version: %s\n", FAP_VERSION);
    furi_string_cat_printf(temp_str, "Author: %s\n\n", WS_DEVELOPED);


    furi_string_cat_printf(temp_str, "\e#RX protocols\n");
    for(size_t i = 0U; i < weather_station_protocol_registry.size; i++) {
        const SubGhzProtocol* protocol = weather_station_protocol_registry.items[i];
        if(protocol && protocol->name) furi_string_cat_printf(temp_str, "%s\n", protocol->name);
    }

    widget_add_text_box_element(
        app->widget,
        0,
        0,
        128,
        14,
        AlignCenter,
        AlignBottom,
        "\e#\e!                                                      \e!\n",
        false);
    widget_add_text_box_element(
        app->widget,
        0,
        2,
        128,
        14,
        AlignCenter,
        AlignBottom,
        "\e#\e!         Weather Editor       \e!\n",
        false);
    widget_add_text_scroll_element(app->widget, 0, 16, 128, 50, furi_string_get_cstr(temp_str));
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "", weather_station_scene_about_widget_callback, app);
    app->lab_about_taps = 0U;
    app->lab_about_last_tick = 0U;
    furi_string_free(temp_str);

    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewWidget);
    weather_station_release_inactive_gui_views(app, WeatherStationViewWidget);
}

bool weather_station_scene_about_on_event(void* context, SceneManagerEvent event) {
    WeatherStationApp* app = context;
    if(!app) return false;
    if(event.type == SceneManagerEventTypeCustom && event.event == GuiButtonTypeCenter) {
        const uint32_t now = furi_get_tick();
        if(app->lab_about_last_tick == 0U ||
           (now - app->lab_about_last_tick) > furi_ms_to_ticks(5000U)) app->lab_about_taps = 0U;
        app->lab_about_last_tick = now;
        if(app->lab_about_taps < 7U) app->lab_about_taps++;
        if(app->lab_about_taps >= 7U) {
            app->lab_about_taps = 0U;
            scene_manager_next_scene(app->scene_manager, WeatherStationSceneLabPin);
        }
        return true;
    }
    return false;
}

void weather_station_scene_about_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(app->widget) widget_reset(app->widget);
}
