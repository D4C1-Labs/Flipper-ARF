/**
 * Scene: CarEmulate
 * Custom automotive-key emulation GUI ported from ProtoPirate.
 * Activated when SubGhzLastSettings::custom_car_emulate == true and the
 * user presses "Emulate" on a saved dynamic protocol.
 *
 * Flow:
 *   SavedMenu → Emulate → (custom_car_emulate?) CarEmulate : Transmitter
 */
#include "../subghz_i.h"
#include "../views/subghz_car_emulate.h"
#include "../helpers/subghz_button_labels.h"
#include "../helpers/subghz_custom_event.h"
#include <lib/subghz/blocks/generic.h>
#include <notification/notification_messages.h>
#include "../helpers/subghz_txrx_i.h"
#include <lib/subghz/blocks/custom_btn_i.h>
#include <string.h>

#define TAG           "SubGhzSceneCarEmulate"
#define MIN_TX_TICKS  66U   /* ~666 ms at 100 ms tick */

/* ── Per-session state (heap, freed on exit) ─────────────────────────────── */
typedef struct {
    /* Signal metadata read from fff_data */
    char     protocol_name[48];
    uint32_t serial;
    uint8_t  original_button;
    uint32_t original_counter;
    uint32_t current_counter;
    uint32_t freq;
    char     preset_short[12];  /* "AM650", "FM476", … */

    bool     has_serial;
    bool     has_counter;

    /* TX state */
    bool     is_transmitting;
    bool     stop_pending;      /* stop requested before MIN_TX_TICKS elapsed */
    uint32_t tx_start_tick;

    /* Resolved custom button repeated while the physical key is held */
    uint8_t  pending_button;
    uint8_t  max_custom_button;
} CarEmulateState;

static CarEmulateState* s_state = NULL;

/* ═══════════════════════════════════════════════════════════════════════════
 * Button mapping  (protocol-name → InputKey → button byte)
 * Ported verbatim from protopirate_scene_emulate.c
 * ═════════════════════════════════════════════════════════════════════════*/
//static uint8_t car_emulate_map_button(
//    const char* protocol,
//    InputKey    key,
//    uint8_t     original) {

    /* Land Rover V0 */
//    if(strstr(protocol, "Land Rover")) {
//        switch(key) {
//        case InputKeyUp:   return 0x02; /* Lock   */
//        case InputKeyOk:   return 0x04; /* Unlock */
//        default:           return original;
//        }
//    }
    /* Mazda */
//    if(strstr(protocol, "Mazda")) {
//        switch(key) {
//        case InputKeyUp:    return 0x01;
//        case InputKeyOk:    return 0x02;
//        case InputKeyDown:  return 0x04;
//        case InputKeyRight: return 0x08;
//        default:            return original;
//        }
//    }
    /* PSA */
//    if(strstr(protocol, "PSA")) {
//        switch(key) {
//        case InputKeyUp:    return 0x1;
//        case InputKeyOk:    return 0x2;
//        case InputKeyDown:  return 0x4;
//        case InputKeyLeft:  return 0x8;
//        default:            return original;
//        }
//    }
    /* VAG */
//    if(strstr(protocol, "VAG")) {
//        if(original == 0x10 || original == 0x20 || original == 0x40) {
//            switch(key) {
//            case InputKeyUp:   return 0x20;
//            case InputKeyOk:   return 0x10;
//            case InputKeyDown: return 0x40;
//            default:           return original;
//            }
//        }
//        switch(key) {
//        case InputKeyUp:    return 0x2;
//        case InputKeyOk:    return 0x1;
//        case InputKeyDown:  return 0x4;
//        case InputKeyLeft:  return 0x8;
//        case InputKeyRight: return 0x3;
//        default:            return original;
//        }
//    }
    /* Honda Static */
//    if(strstr(protocol, "Honda Static")) {
//        switch(key) {
//        case InputKeyUp:    return 0x1;
//        case InputKeyOk:    return 0x2;
//        case InputKeyDown:  return 0x4;
//        case InputKeyRight: return 0x5;
//        case InputKeyLeft:  return 0x8;
//        default:            return original;
//        }
//    }
    /* Ford */
//    if(strstr(protocol, "Ford")) {
//        switch(key) {
//        case InputKeyLeft:  return 0x1;
//        case InputKeyUp:    return 0x2;
//        case InputKeyOk:    return 0x4;
//        case InputKeyDown:  return 0x8;
//        case InputKeyRight: return 0x10;
//        default:            return original;
//        }
//    }
    /* Chrysler */
//    if(strstr(protocol, "Chrysler")) {
//        switch(key) {
//        case InputKeyUp: return 0x1;
//        case InputKeyOk: return 0x2;
//        default:         return original;
//        }
//    }
    /* Subaru */
//    if(strstr(protocol, "Subaru")) {
//        switch(key) {
//        case InputKeyUp:    return 0x1;
//        case InputKeyOk:    return 0x2;
//        case InputKeyDown:  return 0x3;
//        case InputKeyLeft:  return 0x4;
//        case InputKeyRight: return 0x8;
//        default:            return original;
//        }
//    }
    /* Fiat V1 */
//    if(strstr(protocol, "Fiat V1")) {
//        switch(key) {
//        case InputKeyUp:   return 0x8;
//        case InputKeyOk:   return 0x0;
//        case InputKeyDown: return 0xD;
//        default:           return original;
//        }
//    }
    /* Generic KeeLoq / KIA etc. – simple 4-button layout */
//    if(strstr(protocol, "Kia") || strstr(protocol, "KIA") ||
//       strstr(protocol, "KeeLoq")     || strstr(protocol, "Keeloq")) {
//        switch(key) {
//        case InputKeyUp:    return 0x1;
//        case InputKeyOk:    return 0x2;
//        case InputKeyDown:  return 0x3;
//        case InputKeyLeft:  return 0x4;
//        case InputKeyRight: return 0x8;
//        default:            return original;
//        }
//    }

//    return original;
//}

/* ═══════════════════════════════════════════════════════════════════════════
 * TX helpers
 * ═════════════════════════════════════════════════════════════════════════*/

/**
 * Read frequency and short preset name from fff_data.
 * Falls back to 433.92 MHz / "AM650" on failure.
 */
static void car_emulate_read_freq_preset(SubGhz* subghz, CarEmulateState* st) {
    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);

    st->freq = 433920000UL;
    strncpy(st->preset_short, "AM650", sizeof(st->preset_short) - 1);

    if(!fff) return;

    uint32_t freq = 0;
    flipper_format_rewind(fff);
    if(flipper_format_read_uint32(fff, "Frequency", &freq, 1) && freq > 0) {
        st->freq = freq;
    }

    FuriString* preset_str = furi_string_alloc();
    flipper_format_rewind(fff);
    if(flipper_format_read_string(fff, "Preset", preset_str)) {
        /* Convert long FuriHal name → short token used by the setting */
        const char* raw = furi_string_get_cstr(preset_str);
        const char* short_name = "AM650";
        if(strstr(raw, "Ook270"))       short_name = "AM270";
        else if(strstr(raw, "Ook650"))  short_name = "AM650";
        else if(strstr(raw, "238"))     short_name = "FM238";
        else if(strstr(raw, "12K"))     short_name = "FM12K";
        else if(strstr(raw, "476"))     short_name = "FM476";
        else if(strstr(raw, "Custom"))  short_name = "CUST";
        strncpy(st->preset_short, short_name, sizeof(st->preset_short) - 1);
    }
    furi_string_free(preset_str);
}

static bool car_emulate_hex_digit(char c, uint8_t* value) {
    if(c >= '0' && c <= '9') {
        *value = c - '0';
        return true;
    } else if(c >= 'a' && c <= 'f') {
        *value = c - 'a' + 10;
        return true;
    } else if(c >= 'A' && c <= 'F') {
        *value = c - 'A' + 10;
        return true;
    }
    return false;
}

static bool car_emulate_parse_hex_after(
    const char* text,
    const char* marker,
    uint32_t* value) {
    const char* field = strstr(text, marker);
    if(!field) return false;

    field += strlen(marker);
    while(*field == ' ' || *field == '\t' || *field == '[') {
        field++;
    }
    if(field[0] == '0' && (field[1] == 'x' || field[1] == 'X')) {
        field += 2;
    }

    uint32_t parsed = 0;
    uint8_t digits = 0;
    uint8_t digit_value = 0;
    while((digits < 8) && car_emulate_hex_digit(*field, &digit_value)) {
        parsed = (parsed << 4) | digit_value;
        digits++;
        field++;
    }

    if(digits == 0) return false;
    *value = parsed;
    return true;
}

static bool car_emulate_serial_is_placeholder(void) {
    return !s_state->has_serial || (s_state->serial <= 1);
}

static bool car_emulate_counter_is_placeholder(void) {
    return !s_state->has_counter || (s_state->current_counter == 0);
}

static void car_emulate_set_counter(uint32_t value) {
    s_state->original_counter = value;
    s_state->current_counter = value;
    s_state->has_counter = true;
}

static void car_emulate_apply_global_metadata(void) {
    furi_assert(s_state);

    if(subghz_block_generic_global.cnt_is_available) {
        car_emulate_set_counter(subghz_block_generic_global.current_cnt);
    }
}

static void car_emulate_apply_decoded_metadata(FuriString* decoded_text) {
    furi_assert(s_state);
    if(!decoded_text) return;

    const char* text = furi_string_get_cstr(decoded_text);
    uint32_t value = 0;

    if((car_emulate_parse_hex_after(text, "Sn:", &value) ||
        car_emulate_parse_hex_after(text, "SN:", &value) ||
        car_emulate_parse_hex_after(text, "Ser:", &value) ||
        car_emulate_parse_hex_after(text, "Serial:", &value)) &&
       (value != 0)) {
        s_state->serial = value;
        s_state->has_serial = true;
    } else if(
        car_emulate_serial_is_placeholder() &&
        car_emulate_parse_hex_after(text, "Fix:", &value) &&
        (value != 0)) {
        s_state->serial = value;
        s_state->has_serial = true;
    }

    if(car_emulate_parse_hex_after(text, "Cnt:", &value) &&
       ((value != 0) || car_emulate_counter_is_placeholder())) {
        car_emulate_set_counter(value);
    }
}

/** Update Btn and Cnt fields in fff_data so the transmitter re-serialises them. */
static bool car_emulate_apply_button(SubGhz* subghz, InputKey key) {
    UNUSED(subghz);

    uint8_t custom_btn_id;
    switch(key) {
    case InputKeyUp:    custom_btn_id = SUBGHZ_CUSTOM_BTN_UP;    break;
    case InputKeyDown:  custom_btn_id = SUBGHZ_CUSTOM_BTN_DOWN;  break;
    case InputKeyLeft:  custom_btn_id = SUBGHZ_CUSTOM_BTN_LEFT;  break;
    case InputKeyRight: custom_btn_id = SUBGHZ_CUSTOM_BTN_RIGHT; break;
    case InputKeyOk:
    default:            custom_btn_id = SUBGHZ_CUSTOM_BTN_OK;    break;
    }

    if(custom_btn_id > s_state->max_custom_button) {
        return false;
    }

    return subghz_custom_btn_set(custom_btn_id);
}


/** Update Cnt in fff_data (Btn is handled by the protocol via custom_btn). */
static void car_emulate_update_fff(SubGhz* subghz, uint32_t counter) {
    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    if(!fff) return;
    flipper_format_rewind(fff);
    flipper_format_insert_or_update_uint32(fff, "Cnt", &counter, 1);
}


/** Apply tx_power to the current preset and start a single transmission burst. */
static bool car_emulate_start_tx(SubGhz* subghz, uint8_t custom_btn_id) {
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    if(preset.data && preset.data_size > 0 && subghz->tx_power > 0) {
        subghz_txrx_set_tx_power(preset.data, preset.data_size, subghz->tx_power);
        FURI_LOG_I(TAG, "TX power index applied: %u", subghz->tx_power);
    }

    subghz_custom_btn_set(custom_btn_id);

    bool ok = subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx));
    if(ok) {
        subghz->state_notifications = SubGhzNotificationStateTx;
        notification_message(subghz->notifications, &sequence_blink_magenta_10);
        FURI_LOG_I(TAG, "TX started");
    } else {
        FURI_LOG_E(TAG, "subghz_tx_start failed");
    }
    return ok;
}


/** Stop an active transmission. */
static void car_emulate_stop_tx(SubGhz* subghz) {
    subghz_block_generic_global.endless_tx = false;
    subghz_txrx_stop(subghz->txrx);
    subghz->state_notifications = SubGhzNotificationStateIDLE;
    notification_message(subghz->notifications, &sequence_blink_stop);
    FURI_LOG_I(TAG, "TX stopped");
}

static bool car_emulate_restart_tx(SubGhz* subghz) {
    furi_assert(s_state);

    car_emulate_update_fff(subghz, s_state->current_counter);
    subghz_block_generic_global.endless_tx = true;

    if(car_emulate_start_tx(subghz, s_state->pending_button)) {
        s_state->is_transmitting = true;
        subghz->state_notifications = SubGhzNotificationStateTx;
        notification_message(subghz->notifications, &sequence_blink_magenta_10);
        FURI_LOG_D(TAG, "TX restarted while button is held");
        return true;
    }

    subghz_block_generic_global.endless_tx = false;
    s_state->is_transmitting = false;
    s_state->stop_pending = false;
    notification_message(subghz->notifications, &sequence_error);
    return false;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * View callback (fired from the View's input handler)
 * ═════════════════════════════════════════════════════════════════════════*/
static void subghz_scene_car_emulate_view_callback(uint32_t event, void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Helpers to keep the view in sync
 * ═════════════════════════════════════════════════════════════════════════*/
static void car_emulate_refresh_view(SubGhz* subghz) {
    furi_assert(s_state);
    subghz_car_emulate_view_set_data(
        subghz->car_emulate_view,
        s_state->protocol_name,
        s_state->serial,
        s_state->current_counter,
        s_state->original_counter,
        s_state->freq,
        s_state->preset_short,
        s_state->is_transmitting);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Scene on_enter
 * ═════════════════════════════════════════════════════════════════════════*/
void subghz_scene_car_emulate_on_enter(void* context) {
    SubGhz* subghz = context;
    furi_assert(subghz);

    /* Allocate per-session state */
    s_state = malloc(sizeof(CarEmulateState));
    furi_check(s_state);
    memset(s_state, 0, sizeof(CarEmulateState));
    subghz_block_generic_global.endless_tx = false;

    /* ── Read metadata from the loaded fff_data ── */
    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    if(fff) {
        FuriString* tmp = furi_string_alloc();

        flipper_format_rewind(fff);
        if(flipper_format_read_string(fff, "Protocol", tmp)) {
            strncpy(
                s_state->protocol_name,
                furi_string_get_cstr(tmp),
                sizeof(s_state->protocol_name) - 1);
        }

        flipper_format_rewind(fff);
        s_state->has_serial =
            flipper_format_read_uint32(fff, "Serial", &s_state->serial, 1);

        flipper_format_rewind(fff);
        uint32_t btn_tmp = 0;
        if(flipper_format_read_uint32(fff, "Btn", &btn_tmp, 1)) {
            s_state->original_button = (uint8_t)btn_tmp;
        }

        flipper_format_rewind(fff);
        s_state->has_counter =
            flipper_format_read_uint32(fff, "Cnt", &s_state->original_counter, 1);
        s_state->current_counter = s_state->original_counter;

        furi_string_free(tmp);
    }

    /* ── Initialize the custom_btn system ──────────────────────────────────
     * Reset first so any leftover state from a previous session is cleared.
     * Then deserialize the decoder once: this causes the protocol's own
     * deserialize() to call subghz_custom_btn_set_original() and
     * subghz_custom_btn_set_max(), which is exactly what the standard
     * Transmitter scene does via subghz_scene_transmitter_update_data_show().
     * After this call:
     *   - subghz_custom_btn_get_original() → the button that was in the file
     *   - subghz_custom_btn_is_allowed()   → true if protocol supports it
     *   - subghz_custom_btn_get_max()      → number of buttons available     */
    subghz_block_generic_global_reset(NULL);
    subghz_custom_btns_reset();

    SubGhzProtocolDecoderBase* decoder = subghz_txrx_get_decoder(subghz->txrx);
    if(decoder && fff) {
        flipper_format_rewind(fff);
        if(subghz_protocol_decoder_base_deserialize(decoder, fff) == SubGhzProtocolStatusOk) {
            FuriString* decoded_text = furi_string_alloc();
            subghz_protocol_decoder_base_get_string(decoder, decoded_text);
            car_emulate_apply_global_metadata();
            car_emulate_apply_decoded_metadata(decoded_text);
            furi_string_free(decoded_text);
        }
        /* Rewind again so subsequent reads in car_emulate_read_freq_preset()
         * start from the beginning of the file. */
        flipper_format_rewind(fff);
    }

    s_state->max_custom_button =
        subghz_custom_btn_is_allowed() ? subghz_custom_btn_get_max() : SUBGHZ_CUSTOM_BTN_OK;
    if(s_state->max_custom_button == SUBGHZ_CUSTOM_BTN_OK) {
        s_state->max_custom_button =
            subghz_button_labels_get_max_custom_btn(s_state->protocol_name);
        if(s_state->max_custom_button != SUBGHZ_CUSTOM_BTN_OK) {
            subghz_custom_btn_set_max(s_state->max_custom_button);
        }
    }

    const char* button_labels[SUBGHZ_BUTTON_LABEL_COUNT];
    subghz_button_labels_reset(button_labels);
    subghz_button_labels_apply_protocol(s_state->protocol_name, button_labels);

    subghz_car_emulate_view_set_labels(
        subghz->car_emulate_view,
        subghz_button_labels_get(
            button_labels, SUBGHZ_CUSTOM_BTN_OK, subghz_custom_btn_get_original()),
        s_state->max_custom_button >= SUBGHZ_CUSTOM_BTN_UP ?
            subghz_button_labels_get(
                button_labels, SUBGHZ_CUSTOM_BTN_UP, subghz_custom_btn_get_original()) :
            "",
        s_state->max_custom_button >= SUBGHZ_CUSTOM_BTN_DOWN ?
            subghz_button_labels_get(
                button_labels, SUBGHZ_CUSTOM_BTN_DOWN, subghz_custom_btn_get_original()) :
            "",
        s_state->max_custom_button >= SUBGHZ_CUSTOM_BTN_LEFT ?
            subghz_button_labels_get(
                button_labels, SUBGHZ_CUSTOM_BTN_LEFT, subghz_custom_btn_get_original()) :
            "",
        s_state->max_custom_button >= SUBGHZ_CUSTOM_BTN_RIGHT ?
            subghz_button_labels_get(
                button_labels, SUBGHZ_CUSTOM_BTN_RIGHT, subghz_custom_btn_get_original()) :
            ""
    );

    car_emulate_read_freq_preset(subghz, s_state);

    /* ── Configure the view ── */
    subghz_car_emulate_view_set_callback(
        subghz->car_emulate_view, subghz_scene_car_emulate_view_callback, subghz);

    car_emulate_refresh_view(subghz);

    subghz->state_notifications = SubGhzNotificationStateIDLE;
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdCarEmulate);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Scene on_event
 * ═════════════════════════════════════════════════════════════════════════*/
bool subghz_scene_car_emulate_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    furi_assert(s_state);
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {

        /* ── Transmit ── */
        if((event.event & 0xFFFFU) == SubGhzCustomEventCarEmulateTransmit) {
            InputKey key = (InputKey)((event.event >> 16) & 0xFFU);

            /* Stop any ongoing TX first */
            if(subghz->state_notifications == SubGhzNotificationStateTx) {
                car_emulate_stop_tx(subghz);
            }

            /* Set the custom button BEFORE deserialize() is called inside
             * subghz_tx_start() → subghz_txrx_tx_start().
             * The protocol's deserialize() will call subghz_custom_btn_get()
             * to pick the right button code. */
            if(!car_emulate_apply_button(subghz, key)) {
                notification_message(subghz->notifications, &sequence_error);
                car_emulate_refresh_view(subghz);
                return true;
            }

            /* Bump counter */
            s_state->current_counter++;

            /* Only update the counter in fff_data; the protocol handles Btn. */
            car_emulate_update_fff(subghz, s_state->current_counter);

            s_state->is_transmitting = true;
            s_state->stop_pending    = false;
            s_state->tx_start_tick   = (uint32_t)furi_get_tick();

            uint8_t cur_btn = subghz_custom_btn_get();
            s_state->pending_button = cur_btn;
            subghz_block_generic_global.endless_tx = true;
            if(!car_emulate_start_tx(subghz, cur_btn)) {
                s_state->is_transmitting = false;
                subghz_block_generic_global.endless_tx = false;
                notification_message(subghz->notifications, &sequence_error);
            }

            car_emulate_refresh_view(subghz);
            consumed = true;

        /* ── Stop ── */
        } else if(event.event == SubGhzCustomEventCarEmulateStop) {
            if(s_state->is_transmitting) {
                subghz_block_generic_global.endless_tx = false;

                uint32_t elapsed = (uint32_t)furi_get_tick() - s_state->tx_start_tick;
                if(
                    elapsed >= MIN_TX_TICKS &&
                    subghz->state_notifications == SubGhzNotificationStateTx) {
                    car_emulate_stop_tx(subghz);
                    s_state->is_transmitting = false;
                    s_state->stop_pending    = false;
                } else {
                    s_state->stop_pending = true;
                }
            }
            car_emulate_refresh_view(subghz);
            consumed = true;

        /* ── Exit ── */
        } else if(event.event == SubGhzCustomEventCarEmulateExit) {
            if(subghz->state_notifications == SubGhzNotificationStateTx) {
                car_emulate_stop_tx(subghz);
            }
            subghz_block_generic_global.endless_tx = false;
            scene_manager_search_and_switch_to_previous_scene(
                subghz->scene_manager, SubGhzSceneSavedMenu);
            consumed = true;
        }

    } else if(event.type == SceneManagerEventTypeTick) {

        if(s_state->is_transmitting &&
           subghz->state_notifications == SubGhzNotificationStateTx) {

            /* Check if hardware is done */
            if(subghz_devices_is_async_complete_tx(subghz->txrx->radio_device)) {
                subghz->state_notifications = SubGhzNotificationStateIDLE;
                subghz_txrx_stop(subghz->txrx);

                if(s_state->stop_pending) {
                    s_state->is_transmitting = false;
                    s_state->stop_pending    = false;
                    notification_message(subghz->notifications, &sequence_blink_stop);
                } else {
                    car_emulate_restart_tx(subghz);
                }
            } else {
                /* Still transmitting – blink LED */
                notification_message(subghz->notifications, &sequence_blink_magenta_10);
            }

            /* Enforce MIN_TX_TICKS stop gate */
            if(s_state->stop_pending) {
                uint32_t elapsed = (uint32_t)furi_get_tick() - s_state->tx_start_tick;
                if(elapsed >= MIN_TX_TICKS) {
                    car_emulate_stop_tx(subghz);
                    s_state->is_transmitting = false;
                    s_state->stop_pending    = false;
                }
            }
        }

        /* Refresh view every tick for animation */
        car_emulate_refresh_view(subghz);
        consumed = true;
    }

    return consumed;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Scene on_exit
 * ═════════════════════════════════════════════════════════════════════════*/
void subghz_scene_car_emulate_on_exit(void* context) {
    SubGhz* subghz = context;

    if(subghz->state_notifications == SubGhzNotificationStateTx) {
        car_emulate_stop_tx(subghz);
    }

    subghz_block_generic_global.endless_tx = false;
    subghz->state_notifications = SubGhzNotificationStateIDLE;
    notification_message(subghz->notifications, &sequence_blink_stop);

    /* Clear view callbacks */
    subghz_car_emulate_view_set_callback(subghz->car_emulate_view, NULL, NULL);

    /* Free per-session state */
    if(s_state) {
        free(s_state);
        s_state = NULL;
    }
}
