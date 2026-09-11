#include "../weather_station_app_i.h"

#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define TAG "WeatherSavedProfiles"
#define WEATHER_EDITOR_SAVED_PAGE_SIZE 4U
#define WEATHER_EDITOR_SAVED_EVENT_PREVIOUS 0xFFFFFFFEUL
#define WEATHER_EDITOR_SAVED_EVENT_NEXT 0xFFFFFFFFUL
#define WEATHER_EDITOR_SAVED_EVENT_ORG 0xFFFFFFFCUL
#define WEATHER_EDITOR_SAVED_EVENT_MOD 0xFFFFFFFDUL
#define WEATHER_EDITOR_SAVED_SOURCE_NONE 0U
#define WEATHER_EDITOR_SAVED_SOURCE_ORG 1U
#define WEATHER_EDITOR_SAVED_SOURCE_MOD 2U

static bool weather_editor_has_saved_extension(const char* name) {
    const size_t length = strlen(name);
    if(length < 3U) return false;
    const char* ext = name + length - 3U;
    return ext[0] == '.' && (ext[1] == 'w' || ext[1] == 'W') &&
           (ext[2] == 's' || ext[2] == 'S');
}

static void weather_editor_saved_profiles_clear(WeatherStationApp* app) {
    if(!app) return;
    for(size_t i = 0; i < WEATHER_EDITOR_MAX_SAVED_PROFILES; i++) {
        if(app->saved_profile_paths[i]) {
            furi_string_free(app->saved_profile_paths[i]);
            app->saved_profile_paths[i] = NULL;
        }
    }
    app->saved_profile_count = 0U;
}

static void weather_editor_saved_profile_callback(void* context, uint32_t index) {
    WeatherStationApp* app = context;
    if(!app || !app->view_dispatcher) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void weather_editor_saved_profiles_scan_folder(
    WeatherStationApp* app,
    Storage* storage,
    const char* folder,
    uint32_t page_start,
    uint32_t page_end,
    uint32_t* total_count) {
    if(!app || !storage || !folder || !total_count) return;

    File* directory = storage_file_alloc(storage);
    if(!directory) return;

    const bool opened = storage_dir_open(directory, folder);
    if(opened) {
        FileInfo info;
        char name[128];
        while(storage_dir_read(directory, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info) || !weather_editor_has_saved_extension(name)) continue;

            const uint32_t global_index = *total_count;
            if(global_index >= page_start && global_index < page_end &&
               app->saved_profile_count < WEATHER_EDITOR_SAVED_PAGE_SIZE) {
                const uint8_t page_index = app->saved_profile_count;
                FuriString* path = furi_string_alloc();
                if(path) {
                    furi_string_printf(path, "%s/%s", folder, name);
                    app->saved_profile_paths[page_index] = path;

                    const char* stored_path = furi_string_get_cstr(path);
                    const char* label = strrchr(stored_path, '/');
                    label = label ? label + 1 : stored_path;
                    submenu_add_item(
                        app->submenu,
                        label,
                        page_index,
                        weather_editor_saved_profile_callback,
                        app);
                    app->saved_profile_count++;
                }
            }

            if(*total_count < UINT16_MAX) (*total_count)++;
        }
        storage_dir_close(directory);
    }

    storage_file_free(directory);
}

static uint32_t weather_editor_saved_profiles_scan_page(
    WeatherStationApp* app,
    Storage* storage) {
    const uint32_t page_start =
        (uint32_t)app->saved_profile_page * WEATHER_EDITOR_SAVED_PAGE_SIZE;
    const uint32_t page_end = page_start + WEATHER_EDITOR_SAVED_PAGE_SIZE;
    uint32_t total_count = 0U;

    const char* folder = app->saved_profile_source == WEATHER_EDITOR_SAVED_SOURCE_MOD ?
                             WEATHER_EDITOR_EDITED_PROFILE_FOLDER :
                             WEATHER_EDITOR_RX_PROFILE_FOLDER;
    weather_editor_saved_profiles_scan_folder(
        app, storage, folder, page_start, page_end, &total_count);

    return total_count;
}

static void weather_editor_saved_profiles_build_source_menu(WeatherStationApp* app) {
    if(!app || !weather_station_ensure_submenu(app)) return;

    weather_editor_saved_profiles_clear(app);
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Load saved");
    submenu_add_item(
        app->submenu, "ORG", WEATHER_EDITOR_SAVED_EVENT_ORG, weather_editor_saved_profile_callback, app);
    submenu_add_item(
        app->submenu, "MOD", WEATHER_EDITOR_SAVED_EVENT_MOD, weather_editor_saved_profile_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewSubmenu);
    weather_station_release_inactive_gui_views(app, WeatherStationViewSubmenu);
}

static void weather_editor_saved_profiles_build_list(WeatherStationApp* app) {
    if(!app || !weather_station_ensure_submenu(app)) return;
    if(app->saved_profile_source == WEATHER_EDITOR_SAVED_SOURCE_NONE) {
        weather_editor_saved_profiles_build_source_menu(app);
        return;
    }

    weather_editor_saved_profiles_clear(app);
    submenu_reset(app->submenu);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        submenu_set_header(app->submenu, "SD card error");
        view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewSubmenu);
        weather_station_release_inactive_gui_views(app, WeatherStationViewSubmenu);
        return;
    }

    storage_simply_mkdir(storage, EXT_PATH("apps_data/weather_editor"));
    storage_simply_mkdir(storage, WEATHER_EDITOR_PROFILE_FOLDER);
    storage_simply_mkdir(storage, WEATHER_EDITOR_RX_PROFILE_FOLDER);
    storage_simply_mkdir(storage, WEATHER_EDITOR_EDITED_PROFILE_FOLDER);

    uint32_t total_count = weather_editor_saved_profiles_scan_page(app, storage);

    /* If files were removed while a later page was selected, move to the
       newest valid page and scan once more. */
    const uint32_t page_start =
        (uint32_t)app->saved_profile_page * WEATHER_EDITOR_SAVED_PAGE_SIZE;
    if(total_count > 0U && page_start >= total_count && app->saved_profile_page > 0U) {
        app->saved_profile_page =
            (uint16_t)((total_count - 1U) / WEATHER_EDITOR_SAVED_PAGE_SIZE);
        weather_editor_saved_profiles_clear(app);
        submenu_reset(app->submenu);
        total_count = weather_editor_saved_profiles_scan_page(app, storage);
    }

    furi_record_close(RECORD_STORAGE);

    app->saved_profile_total_count =
        total_count > UINT16_MAX ? UINT16_MAX : (uint16_t)total_count;

    const char* source_label =
        app->saved_profile_source == WEATHER_EDITOR_SAVED_SOURCE_MOD ? "MOD" : "ORG";

    if(total_count == 0U) {
        app->saved_profile_page = 0U;
        char empty_header[32];
        snprintf(empty_header, sizeof(empty_header), "%s - no saved files", source_label);
        submenu_set_header(app->submenu, empty_header);
    } else {
        const uint32_t page_count =
            (total_count + WEATHER_EDITOR_SAVED_PAGE_SIZE - 1U) /
            WEATHER_EDITOR_SAVED_PAGE_SIZE;
        char header[32];
        snprintf(
            header,
            sizeof(header),
            "%s %u/%lu",
            source_label,
            (unsigned)(app->saved_profile_page + 1U),
            (unsigned long)page_count);
        submenu_set_header(app->submenu, header);

        if(app->saved_profile_page > 0U) {
            submenu_add_item(
                app->submenu,
                "< Previous page",
                WEATHER_EDITOR_SAVED_EVENT_PREVIOUS,
                weather_editor_saved_profile_callback,
                app);
        }
        if((uint32_t)app->saved_profile_page + 1U < page_count) {
            submenu_add_item(
                app->submenu,
                "Next page >",
                WEATHER_EDITOR_SAVED_EVENT_NEXT,
                weather_editor_saved_profile_callback,
                app);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewSubmenu);
    weather_station_release_inactive_gui_views(app, WeatherStationViewSubmenu);
}

static void weather_editor_saved_profiles_release_editor_memory(WeatherStationApp* app) {
    if(!app) return;

    weather_station_release_rx_core(app);

    if(app->editor_tx_raw) {
        weather_editor_raw_free(app->editor_tx_raw);
        app->editor_tx_raw = NULL;
    }

    if(app->loaded_profile_preset_data) {
        free(app->loaded_profile_preset_data);
        app->loaded_profile_preset_data = NULL;
    }
    if(app->txrx && app->txrx->preset) {
        app->txrx->preset->data = NULL;
        app->txrx->preset->data_size = 0U;
    }
    app->editor_loaded_from_profile = false;
    app->editor_simulation_mode = false;
    if(app->loaded_profile_path) furi_string_reset(app->loaded_profile_path);
}

void weather_station_scene_saved_profiles_on_enter(void* context) {
    WeatherStationApp* app = context;
    if(!app) return;

    weather_editor_saved_profiles_release_editor_memory(app);
    app->saved_profile_source = WEATHER_EDITOR_SAVED_SOURCE_NONE;
    app->saved_profile_page = 0U;
    weather_editor_saved_profiles_build_source_menu(app);
}

bool weather_station_scene_saved_profiles_on_event(void* context, SceneManagerEvent event) {
    WeatherStationApp* app = context;
    if(!app) return false;

    if(event.type == SceneManagerEventTypeBack &&
       app->saved_profile_source != WEATHER_EDITOR_SAVED_SOURCE_NONE) {
        app->saved_profile_source = WEATHER_EDITOR_SAVED_SOURCE_NONE;
        app->saved_profile_page = 0U;
        weather_editor_saved_profiles_build_source_menu(app);
        return true;
    }

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == WEATHER_EDITOR_SAVED_EVENT_ORG ||
       event.event == WEATHER_EDITOR_SAVED_EVENT_MOD) {
        app->saved_profile_source = event.event == WEATHER_EDITOR_SAVED_EVENT_MOD ?
                                        WEATHER_EDITOR_SAVED_SOURCE_MOD :
                                        WEATHER_EDITOR_SAVED_SOURCE_ORG;
        app->saved_profile_page = 0U;
        weather_editor_saved_profiles_build_list(app);
        return true;
    }

    if(event.event == WEATHER_EDITOR_SAVED_EVENT_PREVIOUS) {
        if(app->saved_profile_page > 0U) app->saved_profile_page--;
        weather_editor_saved_profiles_build_list(app);
        return true;
    }

    if(event.event == WEATHER_EDITOR_SAVED_EVENT_NEXT) {
        const uint32_t next_start =
            ((uint32_t)app->saved_profile_page + 1U) * WEATHER_EDITOR_SAVED_PAGE_SIZE;
        if(next_start < app->saved_profile_total_count) app->saved_profile_page++;
        weather_editor_saved_profiles_build_list(app);
        return true;
    }

    if(event.event >= app->saved_profile_count ||
       !app->saved_profile_paths[event.event]) {
        return false;
    }

    const uint8_t index = (uint8_t)event.event;
    scene_manager_set_scene_state(app->scene_manager, WeatherStationSceneSavedProfiles, index);

    char selected_path[256];
    snprintf(
        selected_path,
        sizeof(selected_path),
        "%s",
        furi_string_get_cstr(app->saved_profile_paths[index]));

    /* Copy the selected path, then move away from the submenu and free the
       complete submenu object. submenu_reset() clears items but may retain the
       dynamic array capacity, which is too expensive when qFlipper is active. */
    if(!weather_station_ensure_widget(app)) {
        furi_string_set(app->editor_result_title, "Out of memory");
        furi_string_set(app->editor_result_text, "Cannot open loading view.");
        return true;
    }
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 29, AlignCenter, AlignCenter, FontPrimary, "Loading...");
    view_dispatcher_switch_to_view(app->view_dispatcher, WeatherStationViewWidget);
    weather_station_release_inactive_gui_views(app, WeatherStationViewWidget);
    weather_editor_saved_profiles_clear(app);


    SubGhzRadioPreset loaded_preset = {
        .name = furi_string_alloc(),
        .frequency = 0U,
        .data = NULL,
        .data_size = 0U,
    };
    uint8_t* loaded_data = NULL;
    bool ok = loaded_preset.name != NULL;

    if(ok) {
        ok = weather_editor_load_profile(
            selected_path,
            &loaded_preset,
            app->editor,
            &loaded_data,
            app->editor_status);
    }

    if(ok) {
        free(app->loaded_profile_preset_data);
        app->loaded_profile_preset_data = loaded_data;
        loaded_data = NULL;

        furi_string_set(app->txrx->preset->name, loaded_preset.name);
        app->txrx->preset->frequency = loaded_preset.frequency;
        app->txrx->preset->data = app->loaded_profile_preset_data;
        app->txrx->preset->data_size = loaded_preset.data_size;

        app->editor_loaded_from_profile = true;
        app->editor_simulation_mode = false;
        if(app->loaded_profile_path) furi_string_set(app->loaded_profile_path, selected_path);
        app->editor_frequency_hz = loaded_preset.frequency;
        app->editor_received_channel = app->editor->channel;
        app->editor_channel_auto = false;
        app->temperature_negative = app->editor->temperature_tenths < 0;
    } else if(!loaded_preset.name) {
        furi_string_set(app->editor_status, "No memory for preset name");
    }


    if(ok) {
        scene_manager_set_scene_state(app->scene_manager, WeatherStationSceneActions, 0);
        scene_manager_next_scene(app->scene_manager, WeatherStationSceneActions);
    } else {
        furi_string_set(app->editor_result_title, "Read error");
        furi_string_set(app->editor_result_text, app->editor_status);
        scene_manager_next_scene(app->scene_manager, WeatherStationSceneActionResult);
    }

    free(loaded_data);
    if(loaded_preset.name) furi_string_free(loaded_preset.name);
    return true;
}

void weather_station_scene_saved_profiles_on_exit(void* context) {
    WeatherStationApp* app = context;
    if(!app) return;
    if(app->submenu) submenu_reset(app->submenu);
    weather_editor_saved_profiles_clear(app);
}
