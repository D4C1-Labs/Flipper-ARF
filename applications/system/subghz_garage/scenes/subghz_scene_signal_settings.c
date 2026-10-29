#include "../subghz_i.h"
#include "subghz/types.h"
#include "../helpers/subghz_custom_event.h"
#include "../helpers/subghz_lib_ext_compat.h"
#include <lib/toolbox/value_index.h>
#include <machine/endian.h>
#include <toolbox/strint.h>
#include <lib/subghz/blocks/generic.h>
#include "../helpers/subghz_generic_global_compat.h"
#include "../helpers/subghz_variable_item_lock_compat.h"
#define TAG "SubGhzSceneSignalSettings"

static uint32_t counter_mode = 0xff;
static uint32_t counter32 = 0x0;
static uint16_t counter16 = 0x0;
static uint8_t cnt_byte_count = 0;
static uint8_t* cnt_byte_ptr = NULL;

static FuriString* byte_input_text;

static uint8_t button = 0x0;
static uint8_t btn_byte_count = 1;
static uint8_t* btn_byte_ptr = NULL;

static uint8_t submenu_called = 0;

#define COUNTER_MODE_COUNT 8
static const char* const counter_mode_text[COUNTER_MODE_COUNT] = {
    "System",
    "Mode 1",
    "Mode 2",
    "Mode 3",
    "Mode 4",
    "Mode 5",
    "Mode 6",
    "Mode 7",
};

static const int32_t counter_mode_value[COUNTER_MODE_COUNT] = {
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
};

typedef struct {
    char* name;
    uint8_t mode_count;
} Protocols;

static Protocols protocols[] = {
    {"Nice FloR-S", 3},
    {"CAME Atomo", 4},
    {"Alutech AT-4N", 3},
    {"KeeLoq", 8},
    {"Phoenix_V2", 3},
};

#define PROTOCOLS_COUNT (sizeof(protocols) / sizeof(Protocols));

void subghz_scene_signal_settings_counter_mode_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, counter_mode_text[index]);
    counter_mode = counter_mode_value[index];

    SubGhz* subghz = variable_item_get_context(item);
    const char* file_path = furi_string_get_cstr(subghz->file_path);

    furi_assert(subghz);
    furi_assert(file_path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);

    if(flipper_format_file_open_existing(fff_data_file, file_path)) {
        if(flipper_format_insert_or_update_uint32(fff_data_file, "CounterMode", &counter_mode, 1)) {
            FURI_LOG_D(TAG, "Successfully updated/inserted CounterMode value %li", counter_mode);
        } else {
            FURI_LOG_E(TAG, "Error update/insert CounterMode value");
        }
    } else {
        FURI_LOG_E(TAG, "Error open file %s for writing", file_path);
    }

    flipper_format_file_close(fff_data_file);
    flipper_format_free(fff_data_file);
    furi_record_close(RECORD_STORAGE);

    if(subghz_key_load(subghz, file_path, false)) {
        FURI_LOG_D(TAG, "Subghz file was successfully reloaded");
    } else {
        FURI_LOG_E(TAG, "Error reloading subghz file");
    }
}

void subghz_scene_signal_settings_byte_input_callback(void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventByteInputDone);
}

void subghz_scene_signal_settings_variable_item_list_enter_callback(void* context, uint32_t index) {
    SubGhz* subghz = context;

    if(index == 1 && cnt_byte_ptr != NULL) {
        submenu_called = 1;
        furi_string_cat_printf(byte_input_text, "%i", subghz_block_generic_global.cnt_length_bit);
        furi_string_cat_str(byte_input_text, "-bits counter in HEX");

        subghz_ensure_byte_input(subghz);
        ByteInput* byte_input = subghz->byte_input;
        byte_input_set_header_text(byte_input, furi_string_get_cstr(byte_input_text));

        byte_input_set_result_callback(
            byte_input,
            subghz_scene_signal_settings_byte_input_callback,
            NULL,
            subghz,
            cnt_byte_ptr,
            cnt_byte_count);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdByteInput);
    }

    if(index == 2 && btn_byte_ptr != NULL) {
        submenu_called = 2;
        furi_string_cat_printf(byte_input_text, "%i", subghz_block_generic_global.btn_length_bit);
        furi_string_cat_str(byte_input_text, "-bits button in HEX");

        subghz_ensure_byte_input(subghz);
        ByteInput* byte_input = subghz->byte_input;
        byte_input_set_header_text(byte_input, furi_string_get_cstr(byte_input_text));

        byte_input_set_result_callback(
            byte_input,
            subghz_scene_signal_settings_byte_input_callback,
            NULL,
            subghz,
            btn_byte_ptr,
            btn_byte_count);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdByteInput);
    }
}

void subghz_scene_signal_settings_on_enter(void* context) {
    SubGhz* subghz = context;

    const char* file_path = furi_string_get_cstr(subghz->file_path);

    furi_assert(subghz);
    furi_assert(file_path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff_data_file = flipper_format_file_alloc(storage);
    FuriString* tmp_text = furi_string_alloc_set_str("");

    uint32_t tmp_counter_mode = 0;
    counter_mode = 0xff;
    uint8_t mode_count = 1;

    if(!flipper_format_file_open_existing(fff_data_file, file_path)) {
        FURI_LOG_E(TAG, "Error open file %s", file_path);
    } else {
        flipper_format_read_string(fff_data_file, "Protocol", tmp_text);

        for(uint8_t i = 0; i < PROTOCOLS_COUNT i++) {
            if(!strcmp(furi_string_get_cstr(tmp_text), protocols[i].name)) {
                mode_count = protocols[i].mode_count;
                if(flipper_format_read_uint32(fff_data_file, "CounterMode", &tmp_counter_mode, 1)) {
                    counter_mode = (uint8_t)tmp_counter_mode;
                } else {
                    counter_mode = 0;
                }
            }
        }
    }
    FURI_LOG_D(TAG, "Loaded CounterMode value %li", counter_mode);

    flipper_format_file_close(fff_data_file);
    flipper_format_free(fff_data_file);
    furi_record_close(RECORD_STORAGE);

    byte_input_text = furi_string_alloc_set_str("Enter ");
    bool counter_not_available = true;
    bool button_not_available = true;

    subghz_ensure_variable_item_list(subghz);
    VariableItemList* variable_item_list = subghz->variable_item_list;
    int32_t value_index;
    VariableItem* item;

    variable_item_list_set_enter_callback(
        variable_item_list,
        subghz_scene_signal_settings_variable_item_list_enter_callback,
        subghz);

    item = variable_item_list_add(
        variable_item_list,
        "Counter Mode",
        mode_count,
        subghz_scene_signal_settings_counter_mode_changed,
        subghz);
    value_index = value_index_int32(counter_mode, counter_mode_value, mode_count);
    variable_item_set_current_value_index(item, value_index);
    variable_item_set_current_value_text(item, counter_mode_text[value_index]);
    subghz_garage_variable_item_set_locked(item, (counter_mode == 0xff), "Not available\nfor this\nprotocol !");

    SubGhzProtocolDecoderBase* decoder = subghz_txrx_get_decoder(subghz->txrx);

    if(subghz_protocol_decoder_base_deserialize(decoder, subghz_txrx_get_fff_data(subghz->txrx)) ==
       SubGhzProtocolStatusOk) {
        subghz_protocol_decoder_base_get_string(decoder, tmp_text);
    } else {
        FURI_LOG_E(TAG, "Cant deserialize this subghz file");
    }

    if(!subghz_block_generic_global.cnt_is_available) {
        counter_mode = 0xff;
        FURI_LOG_D(TAG, "Counter mode and edit not available for this protocol");
    } else {
        counter_not_available = false;

        if(subghz_block_generic_global.cnt_length_bit > 16) {
            counter32 = subghz_block_generic_global.current_cnt;
            furi_string_printf(tmp_text, "%lX", counter32);
            counter32 = __bswap32(counter32);
            cnt_byte_ptr = (uint8_t*)&counter32;
            cnt_byte_count = 4;
        } else {
            counter16 = subghz_block_generic_global.current_cnt;
            furi_string_printf(tmp_text, "%X", counter16);
            counter16 = __bswap16(counter16);
            cnt_byte_ptr = (uint8_t*)&counter16;
            cnt_byte_count = 2;
        }
    }

    item = variable_item_list_add(variable_item_list, "Edit Counter", 1, NULL, subghz);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, furi_string_get_cstr(tmp_text));
    subghz_garage_variable_item_set_locked(item, (counter_not_available), "Not available\nfor this\nprotocol !");

    if(!subghz_block_generic_global.btn_is_available) {
        FURI_LOG_D(TAG, "Button edit not available for this protocol");
    } else {
        button_not_available = false;
        button = subghz_block_generic_global.current_btn;
        furi_string_printf(tmp_text, "%X", button);
        btn_byte_ptr = (uint8_t*)&button;
    }

    item = variable_item_list_add(variable_item_list, "Edit Button", 1, NULL, subghz);
    variable_item_set_current_value_index(item, 0);
    variable_item_set_current_value_text(item, furi_string_get_cstr(tmp_text));
    subghz_garage_variable_item_set_locked(item, (button_not_available), "Not available\nfor this\nprotocol !");

    furi_assert(cnt_byte_ptr);
    furi_assert(cnt_byte_count > 0);
    furi_assert(btn_byte_ptr);

    furi_string_free(tmp_text);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_signal_settings_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventByteInputDone) {
            switch(submenu_called) {

            case 1:
                switch(cnt_byte_count) {
                case 2:

                    counter16 = __bswap16(counter16);
                    subghz_garage_counter_override_set(counter16);
                    subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx));
                    subghz_txrx_stop(subghz->txrx);
                    break;
                case 4:

                    counter32 = __bswap32(counter32);
                    subghz_garage_counter_override_set(counter32);
                    subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx));
                    subghz_txrx_stop(subghz->txrx);
                    break;
                default:
                    break;
                }
                break;

            case 2:
                subghz_garage_button_override_set(button);

                int32_t tmp_counter = subghz_garage_get_rolling_counter_mult();
                subghz_garage_set_rolling_counter_mult(0);
                subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx));
                subghz_txrx_stop(subghz->txrx);

                subghz_garage_set_rolling_counter_mult(tmp_counter);
                break;

            default:
                break;
            }

            scene_manager_previous_scene(subghz->scene_manager);
            return true;

        } else {
            if(event.type == SceneManagerEventTypeBack) {
                scene_manager_previous_scene(subghz->scene_manager);
                return true;
            }
        }
    }
    return false;
}

void subghz_scene_signal_settings_on_exit(void* context) {
    SubGhz* subghz = context;

    furi_assert(subghz);

    variable_item_list_set_selected_item(subghz->variable_item_list, 0);
    variable_item_list_reset(subghz->variable_item_list);
    if(subghz->byte_input) {
        byte_input_set_result_callback(subghz->byte_input, NULL, NULL, NULL, NULL, 0);
        byte_input_set_header_text(subghz->byte_input, "");
    }
    furi_string_free(byte_input_text);
}
