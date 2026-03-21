#include "rolljam_scene.h"

// ============================================================
// Menu scene: select frequency, modulation, start attack
// ============================================================

static uint8_t get_min_offset_index(uint8_t mod_index) {
    if(mod_index == ModIndex_AM270) return JamOffIndex_1000k;
    return JamOffIndex_300k;
}

static void enforce_min_offset(RollJamApp* app, VariableItem* offset_item) {
    uint8_t min_idx = get_min_offset_index(app->mod_index);
    if(app->jam_offset_index < min_idx) {
        app->jam_offset_index = min_idx;
        app->jam_offset_hz    = jam_offset_values[min_idx];
        if(offset_item) {
            variable_item_set_current_value_index(offset_item, min_idx);
            variable_item_set_current_value_text(offset_item, jam_offset_names[min_idx]);
        }
        FURI_LOG_I(TAG, "Menu: offset ajustado a %s para AM270",
                   jam_offset_names[min_idx]);
    }
}

static VariableItem* s_offset_item = NULL;

static void menu_freq_changed(VariableItem* item) {
    RollJamApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->freq_index = index;
    app->frequency  = freq_values[index];
    variable_item_set_current_value_text(item, freq_names[index]);
}

static void menu_mod_changed(VariableItem* item) {
    RollJamApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->mod_index = index;
    variable_item_set_current_value_text(item, mod_names[index]);

    enforce_min_offset(app, s_offset_item);
}

static void menu_jam_offset_changed(VariableItem* item) {
    RollJamApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);

    uint8_t min_idx = get_min_offset_index(app->mod_index);
    if(index < min_idx) {
        index = min_idx;
        variable_item_set_current_value_index(item, index);
    }

    app->jam_offset_index = index;
    app->jam_offset_hz    = jam_offset_values[index];
    variable_item_set_current_value_text(item, jam_offset_names[index]);
}

static void menu_hw_changed(VariableItem* item) {
    RollJamApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->hw_index = index;
    variable_item_set_current_value_text(item, hw_names[index]);
}

static void menu_enter_callback(void* context, uint32_t index) {
    RollJamApp* app = context;
    if(index == 4) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RollJamEventStartAttack);
    }
}

void rolljam_scene_menu_on_enter(void* context) {
    RollJamApp* app = context;

    variable_item_list_reset(app->var_item_list);

    // --- Frequency ---
    VariableItem* freq_item = variable_item_list_add(
        app->var_item_list,
        "Frequency",
        FreqIndex_COUNT,
        menu_freq_changed,
        app);
    variable_item_set_current_value_index(freq_item, app->freq_index);
    variable_item_set_current_value_text(freq_item, freq_names[app->freq_index]);

    // --- Modulation ---
    VariableItem* mod_item = variable_item_list_add(
        app->var_item_list,
        "Modulation",
        ModIndex_COUNT,
        menu_mod_changed,
        app);
    variable_item_set_current_value_index(mod_item, app->mod_index);
    variable_item_set_current_value_text(mod_item, mod_names[app->mod_index]);

    // --- Jam Offset ---
    VariableItem* offset_item = variable_item_list_add(
        app->var_item_list,
        "Jam Offset",
        JamOffIndex_COUNT,
        menu_jam_offset_changed,
        app);

    s_offset_item = offset_item;
    enforce_min_offset(app, offset_item);

    variable_item_set_current_value_index(offset_item, app->jam_offset_index);
    variable_item_set_current_value_text(offset_item, jam_offset_names[app->jam_offset_index]);

    // --- Hardware ---
    VariableItem* hw_item = variable_item_list_add(
        app->var_item_list,
        "Hardware",
        HwIndex_COUNT,
        menu_hw_changed,
        app);
    variable_item_set_current_value_index(hw_item, app->hw_index);
    variable_item_set_current_value_text(hw_item, hw_names[app->hw_index]);

    // --- Start button ---
    variable_item_list_add(
        app->var_item_list,
        ">> START ATTACK <<",
        0,
        NULL,
        app);

    variable_item_list_set_enter_callback(
        app->var_item_list, menu_enter_callback, app);

    view_dispatcher_switch_to_view(
        app->view_dispatcher, RollJamViewVarItemList);
}

bool rolljam_scene_menu_on_event(void* context, SceneManagerEvent event) {
    RollJamApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RollJamEventStartAttack) {
            enforce_min_offset(app, NULL);

            memset(&app->signal_first,  0, sizeof(RawSignal));
            memset(&app->signal_second, 0, sizeof(RawSignal));

            scene_manager_next_scene(
                app->scene_manager, RollJamSceneAttackPhase1);
            return true;
        }
    }
    return false;
}

void rolljam_scene_menu_on_exit(void* context) {
    RollJamApp* app = context;
    s_offset_item = NULL;
    variable_item_list_reset(app->var_item_list);
}
