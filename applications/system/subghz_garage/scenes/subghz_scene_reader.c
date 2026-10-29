#include "../subghz_i.h"
#include "../helpers/subghz_debug_log.h"
#include "../helpers/subghz_name_generator_compat.h"
#include <lib/subghz/protocols/raw.h>
#include <toolbox/path.h>
#include <furi.h>
#include <furi/core/memmgr.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define TAG "SubGhzSceneReader"

#define RAW_FILE_NAME "RAW_"

bool subghz_scene_read_raw_update_filename(SubGhz* subghz) {
    bool ret = false;

    FuriString* temp_str = furi_string_alloc();
    do {
        FlipperFormat* fff_data = subghz_txrx_get_fff_data(subghz->txrx);
        if(!flipper_format_rewind(fff_data)) {
            FURI_LOG_E(TAG, "Rewind error");
            break;
        }

        if(!flipper_format_read_string(fff_data, "File_name", temp_str)) {
            FURI_LOG_E(TAG, "Missing File_name");
            break;
        }

        furi_string_set(subghz->file_path, temp_str);

        ret = true;
    } while(false);

    furi_string_free(temp_str);
    return ret;
}

static void subghz_scene_read_raw_update_statusbar(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;

    FuriString* frequency_str = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();

#ifdef SUBGHZ_EXT_PRESET_NAME
    subghz_txrx_get_frequency_and_modulation(subghz->txrx, frequency_str, modulation_str, true);
#else
    subghz_txrx_get_frequency_and_modulation(subghz->txrx, frequency_str, modulation_str, false);
#endif
    subghz_read_raw_add_data_statusbar(
        subghz->subghz_read_raw,
        furi_string_get_cstr(frequency_str),
        furi_string_get_cstr(modulation_str));

    furi_string_free(frequency_str);
    furi_string_free(modulation_str);

    subghz_read_raw_set_radio_device_type(
        subghz->subghz_read_raw, subghz_txrx_radio_device_get(subghz->txrx));
}

void subghz_scene_read_raw_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

void subghz_scene_read_raw_callback_end_tx(void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(
        subghz->view_dispatcher, SubGhzCustomEventViewReadRAWSendStop);
}

#define ENVELOPE_PREVIEW_SAMPLES 2048u

typedef struct {
    File*  file;
    uint8_t buf[128];
    size_t len;
    size_t pos;
    bool   eof;
} SubghzRawLineReader;

static bool subghz_raw_lr_read_line(SubghzRawLineReader* lr, FuriString* out) {
    furi_string_reset(out);
    bool any = false;
    while(true) {
        if(lr->pos >= lr->len) {
            if(lr->eof) break;
            lr->len = storage_file_read(lr->file, lr->buf, sizeof(lr->buf));
            lr->pos = 0;
            if(lr->len == 0) {
                lr->eof = true;
                break;
            }
        }
        char c = (char)lr->buf[lr->pos++];
        if(c == '\n') {
            any = true;
            break;
        }
        if(c == '\r') continue;
        furi_string_push_back(out, c);
        any = true;
    }
    return any;
}

static const uint8_t ENVELOPE_ZOOM_WINDOW_PCT[5] = {100, 60, 36, 22, 13};

static void subghz_scene_read_raw_load_envelope_ex(
    SubGhz* subghz, uint8_t zoom_level, uint8_t center_pct, uint64_t* out_total_us) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File*    file     = storage_file_alloc(storage);

    uint8_t  preview[100];
    memset(preview, 0, sizeof(preview));
    uint32_t total_samples = 0;
    bool     ok            = false;
    uint64_t total_us      = 0;
    if(out_total_us) *out_total_us = 0;

    int32_t* buf = malloc(sizeof(int32_t) * ENVELOPE_PREVIEW_SAMPLES);

    if(buf && storage_file_open(
                  file, furi_string_get_cstr(subghz->file_path),
                  FSAM_READ, FSOM_OPEN_EXISTING)) {

        SubghzRawLineReader lr = {.file = file, .len = 0, .pos = 0, .eof = false};
        FuriString* line = furi_string_alloc();

        while(subghz_raw_lr_read_line(&lr, line)) {
            const char* s = furi_string_get_cstr(line);
            if(strncmp(s, "RAW_Data:", 9) == 0) {
                const char* p = s + 9;
                char* end;
                while(*p) {
                    long v = strtol(p, &end, 10);
                    if(end == p) {
                        if(*p == '\0') break;
                        p++;
                        continue;
                    }
                    p = end;

                    total_us += (uint64_t)(v < 0 ? -(int64_t)v : (int64_t)v);

                    if(total_samples < ENVELOPE_PREVIEW_SAMPLES)
                        buf[total_samples++] = (int32_t)v;
                }
            }
        }

        furi_string_free(line);
        storage_file_close(file);

        if(total_samples > 0) {

            if(out_total_us) *out_total_us = total_us;

            if(total_us > 0) {

                uint64_t disp_start = 0;
                uint64_t disp_end   = total_us;
                if(zoom_level > 0 && zoom_level < 5) {
                    uint64_t window_span =
                        (total_us * ENVELOPE_ZOOM_WINDOW_PCT[zoom_level]) / 100;
                    if(window_span < 1) window_span = 1;
                    uint64_t center = ((uint64_t)center_pct * total_us) / 100;
                    uint64_t half   = window_span / 2;
                    disp_start = (center > half) ? (center - half) : 0;
                    disp_end   = disp_start + window_span;
                    if(disp_end > total_us) {
                        disp_end   = total_us;
                        disp_start = (disp_end > window_span) ? (disp_end - window_span) : 0;
                    }
                }
                uint64_t disp_span = (disp_end > disp_start) ? (disp_end - disp_start) : 1;

                uint32_t bucket_on_us[100] = {0};
                uint64_t current_us = 0;

                for(uint32_t i = 0; i < total_samples; i++) {
                    int32_t  val = buf[i];
                    uint64_t dur = (uint64_t)(val < 0 ? -val : val);
                    bool     hi  = (val > 0);
                    uint64_t seg_start = current_us;
                    uint64_t seg_end   = current_us + dur;
                    current_us = seg_end;

                    if(!hi || dur == 0) continue;

                    uint64_t cs = seg_start > disp_start ? seg_start : disp_start;
                    uint64_t ce = seg_end   < disp_end   ? seg_end   : disp_end;
                    if(ce <= cs) continue;

                    uint8_t b0 = (uint8_t)(((cs - disp_start) * 100) / disp_span);
                    uint8_t b1 = (uint8_t)(((ce - disp_start) * 100) / disp_span);
                    if(b1 > 99) b1 = 99;
                    for(uint8_t b = b0; b <= b1; b++) {
                        uint64_t bstart = disp_start + ((uint64_t)b * disp_span / 100);
                        uint64_t bend   = disp_start + ((uint64_t)(b + 1) * disp_span / 100);
                        uint64_t ov_s   = cs > bstart ? cs : bstart;
                        uint64_t ov_e   = ce < bend   ? ce : bend;
                        if(ov_e > ov_s) bucket_on_us[b] += (uint32_t)(ov_e - ov_s);
                    }
                }

                for(uint8_t b = 0; b < 100; b++) {
                    uint64_t bstart = disp_start + ((uint64_t)b * disp_span / 100);
                    uint64_t bend   = disp_start + ((uint64_t)(b + 1) * disp_span / 100);
                    uint64_t span   = (bend > bstart) ? (bend - bstart) : 1;
                    uint32_t inten  = (uint32_t)((uint64_t)bucket_on_us[b] * 255 / span);
                    preview[b] = (uint8_t)(inten > 255 ? 255 : inten);
                }
                ok = true;
            }
        }
    } else if(file) {
        storage_file_close(file);
    }

    if(buf) free(buf);
    if(file) storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    subghz_read_raw_set_envelope(
        subghz->subghz_read_raw,
        ok ? preview : NULL,
        (uint32_t)total_us);
}

static void subghz_scene_read_raw_load_envelope(SubGhz* subghz) {
    subghz_scene_read_raw_load_envelope_ex(subghz, 0, 0, NULL);
}

static void subghz_scene_read_raw_load_envelope_zoomed(
    SubGhz* subghz, uint8_t zoom_level, uint8_t center_pct) {
    subghz_scene_read_raw_load_envelope_ex(subghz, zoom_level, center_pct, NULL);
}

#define AUTO_READ_TMP_FILE_NAME "auto_read_tmp"

#define SUBGHZ_AUTO_READ_SILENCE_TICKS 3

#define SUBGHZ_AUTO_READ_SETTLE_TICKS 3

#define SUBGHZ_AUTO_READ_MIN_SAMPLES 32

#define SUBGHZ_AUTO_READ_SHORT_CAPTURE_LOG_MAX 3

#define SUBGHZ_AUTO_READ_MAX_CAPTURE_TICKS 50

#define SUBGHZ_AUTO_READ_NOISE_MARGIN_DB 10.0f

#define SUBGHZ_AUTO_READ_NOISE_RISE_DB 0.5f

#define SUBGHZ_AUTO_READ_NOISE_GATE_MAX (-50.0f)

#define SUBGHZ_AUTO_READ_DECODE_SAMPLES_PER_TICK 400

#define SUBGHZ_AUTO_START_COUNTDOWN_SEC 3

#define SUBGHZ_LOW_RAM_TRIGGER_TICKS 10

#define SUBGHZ_LOW_RAM_TICK_BAIL_RECHECK_WAIT_MS 300

#define SUBGHZ_LOW_RAM_RESUME_ATTEMPTS_MAX 3

typedef enum {

    SubGhzReceiverAutoStateStart,
    SubGhzReceiverAutoStateListening,
    SubGhzReceiverAutoStateDecoding,
} SubGhzReceiverAutoState;

static uint32_t s_auto_last_sample_count = 0;
static uint32_t s_auto_silence_ticks = 0;
static bool s_auto_has_activity = false;
static uint16_t s_auto_history_count_before_decode = 0;

static uint8_t s_auto_decode_group_cursor = 0;

static uint8_t s_auto_decode_group_ordinal = 0;
static uint8_t s_auto_decode_group_total = 0;
static uint32_t s_auto_decode_samples_total = 0;
static uint32_t s_auto_decode_samples_done = 0;

static uint32_t s_auto_tick_count = 0;

static uint32_t s_auto_activity_start_samples = 0;

static uint32_t s_auto_short_capture_count = 0;

static uint32_t s_auto_activity_start_tick = 0;

static float s_auto_noise_floor = 0.0f;

static uint32_t s_low_ram_tick_count = 0;

static uint32_t s_low_ram_resume_attempts = 0;

static uint8_t s_auto_start_countdown_sec = 0;
static uint8_t s_auto_start_subtick = 0;

static bool s_auto_start_cancelled = false;

static uint32_t s_auto_start_entry_fail_count = 0;

static uint8_t s_manual_start_countdown_sec = 0;
static uint8_t s_manual_start_subtick = 0;
static bool s_manual_start_countdown_fired = false;

static bool s_manual_start_cancelled = false;

const NotificationSequence subghz_sequence_rx = {
    &message_green_255,

    &message_display_backlight_on,

    &message_vibro_on,
    &message_note_c6,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,

    &message_delay_50,
    NULL,
};

static void subghz_scene_receiver_callback(SubGhzCustomEvent event, void* context) {
    furi_assert(context);
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

static void subghz_scene_receiver_apply_file_prefix(SubGhz* subghz, char* name_buf, size_t max_len) {
    if(subghz->last_settings->file_prefix[0] == '\0') return;

    char tmp[SUBGHZ_MAX_LEN_NAME + sizeof(subghz->last_settings->file_prefix)];
    snprintf(tmp, sizeof(tmp), "%s%s", subghz->last_settings->file_prefix, name_buf);
    strncpy(name_buf, tmp, max_len - 1);
    name_buf[max_len - 1] = '\0';
}

static void subghz_scene_receiver_process_auto_save(SubGhz* subghz) {
    uint16_t idx;
    while(subghz_history_find_auto_save_pending(subghz->history, &idx)) {
        bool saved = false;
        FlipperFormat* ff = subghz_history_get_raw_data(subghz->history, idx);
        if(ff) {
            FuriString* protocol_name = furi_string_alloc();
            flipper_format_rewind(ff);
            if(!flipper_format_read_string(ff, "Protocol", protocol_name)) {
                furi_string_set(protocol_name, "Unknown");
            }

            char file_name_buf[SUBGHZ_MAX_LEN_NAME] = {0};
            DateTime datetime = subghz_history_get_datetime(subghz->history, idx);
            subghz_garage_name_generator_make_auto_datetime(
                file_name_buf,
                SUBGHZ_MAX_LEN_NAME,
                furi_string_get_cstr(protocol_name),
                &datetime);
            subghz_scene_receiver_apply_file_prefix(subghz, file_name_buf, SUBGHZ_MAX_LEN_NAME);

            FuriString* path = furi_string_alloc();
            furi_string_set(path, SUBGHZ_APP_FOLDER);
            furi_string_cat_printf(path, "/%s%s", file_name_buf, SUBGHZ_APP_FILENAME_EXTENSION);

            saved = subghz_save_protocol_to_file(subghz, ff, furi_string_get_cstr(path));

            furi_string_free(path);
            furi_string_free(protocol_name);
        }

        notification_message(
            subghz->notifications, saved ? &sequence_double_vibro : &sequence_error);
        subghz_history_set_auto_save_pending(subghz->history, idx, false);
    }
}

static void subghz_scene_receiver_add_to_history_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder_base,
    void* context) {
    furi_assert(context);
    SubGhz* subghz = context;

    subghz_debug_log_write("add_to_history_callback: enter, decoder_base=%p", (void*)decoder_base);
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    bool added = subghz_history_add_to_history(
        subghz->history, decoder_base, &preset, SUBGHZ_LOW_RAM_FREE_HEAP_READ);
    if(added && subghz->last_settings->auto_save) {
        subghz_history_set_auto_save_pending(
            subghz->history, subghz_history_get_last_index(subghz->history) - 1, true);
    }
    subghz_debug_log_write("add_to_history_callback: added, resetting receiver");
    subghz_receiver_reset(receiver);
    subghz_debug_log_write("add_to_history_callback: exit");
}

static void subghz_scene_receiver_delete_tmp_file(SubGhz* subghz) {
    UNUSED(subghz);
    FuriString* temp_str = furi_string_alloc();
    furi_string_printf(
        temp_str, "%s/%s%s", SUBGHZ_RAW_FOLDER, AUTO_READ_TMP_FILE_NAME, SUBGHZ_APP_FILENAME_EXTENSION);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_remove(storage, furi_string_get_cstr(temp_str));
    furi_record_close(RECORD_STORAGE);
    furi_string_free(temp_str);
}

static void subghz_scene_reader_read_show_start(SubGhz* subghz) {
    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneReceiver, SubGhzReceiverAutoStateStart);

    float threshold_rssi = subghz_threshold_rssi_get(subghz->threshold_rssi);

    FuriString* frequency_str = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();
    subghz_txrx_get_frequency_and_modulation(subghz->txrx, frequency_str, modulation_str, true);
    subghz_read_raw_add_data_statusbar(
        subghz->subghz_read_raw,
        furi_string_get_cstr(frequency_str),
        furi_string_get_cstr(modulation_str));
    furi_string_free(frequency_str);
    furi_string_free(modulation_str);
    subghz_read_raw_set_radio_device_type(
        subghz->subghz_read_raw, subghz_txrx_radio_device_get(subghz->txrx));

    subghz_read_raw_set_callback(subghz->subghz_read_raw, subghz_scene_receiver_callback, subghz);
    subghz_read_raw_set_viz_mode(
        subghz->subghz_read_raw,
        (SubGhzReadRawVizMode)subghz->last_settings->visualizer_display_mode);

    subghz_read_raw_set_status(subghz->subghz_read_raw, SubGhzReadRAWStatusStart, "", threshold_rssi);

    s_auto_start_countdown_sec = SUBGHZ_AUTO_START_COUNTDOWN_SEC;
    s_auto_start_subtick = 0;
    s_auto_start_cancelled = false;
    subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, s_auto_start_countdown_sec);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReadRAW);
}

static void subghz_scene_receiver_start_listening(SubGhz* subghz, bool switch_view) {
    s_auto_last_sample_count = 0;
    s_auto_silence_ticks = 0;
    s_auto_has_activity = false;
    s_auto_tick_count = 0;
    s_auto_activity_start_samples = 0;
    s_auto_short_capture_count = 0;
    s_auto_activity_start_tick = 0;
    s_auto_noise_floor = 0.0f;
    s_low_ram_tick_count = 0;

    subghz_threshold_rssi_reset(subghz->threshold_rssi);

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneReceiver, SubGhzReceiverAutoStateListening);

    float threshold_rssi = subghz_threshold_rssi_get(subghz->threshold_rssi);

    FuriString* frequency_str = furi_string_alloc();
    FuriString* modulation_str = furi_string_alloc();
    subghz_txrx_get_frequency_and_modulation(subghz->txrx, frequency_str, modulation_str, true);
    subghz_read_raw_add_data_statusbar(
        subghz->subghz_read_raw,
        furi_string_get_cstr(frequency_str),
        furi_string_get_cstr(modulation_str));
    furi_string_free(frequency_str);
    furi_string_free(modulation_str);
    subghz_read_raw_set_radio_device_type(
        subghz->subghz_read_raw, subghz_txrx_radio_device_get(subghz->txrx));

    subghz_read_raw_set_callback(subghz->subghz_read_raw, subghz_scene_receiver_callback, subghz);
    subghz_read_raw_set_viz_mode(
        subghz->subghz_read_raw,
        (SubGhzReadRawVizMode)subghz->last_settings->visualizer_display_mode);

    if(subghz_low_ram_mitigate(subghz, SUBGHZ_LOW_RAM_FREE_HEAP_CHAIN_START)) {
        s_auto_start_entry_fail_count++;

        size_t bail_free_heap = memmgr_get_free_heap();
        if(furi_get_tick() < subghz->low_ram_grace_until_ms) {

            FURI_LOG_W(
                TAG,
                "start_listening: free heap %zu still low within post-recovery grace, backing out to Start",
                bail_free_heap);
            subghz_debug_log_write(
                "start_listening: free heap %zu still low within post-recovery grace, backing out to Start",
                bail_free_heap);
            subghz_scene_reader_read_show_start(subghz);
            return;
        }
        FURI_LOG_W(
            TAG,
            "start_listening: free heap %zu still low after mitigation, bailing to warning",
            bail_free_heap);
        subghz_debug_log_write(
            "start_listening: free heap %zu still low after mitigation, bailing to warning",
            bail_free_heap);

        subghz_txrx_release_protocol_group(subghz->txrx);

        subghz_txrx_release_radio(subghz->txrx);
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneLowRamWarning);
        return;
    }
    s_auto_start_entry_fail_count = 0;

    subghz_txrx_reset_protocol_load_failed(subghz->txrx);
    if(!subghz_txrx_load_decoder_by_name_protocol(subghz->txrx, SUBGHZ_PROTOCOL_RAW_NAME)) {
        FURI_LOG_E(TAG, "start_listening: protocol group failed to load");

        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneProtocolLoadError);
        return;
    }
    subghz_txrx_receiver_set_filter(subghz->txrx, SubGhzProtocolFlag_RAW);
    subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);

    SubGhzProtocolDecoderRAW* decoder_raw =
        (SubGhzProtocolDecoderRAW*)subghz_txrx_get_decoder(subghz->txrx);
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);

    subghz_txrx_stop(subghz->txrx);
    subghz_protocol_raw_save_to_file_stop(decoder_raw);

    subghz_debug_log_write(
        "start_listening: pre-save_to_file_init free heap %zu", memmgr_get_free_heap());
    bool save_to_file_ok =
        subghz_protocol_raw_save_to_file_init(decoder_raw, AUTO_READ_TMP_FILE_NAME, &preset);
    subghz_debug_log_write(
        "start_listening: post-save_to_file_init free heap %zu, ok=%d",
        memmgr_get_free_heap(),
        save_to_file_ok);

    if(save_to_file_ok) {
        FURI_LOG_I(TAG, "start_listening: rx_start");
        subghz_debug_log_write("start_listening: rx_start");
        bool rx_ready = subghz_txrx_rx_start(subghz->txrx);
        if(!rx_ready) {

            FURI_LOG_W(TAG, "start_listening: worker alloc refused, bailing to warning");
            subghz_debug_log_write(
                "start_listening: worker alloc refused, bailing to warning");
            subghz_protocol_raw_save_to_file_stop(decoder_raw);
            subghz_scene_receiver_delete_tmp_file(subghz);
            subghz_txrx_release_protocol_group(subghz->txrx);
            subghz_txrx_release_radio(subghz->txrx);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneLowRamWarning);
            return;
        }
        subghz->state_notifications = SubGhzNotificationStateRx;
        subghz_read_raw_set_radio_device_type(
            subghz->subghz_read_raw, subghz_txrx_radio_device_get(subghz->txrx));

        subghz_txrx_hopper_set_state(
            subghz->txrx,
            subghz->last_settings->enable_hopping ? SubGhzHopperStateRunning :
                                                     SubGhzHopperStateOFF);

        subghz_read_raw_set_status(subghz->subghz_read_raw, SubGhzReadRAWStatusREC, "", threshold_rssi);
        if(switch_view) {
            view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReadRAW);
        }
    } else if(subghz_low_ram_mitigate(subghz, SUBGHZ_LOW_RAM_FREE_HEAP_CHAIN_START)) {

        size_t bail_free_heap = memmgr_get_free_heap();
        if(furi_get_tick() < subghz->low_ram_grace_until_ms) {

            FURI_LOG_W(
                TAG,
                "start_listening: save_to_file_init failed with free heap %zu within post-recovery grace, backing out to Start",
                bail_free_heap);
            subghz_debug_log_write(
                "start_listening: save_to_file_init failed with free heap %zu within post-recovery grace, backing out to Start",
                bail_free_heap);
            subghz_scene_receiver_delete_tmp_file(subghz);
            subghz_scene_reader_read_show_start(subghz);
            return;
        }
        FURI_LOG_W(
            TAG,
            "start_listening: save_to_file_init failed with free heap %zu still low after mitigation",
            bail_free_heap);
        subghz_debug_log_write(
            "start_listening: save_to_file_init failed with free heap %zu still low after mitigation",
            bail_free_heap);

        subghz_txrx_release_protocol_group(subghz->txrx);
        subghz_txrx_release_radio(subghz->txrx);
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneLowRamWarning);
    } else {
        furi_string_set(subghz->error_str, "Function requires\nan SD card.");
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
    }
}

static bool subghz_scene_receiver_decode_next(SubGhz* subghz) {
    LevelDuration level_duration;
    SubGhzReceiver* receiver = subghz_txrx_get_receiver(subghz->txrx);
    for(uint32_t read = SUBGHZ_AUTO_READ_DECODE_SAMPLES_PER_TICK; read > 0; --read) {
        level_duration =
            subghz_file_encoder_worker_get_level_duration(subghz->decode_raw_file_worker_encoder);
        if(!level_duration_is_reset(level_duration)) {
            if(level_duration_is_wait(level_duration)) {
                return true;
            }
            bool level = level_duration_get_level(level_duration);
            uint32_t duration = level_duration_get_duration(level_duration);
            if(duration > 1000000) {
                FURI_LOG_E(TAG, "decode_next: LD overflow: %ld", duration);
                return true;
            }
            subghz_receiver_decode(receiver, level, duration);
            s_auto_decode_samples_done++;
        } else {
            return false;
        }
    }
    return true;
}

static void subghz_scene_receiver_decode_show_progress(SubGhz* subghz) {
    uint32_t group_pct = 100;
    if(s_auto_decode_samples_total > 0) {
        group_pct = (s_auto_decode_samples_done * 100) / s_auto_decode_samples_total;
        if(group_pct > 100) {
            group_pct = 100;
        }
    }
    uint32_t group_total = s_auto_decode_group_total ? s_auto_decode_group_total : 1;
    uint32_t groups_done = s_auto_decode_group_ordinal ? (s_auto_decode_group_ordinal - 1u) : 0u;
    uint32_t pct = (groups_done * 100 + group_pct) / group_total;
    if(pct > 100) {
        pct = 100;
    }
    subghz_read_raw_set_decoding(
        subghz->subghz_read_raw,
        s_auto_decode_group_ordinal,
        (uint8_t)group_total,
        (uint8_t)pct);
}

static void subghz_scene_receiver_no_match_widget_cb(
    GuiButtonType result,
    InputType type,
    void* context) {
    SubGhz* subghz = context;
    if(type != InputTypeShort) return;

    if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventReceiverNoMatchBack);
    } else if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(
            subghz->view_dispatcher, SubGhzCustomEventReceiverNoMatchSave);
    }
}

static void subghz_scene_receiver_show_decode_failed_popup(SubGhz* subghz) {
    subghz_ensure_widget(subghz);
    Widget* widget = subghz->widget;
    widget_add_string_multiline_element(
        widget, 64, 8, AlignCenter, AlignTop, FontPrimary, "No Match");
    widget_add_string_multiline_element(
        widget, 64, 20, AlignCenter, AlignTop, FontSecondary, "Couldn't decode\nthat signal.");
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Back", subghz_scene_receiver_no_match_widget_cb, subghz);
    widget_add_button_element(
        widget, GuiButtonTypeRight, "Save", subghz_scene_receiver_no_match_widget_cb, subghz);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

static void subghz_scene_receiver_decode_finish(SubGhz* subghz) {
    subghz_debug_log_write("decode_finish: enter");
    subghz_read_raw_clear_decoding(subghz->subghz_read_raw);
    if(subghz->last_settings->auto_save) {
        subghz_scene_receiver_process_auto_save(subghz);
    }
    subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);

    if(subghz->decode_raw_file_worker_encoder) {
        if(subghz_file_encoder_worker_is_running(subghz->decode_raw_file_worker_encoder)) {
            subghz_debug_log_write("decode_finish: stopping worker");
            subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
        }
        subghz_debug_log_write("decode_finish: freeing worker");
        subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
        subghz->decode_raw_file_worker_encoder = NULL;
    }

    subghz_scene_receiver_delete_tmp_file(subghz);
    subghz_debug_log_write("decode_finish: tmp file deleted");

    uint16_t new_count = subghz_history_get_item(subghz->history);
    subghz_debug_log_write(
        "decode_finish: history count now %d (was %d before)", new_count, s_auto_history_count_before_decode);
    if(new_count > s_auto_history_count_before_decode) {
        FURI_LOG_I(TAG, "decode_finish: matched, opening info for idx %d", new_count - 1);
        subghz->idx_menu_chosen = new_count - 1;
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverInfo);
    } else {
        FURI_LOG_I(TAG, "decode_finish: no match");
        subghz_scene_receiver_show_decode_failed_popup(subghz);
    }
    subghz_debug_log_write("decode_finish: exit");
}

static bool subghz_scene_receiver_decode_open_next_group(SubGhz* subghz, uint8_t search_from) {
    if(subghz->decode_raw_file_worker_encoder) {
        if(subghz_file_encoder_worker_is_running(subghz->decode_raw_file_worker_encoder)) {
            subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
        }
        subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
        subghz->decode_raw_file_worker_encoder = NULL;
    }

    SubGhzGarageProtocolGroup candidate;
    while(subghz_garage_protocol_group_next_enabled(
        subghz->last_settings->protocol_groups_enabled, search_from, &candidate)) {
        search_from = (uint8_t)candidate + 1;

        s_auto_decode_group_ordinal++;
        s_auto_decode_samples_done = 0;
        subghz_scene_receiver_decode_show_progress(subghz);

        if(!subghz_txrx_ensure_protocol_group(subghz->txrx, candidate)) {
            subghz_debug_log_write(
                "decode_open_next_group: group %d failed to load, skipping", (int)candidate);
            continue;
        }

        subghz->decode_raw_file_worker_encoder = subghz_file_encoder_worker_alloc();

        FuriString* file_name = furi_string_alloc();
        bool started = flipper_format_rewind(subghz_txrx_get_fff_data(subghz->txrx)) &&
                       flipper_format_read_string(
                           subghz_txrx_get_fff_data(subghz->txrx), "File_name", file_name) &&
                       subghz_file_encoder_worker_start(
                           subghz->decode_raw_file_worker_encoder,
                           furi_string_get_cstr(file_name),
                           subghz_txrx_radio_device_get_name(subghz->txrx));
        furi_string_free(file_name);

        if(!started) {
            FURI_LOG_E(
                TAG, "decode_open_next_group: couldn't start decode worker for group %d",
                (int)candidate);
            subghz_debug_log_write(
                "decode_open_next_group: couldn't start decode worker for group %d, skipping",
                (int)candidate);
            subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
            subghz->decode_raw_file_worker_encoder = NULL;
            continue;
        }

        furi_delay_ms(100);

        subghz_txrx_set_rx_callback(
            subghz->txrx, subghz_scene_receiver_add_to_history_callback, subghz);
        subghz_txrx_receiver_set_filter(subghz->txrx, SubGhzProtocolFlag_Decodable);

        s_auto_decode_group_cursor = (uint8_t)candidate;
        subghz_debug_log_write("decode_open_next_group: decoding against group %d", (int)candidate);
        return true;
    }
    return false;
}

static void subghz_scene_receiver_stop_and_decode(SubGhz* subghz) {
    subghz_debug_log_write("stop_and_decode: enter");
    subghz_txrx_stop(subghz->txrx);
    subghz_debug_log_write("stop_and_decode: txrx_stop done");
    subghz->state_notifications = SubGhzNotificationStateIDLE;

    SubGhzProtocolDecoderRAW* decoder_raw =
        (SubGhzProtocolDecoderRAW*)subghz_txrx_get_decoder(subghz->txrx);
    size_t spl_count = subghz_protocol_raw_get_sample_write(decoder_raw);
    subghz_debug_log_write("stop_and_decode: decoder_raw=%p spl_count=%zu", (void*)decoder_raw, spl_count);
    subghz_protocol_raw_save_to_file_stop(decoder_raw);
    subghz_debug_log_write("stop_and_decode: save_to_file_stop done");

    if(spl_count == 0) {
        subghz_debug_log_write("stop_and_decode: spl_count 0, restarting listening");
        subghz_scene_receiver_delete_tmp_file(subghz);

        subghz_scene_receiver_start_listening(subghz, false);
        return;
    }

    if(subghz_low_ram_mitigate(subghz, SUBGHZ_LOW_RAM_FREE_HEAP_CHAIN_START)) {
        FURI_LOG_W(
            TAG, "stop_and_decode: free heap %zu still low after mitigation, skipping decode",
            memmgr_get_free_heap());
        subghz_debug_log_write(
            "stop_and_decode: free heap %zu still low after mitigation, skipping decode",
            memmgr_get_free_heap());
        subghz_scene_receiver_delete_tmp_file(subghz);

        subghz_txrx_release_protocol_group(subghz->txrx);
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneLowRamWarning);
        return;
    }

    FuriString* temp_str = furi_string_alloc();
    furi_string_printf(
        temp_str, "%s/%s%s", SUBGHZ_RAW_FOLDER, AUTO_READ_TMP_FILE_NAME, SUBGHZ_APP_FILENAME_EXTENSION);
    subghz_protocol_raw_gen_fff_data(
        subghz_txrx_get_fff_data(subghz->txrx),
        furi_string_get_cstr(temp_str),
        subghz_txrx_radio_device_get_name(subghz->txrx));
    furi_string_set(subghz->file_path, temp_str);
    furi_string_free(temp_str);
    subghz_debug_log_write("stop_and_decode: fff_data generated, file_path set");

    s_auto_history_count_before_decode = subghz_history_get_item(subghz->history);
    subghz_debug_log_write(
        "stop_and_decode: history_count_before=%d, allocating file encoder worker",
        s_auto_history_count_before_decode);

    s_auto_decode_group_ordinal = 0;
    s_auto_decode_group_total = 0;
    s_auto_decode_samples_total = spl_count;
    s_auto_decode_samples_done = 0;
    {
        SubGhzGarageProtocolGroup group;
        uint8_t search_from = 0;
        while(subghz_garage_protocol_group_next_enabled(
            subghz->last_settings->protocol_groups_enabled, search_from, &group)) {
            s_auto_decode_group_total++;
            search_from = (uint8_t)group + 1;
        }
    }

    if(!subghz_scene_receiver_decode_open_next_group(subghz, 0)) {
        FURI_LOG_E(TAG, "stop_and_decode: no enabled protocol group could be loaded");
        subghz_debug_log_write(
            "stop_and_decode: no enabled protocol group could be loaded, restarting listening");
        subghz_scene_receiver_delete_tmp_file(subghz);

        subghz_scene_receiver_start_listening(subghz, false);
        return;
    }
    subghz_debug_log_write("stop_and_decode: file encoder worker started, rx_callback + filter set");

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneReceiver, SubGhzReceiverAutoStateDecoding);

    FURI_LOG_I(TAG, "stop_and_decode: decoding started, %zu samples", spl_count);
    subghz_debug_log_write("stop_and_decode: decoding started, %zu samples", spl_count);
}

static void subghz_scene_reader_read_on_enter(SubGhz* subghz) {
    FURI_LOG_I(TAG, "on_enter");

    s_low_ram_resume_attempts = 0;

    rpc_gui_screen_stream_set_suppressed(true);

    subghz_ensure_history(subghz);

    SubGhzReceiverAutoState auto_state = (SubGhzReceiverAutoState)
        scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReceiver);
    if(auto_state == SubGhzReceiverAutoStateListening) {

        subghz_scene_receiver_start_listening(subghz, true);
    } else {

        s_auto_start_entry_fail_count = 0;
        subghz_scene_reader_read_show_start(subghz);
    }

    FURI_LOG_I(TAG, "on_enter: done");
}

static bool subghz_scene_reader_read_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeBack) {

        if(subghz->widget) {
            widget_reset(subghz->widget);
        }
        subghz_scene_reader_read_show_start(subghz);
        s_auto_start_countdown_sec = 0;
        s_auto_start_cancelled = true;
        subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, 0);
        return true;
    }

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SubGhzCustomEventViewReadRAWBack:

        case SubGhzCustomEventViewReadRAWIDLE: {

            SubGhzReceiverAutoState auto_state = (SubGhzReceiverAutoState)
                scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReceiver);
            subghz_debug_log_write(
                "exit_read: enter, event=%lu auto_state=%d",
                (unsigned long)event.event,
                (int)auto_state);

            if(auto_state == SubGhzReceiverAutoStateStart && s_auto_start_countdown_sec > 0) {

                s_auto_start_countdown_sec = 0;
                s_auto_start_cancelled = true;
                subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, 0);
                consumed = true;
                break;
            }

            bool was_reading = (auto_state == SubGhzReceiverAutoStateListening) ||
                               (auto_state == SubGhzReceiverAutoStateDecoding);

            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneReceiver, SubGhzReceiverAutoStateStart);

            if(auto_state == SubGhzReceiverAutoStateDecoding) {
                subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);
                if(subghz->decode_raw_file_worker_encoder) {
                    if(subghz_file_encoder_worker_is_running(subghz->decode_raw_file_worker_encoder)) {
                        subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
                    }
                    subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
                    subghz->decode_raw_file_worker_encoder = NULL;
                }
            } else if(auto_state == SubGhzReceiverAutoStateListening) {
                subghz_debug_log_write("exit_read: stopping txrx");
                subghz_txrx_stop(subghz->txrx);
                subghz_debug_log_write("exit_read: txrx_stop done");
                subghz->state_notifications = SubGhzNotificationStateIDLE;
                SubGhzProtocolDecoderRAW* decoder_raw =
                    (SubGhzProtocolDecoderRAW*)subghz_txrx_get_decoder(subghz->txrx);
                subghz_debug_log_write("exit_read: decoder_raw=%p", (void*)decoder_raw);
                subghz_protocol_raw_save_to_file_stop(decoder_raw);
                subghz_debug_log_write("exit_read: save_to_file_stop done");
                subghz_txrx_hopper_set_state(subghz->txrx, SubGhzHopperStateOFF);
                subghz_debug_log_write("exit_read: hopper off done");
                subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);
                subghz_debug_log_write("exit_read: rx_callback cleared");
            }

            subghz_txrx_release_protocol_group(subghz->txrx);

            subghz_scene_receiver_delete_tmp_file(subghz);
            subghz_debug_log_write("exit_read: tmp file deleted");

            if(was_reading) {
                subghz_debug_log_write("exit_read: read stopped, back to Start without countdown");
                subghz_scene_reader_read_show_start(subghz);
                s_auto_start_countdown_sec = 0;
                s_auto_start_cancelled = true;
                subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, 0);
                consumed = true;
                break;
            }

            subghz_txrx_set_default_preset(subghz->txrx, subghz->last_settings->frequency);
            subghz_debug_log_write("exit_read: default preset set, switching scene");
            subghz_return_to_launcher(subghz);
            subghz_debug_log_write("exit_read: scene switch done");
            consumed = true;
            break;
        }

        case SubGhzCustomEventViewReadRAWConfig: {

            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerSet);

            SubGhzReceiverAutoState auto_state = (SubGhzReceiverAutoState)
                scene_manager_get_scene_state(subghz->scene_manager, SubGhzSceneReceiver);
            if(auto_state == SubGhzReceiverAutoStateListening) {
                SubGhzProtocolDecoderRAW* decoder_raw =
                    (SubGhzProtocolDecoderRAW*)subghz_txrx_get_decoder(subghz->txrx);
                subghz_txrx_stop(subghz->txrx);
                subghz_protocol_raw_save_to_file_stop(decoder_raw);
                subghz_scene_receiver_delete_tmp_file(subghz);
            }

            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverConfig);
            consumed = true;
            break;
        }

        case SubGhzCustomEventViewReadRAWREC:

            subghz_scene_receiver_start_listening(subghz, false);
            consumed = true;
            break;

        case SubGhzCustomEventReceiverNoMatchBack:
            widget_reset(subghz->widget);
            subghz_scene_reader_read_show_start(subghz);
            s_auto_start_countdown_sec = 0;
            s_auto_start_cancelled = true;
            subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, 0);
            consumed = true;
            break;

        case SubGhzCustomEventReceiverNoMatchSave:
            widget_reset(subghz->widget);
            if(subghz_file_available(subghz) && subghz_scene_read_raw_update_filename(subghz)) {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerSetRAW);
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateBack);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveName);
            } else {
                subghz_scene_reader_read_show_start(subghz);
            }
            consumed = true;
            break;

        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        SubGhzReceiverAutoState auto_state = (SubGhzReceiverAutoState)scene_manager_get_scene_state(
            subghz->scene_manager, SubGhzSceneReceiver);

        if(auto_state != SubGhzReceiverAutoStateStart) {
            size_t free_heap = memmgr_get_free_heap();
            bool low_ram_condition = free_heap < SUBGHZ_LOW_RAM_FREE_HEAP_READ;

            if(low_ram_condition) {
                s_low_ram_tick_count++;
            } else {
                s_low_ram_tick_count = 0;
            }

            if(s_low_ram_tick_count >= SUBGHZ_LOW_RAM_TRIGGER_TICKS &&
               !subghz_low_ram_mitigate(subghz, SUBGHZ_LOW_RAM_FREE_HEAP_READ)) {
                s_low_ram_tick_count = 0;
            }

            if(s_low_ram_tick_count >= SUBGHZ_LOW_RAM_TRIGGER_TICKS) {
                uint32_t sustained_ticks = s_low_ram_tick_count;
                s_low_ram_tick_count = 0;

                if(memmgr_get_free_heap() >= SUBGHZ_LOW_RAM_FREE_HEAP_READ) {
                    FURI_LOG_I(
                        TAG,
                        "tick: free heap %zu recovered by bail time (was low for %lu ticks) - skipping warning",
                        memmgr_get_free_heap(),
                        (unsigned long)sustained_ticks);
                    return true;
                }

                bool was_listening = (auto_state == SubGhzReceiverAutoStateListening);
                if(was_listening) {
                    SubGhzProtocolDecoderRAW* decoder_raw =
                        (SubGhzProtocolDecoderRAW*)subghz_txrx_get_decoder(subghz->txrx);
                    subghz_txrx_stop(subghz->txrx);
                    subghz_protocol_raw_save_to_file_stop(decoder_raw);
                    subghz_scene_receiver_delete_tmp_file(subghz);
                }

                furi_delay_ms(SUBGHZ_LOW_RAM_TICK_BAIL_RECHECK_WAIT_MS);
                size_t bail_free_heap = memmgr_get_free_heap();

                bool safe_to_resume = bail_free_heap >=
                                      (SUBGHZ_LOW_RAM_FREE_HEAP + SUBGHZ_GARAGE_WORKER_RAM_COST);
                if(safe_to_resume && s_low_ram_resume_attempts >= SUBGHZ_LOW_RAM_RESUME_ATTEMPTS_MAX) {
                    FURI_LOG_W(
                        TAG,
                        "tick: free heap %zu recovered but already resumed %lu times this visit - forcing full bail",
                        bail_free_heap,
                        (unsigned long)s_low_ram_resume_attempts);
                    subghz_debug_log_write(
                        "tick: free heap %zu recovered but already resumed %lu times this visit - forcing full bail",
                        bail_free_heap,
                        (unsigned long)s_low_ram_resume_attempts);
                    safe_to_resume = false;
                }
                if(safe_to_resume) {
                    s_low_ram_resume_attempts++;
                    FURI_LOG_I(
                        TAG,
                        "tick: free heap %zu recovered with margin after teardown (was low for %lu ticks) - resuming (attempt %lu)",
                        bail_free_heap,
                        (unsigned long)sustained_ticks,
                        (unsigned long)s_low_ram_resume_attempts);
                    subghz_debug_log_write(
                        "tick: free heap %zu recovered with margin after teardown (was low for %lu ticks) - resuming (attempt %lu)",
                        bail_free_heap,
                        (unsigned long)sustained_ticks,
                        (unsigned long)s_low_ram_resume_attempts);
                    if(was_listening) {
                        subghz_scene_receiver_start_listening(subghz, false);
                    }
                    return true;
                }
                if(furi_get_tick() < subghz->low_ram_grace_until_ms) {

                    FURI_LOG_W(
                        TAG,
                        "tick: free heap %zu still low within post-recovery grace, backing out to Start",
                        bail_free_heap);
                    subghz_debug_log_write(
                        "tick: free heap %zu still low within post-recovery grace, backing out to Start",
                        bail_free_heap);

                    if(auto_state == SubGhzReceiverAutoStateDecoding) {
                        subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);
                        if(subghz->decode_raw_file_worker_encoder) {
                            if(subghz_file_encoder_worker_is_running(
                                   subghz->decode_raw_file_worker_encoder)) {
                                subghz_file_encoder_worker_stop(
                                    subghz->decode_raw_file_worker_encoder);
                            }
                            subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
                            subghz->decode_raw_file_worker_encoder = NULL;
                        }
                    }
                    subghz_scene_reader_read_show_start(subghz);
                    return true;
                }
                FURI_LOG_W(
                    TAG,
                    "tick: free heap %zu still low after mitigation, sustained for %lu ticks, bailing to warning",
                    bail_free_heap,
                    (unsigned long)sustained_ticks);
                subghz_debug_log_write(
                    "tick: free heap %zu still low after mitigation, sustained for %lu ticks, bailing to warning",
                    bail_free_heap,
                    (unsigned long)sustained_ticks);

                subghz_txrx_release_protocol_group(subghz->txrx);

                subghz_txrx_release_radio(subghz->txrx);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneLowRamWarning);
                return true;
            }
        } else {
            s_low_ram_tick_count = 0;
        }

        if(auto_state == SubGhzReceiverAutoStateListening) {
            subghz_read_raw_recording_tick(subghz->subghz_read_raw);

            SubGhzProtocolDecoderRAW* decoder_raw =
                (SubGhzProtocolDecoderRAW*)subghz_txrx_get_decoder(subghz->txrx);
            size_t sample_count = subghz_protocol_raw_get_sample_write(decoder_raw);
            subghz_read_raw_update_sample_write(subghz->subghz_read_raw, sample_count);

            float rssi_value = subghz_txrx_radio_device_get_rssi(subghz->txrx);
            SubGhzThresholdRssiData ret_rssi = {.rssi = rssi_value, .is_above = false};
            float user_rssi = subghz_threshold_rssi_get(subghz->threshold_rssi);
            float gate_rssi = user_rssi;
            if(s_auto_tick_count < SUBGHZ_AUTO_READ_SETTLE_TICKS) {
                s_auto_noise_floor = rssi_value;
            } else {
                if(rssi_value < s_auto_noise_floor) {
                    s_auto_noise_floor = rssi_value;
                } else if(!s_auto_has_activity) {
                    s_auto_noise_floor += SUBGHZ_AUTO_READ_NOISE_RISE_DB;
                    if(s_auto_noise_floor > rssi_value) {
                        s_auto_noise_floor = rssi_value;
                    }
                }
                float noise_gate = s_auto_noise_floor + SUBGHZ_AUTO_READ_NOISE_MARGIN_DB;
                if(noise_gate > SUBGHZ_AUTO_READ_NOISE_GATE_MAX) {
                    noise_gate = SUBGHZ_AUTO_READ_NOISE_GATE_MAX;
                }
                if(noise_gate > gate_rssi) {
                    gate_rssi = noise_gate;
                }
                ret_rssi = subghz_threshold_get_rssi_data(
                    subghz->threshold_rssi, rssi_value - (gate_rssi - user_rssi));
                ret_rssi.rssi = rssi_value;
            }
            subghz_read_raw_add_data_rssi(subghz->subghz_read_raw, ret_rssi.rssi, true);

            if(sample_count > s_auto_last_sample_count && ret_rssi.is_above) {
                if(!s_auto_has_activity) {
                    s_auto_activity_start_samples = s_auto_last_sample_count;
                    s_auto_activity_start_tick = s_auto_tick_count;
                    if(s_auto_short_capture_count < SUBGHZ_AUTO_READ_SHORT_CAPTURE_LOG_MAX) {
                        FURI_LOG_I(
                            TAG,
                            "listening: activity started at tick %lu, rssi=%d gate=%d floor=%d threshold=%d samples=%zu",
                            (unsigned long)s_auto_tick_count,
                            (int)rssi_value,
                            (int)gate_rssi,
                            (int)s_auto_noise_floor,
                            (int)user_rssi,
                            sample_count);
                        subghz_debug_log_write(
                            "listening: activity started at tick %lu, rssi=%d gate=%d floor=%d threshold=%d samples=%zu",
                            (unsigned long)s_auto_tick_count,
                            (int)rssi_value,
                            (int)gate_rssi,
                            (int)s_auto_noise_floor,
                            (int)user_rssi,
                            sample_count);
                    }
                }
                s_auto_has_activity = true;
                s_auto_silence_ticks = 0;
            } else if(s_auto_has_activity) {
                s_auto_silence_ticks++;
            }
            s_auto_last_sample_count = sample_count;
            s_auto_tick_count++;

            bool capture_done =
                s_auto_has_activity &&
                (s_auto_silence_ticks >= SUBGHZ_AUTO_READ_SILENCE_TICKS ||
                 (s_auto_tick_count - s_auto_activity_start_tick) >=
                     SUBGHZ_AUTO_READ_MAX_CAPTURE_TICKS);

            if(capture_done &&
               (sample_count - s_auto_activity_start_samples) < SUBGHZ_AUTO_READ_MIN_SAMPLES) {
                if(s_auto_short_capture_count < SUBGHZ_AUTO_READ_SHORT_CAPTURE_LOG_MAX) {
                    FURI_LOG_I(
                        TAG,
                        "listening: only %lu samples since activity start at tick %lu - too short to be a signal, still listening",
                        (unsigned long)(sample_count - s_auto_activity_start_samples),
                        (unsigned long)s_auto_tick_count);
                    subghz_debug_log_write(
                        "listening: only %lu samples since activity start at tick %lu - too short to be a signal, still listening",
                        (unsigned long)(sample_count - s_auto_activity_start_samples),
                        (unsigned long)s_auto_tick_count);
                }
                s_auto_short_capture_count++;
                s_auto_has_activity = false;
                s_auto_silence_ticks = 0;
                subghz_threshold_rssi_reset(subghz->threshold_rssi);
                notification_message(subghz->notifications, &sequence_blink_cyan_10);
            } else if(capture_done) {
                FURI_LOG_I(
                    TAG,
                    "listening: capture ended at tick %lu, quiet ticks %lu, %zu samples",
                    (unsigned long)s_auto_tick_count,
                    (unsigned long)s_auto_silence_ticks,
                    sample_count);
                subghz_debug_log_write(
                    "listening: capture ended at tick %lu, quiet ticks %lu, %zu samples",
                    (unsigned long)s_auto_tick_count,
                    (unsigned long)s_auto_silence_ticks,
                    sample_count);
                subghz_scene_receiver_stop_and_decode(subghz);
            } else {
                notification_message(subghz->notifications, &sequence_blink_cyan_10);
            }

            if(!s_auto_has_activity) {
                subghz_txrx_hopper_update(subghz->txrx, subghz->last_settings->hopping_threshold);
                if(subghz->last_settings->enable_hopping) {
                    subghz_read_raw_set_radio_device_type(
                        subghz->subghz_read_raw, subghz_txrx_radio_device_get(subghz->txrx));
                }
            }
        } else if(auto_state == SubGhzReceiverAutoStateStart && !s_auto_start_cancelled) {

            s_auto_start_subtick++;
            if(s_auto_start_subtick >= 10) {
                s_auto_start_subtick = 0;
                if(s_auto_start_countdown_sec > 0) {
                    s_auto_start_countdown_sec--;
                }
                if(s_auto_start_countdown_sec == 0) {
                    if(s_auto_start_entry_fail_count >= SUBGHZ_LOW_RAM_RESUME_ATTEMPTS_MAX) {

                        FURI_LOG_W(
                            TAG,
                            "auto-start countdown: %lu consecutive failed auto-starts - pausing, awaiting manual Start",
                            (unsigned long)s_auto_start_entry_fail_count);
                        subghz_debug_log_write(
                            "auto-start countdown: %lu consecutive failed auto-starts - pausing, awaiting manual Start",
                            (unsigned long)s_auto_start_entry_fail_count);
                        s_auto_start_cancelled = true;
                        subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, 0);
                    } else {
                        subghz_scene_receiver_start_listening(subghz, false);
                        return true;
                    }
                } else {
                    subghz_read_raw_set_start_countdown(
                        subghz->subghz_read_raw, s_auto_start_countdown_sec);
                }
            }
        } else if(subghz->decode_raw_file_worker_encoder) {
            subghz_debug_log_write("decode tick: calling decode_next");
            bool more = subghz_scene_receiver_decode_next(subghz);
            subghz_debug_log_write("decode tick: decode_next returned %d", (int)more);
            if(more) {
                subghz_scene_receiver_decode_show_progress(subghz);
            }
            if(!more) {

                uint16_t history_count = subghz_history_get_item(subghz->history);
                if(history_count > s_auto_history_count_before_decode) {
                    subghz_debug_log_write("decode tick: matched, calling decode_finish");
                    subghz_scene_receiver_decode_finish(subghz);
                    subghz_debug_log_write("decode tick: decode_finish returned");
                } else {
                    subghz_debug_log_write(
                        "decode tick: no match in group %d, trying next enabled group",
                        (int)s_auto_decode_group_cursor);
                    if(!subghz_scene_receiver_decode_open_next_group(
                           subghz, (uint8_t)(s_auto_decode_group_cursor + 1))) {
                        subghz_debug_log_write("decode tick: no groups left, calling decode_finish");
                        subghz_scene_receiver_decode_finish(subghz);
                        subghz_debug_log_write("decode tick: decode_finish returned");
                    }
                }
            }
        }

    }
    return consumed;
}

static void subghz_scene_reader_read_on_exit(SubGhz* subghz) {

    rpc_gui_screen_stream_set_suppressed(false);
    subghz_cli_soft_unlock(subghz);

    SubGhzReceiverAutoState auto_state = (SubGhzReceiverAutoState)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneReceiver);
    if(auto_state == SubGhzReceiverAutoStateDecoding) {
        subghz_txrx_set_rx_callback(subghz->txrx, NULL, subghz);
        if(subghz->decode_raw_file_worker_encoder) {
            if(subghz_file_encoder_worker_is_running(subghz->decode_raw_file_worker_encoder)) {
                subghz_file_encoder_worker_stop(subghz->decode_raw_file_worker_encoder);
            }
            subghz_file_encoder_worker_free(subghz->decode_raw_file_worker_encoder);
            subghz->decode_raw_file_worker_encoder = NULL;
        }
        subghz_scene_receiver_delete_tmp_file(subghz);
    }
    if(subghz->popup) {
        popup_reset(subghz->popup);
    }
    if(subghz->widget) {
        widget_reset(subghz->widget);
    }
}

void subghz_scene_read_raw_on_enter(void* context) {
    SubGhz* subghz = context;

    subghz_read_raw_set_auto_capture_mode(subghz->subghz_read_raw, subghz->reader_read_mode);

    if(subghz->reader_read_mode) {
        subghz_scene_reader_read_on_enter(subghz);
        return;
    }

    FURI_LOG_I(TAG, "on_enter");

    rpc_gui_screen_stream_set_suppressed(true);

    FuriString* file_name = furi_string_alloc();

    float threshold_rssi = subghz_threshold_rssi_get(subghz->threshold_rssi);
    switch(subghz_rx_key_state_get(subghz)) {
    case SubGhzRxKeyStateBack:
        subghz_read_raw_set_status(
            subghz->subghz_read_raw, SubGhzReadRAWStatusIDLE, "", threshold_rssi);
        break;
    case SubGhzRxKeyStateRAWLoad:
    case SubGhzRxKeyStateRAWMore:
        path_extract_filename(subghz->file_path, file_name, true);
        subghz_read_raw_set_status(
            subghz->subghz_read_raw,
            SubGhzReadRAWStatusLoadKeyTX,
            furi_string_get_cstr(file_name),
            threshold_rssi);

        subghz_read_raw_set_allow_new(subghz->subghz_read_raw, false);

        {
            uint8_t zoom = (uint8_t)subghz->last_settings->raw_playback_zoom_level;
            subghz_read_raw_set_zoom_level(subghz->subghz_read_raw, zoom);
            if(zoom > 0) {
                subghz_scene_read_raw_load_envelope_zoomed(subghz, zoom, 0);
            } else {
                subghz_scene_read_raw_load_envelope(subghz);
            }
        }
        break;
    case SubGhzRxKeyStateRAWSave:
        path_extract_filename(subghz->file_path, file_name, true);
        subghz_read_raw_set_status(
            subghz->subghz_read_raw,
            SubGhzReadRAWStatusSaveKey,
            furi_string_get_cstr(file_name),
            threshold_rssi);

        subghz_read_raw_set_allow_new(subghz->subghz_read_raw, true);
        break;
    default:
        subghz_read_raw_set_status(
            subghz->subghz_read_raw, SubGhzReadRAWStatusStart, "", threshold_rssi);
        s_manual_start_countdown_sec = SUBGHZ_AUTO_START_COUNTDOWN_SEC;
        s_manual_start_subtick = 0;
        s_manual_start_countdown_fired = false;
        s_manual_start_cancelled = false;
        subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, s_manual_start_countdown_sec);
        break;
    }

    if((subghz_rx_key_state_get(subghz) != SubGhzRxKeyStateBack) &&
       (subghz_rx_key_state_get(subghz) != SubGhzRxKeyStateRAWLoad)) {
        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);

        if(furi_string_empty(file_name)) {
            subghz_txrx_set_preset_internal(
                subghz->txrx,
                subghz->last_settings->frequency,
                subghz->last_settings->preset_index,
                subghz->last_settings->tx_power);
        }
    }
    subghz_scene_read_raw_update_statusbar(subghz);

    subghz_read_raw_set_callback(subghz->subghz_read_raw, subghz_scene_read_raw_callback, subghz);

    subghz_read_raw_set_viz_mode(
        subghz->subghz_read_raw,
        (SubGhzReadRawVizMode)subghz->last_settings->visualizer_display_mode);

    subghz_txrx_reset_protocol_load_failed(subghz->txrx);
    if(!subghz_txrx_load_decoder_by_name_protocol(subghz->txrx, SUBGHZ_PROTOCOL_RAW_NAME)) {
        furi_string_free(file_name);
        scene_manager_next_scene(subghz->scene_manager, SubGhzSceneProtocolLoadError);
        return;
    }

    subghz_txrx_receiver_set_filter(subghz->txrx, SubGhzProtocolFlag_RAW);
    furi_string_free(file_name);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdReadRAW);
}

bool subghz_scene_read_raw_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(subghz->reader_read_mode) {
        return subghz_scene_reader_read_on_event(context, event);
    }

    bool consumed = false;
    SubGhzProtocolDecoderRAW* decoder_raw =
        (SubGhzProtocolDecoderRAW*)subghz_txrx_get_decoder(subghz->txrx);
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SubGhzCustomEventViewReadRAWBack:

            if(subghz_read_raw_get_status(subghz->subghz_read_raw) == SubGhzReadRAWStatusStart &&
               s_manual_start_countdown_sec > 0) {

                s_manual_start_countdown_sec = 0;
                s_manual_start_cancelled = true;
                subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, 0);
                consumed = true;
                break;
            }

            subghz_txrx_stop(subghz->txrx);

            subghz_protocol_raw_save_to_file_stop(decoder_raw);
            subghz->state_notifications = SubGhzNotificationStateIDLE;

            if((subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateAddKey) ||
               (subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateBack)) {
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateExit);
                if(subghz_scene_read_raw_update_filename(subghz)) {
                    furi_string_set(subghz->file_path_tmp, subghz->file_path);
                } else {
                    furi_string_reset(subghz->file_path_tmp);
                }
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneNeedSaving);
            } else {

                if(subghz->raw_send_only) {
                    subghz_txrx_set_default_preset(subghz->txrx, 0);
                } else {
                    subghz_txrx_set_default_preset(subghz->txrx, subghz->last_settings->frequency);
                }

                subghz_txrx_release_protocol_group(subghz->txrx);
                if(!scene_manager_search_and_switch_to_previous_scene(
                       subghz->scene_manager, SubGhzSceneSaved)) {
                    subghz_return_to_launcher(subghz);
                }
            }
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWTXPause:

            subghz_txrx_stop(subghz->txrx);
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            notification_message(subghz->notifications, &sequence_reset_rgb);
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWTXResume: {

            FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
            if(fff) {
                flipper_format_rewind(fff);
                if(subghz_tx_start(subghz, fff)) {
                    subghz->state_notifications = SubGhzNotificationStateTx;
                }
            }
            consumed = true;
            break;
        }

        case SubGhzCustomEventViewReadRAWZoomIn:
        case SubGhzCustomEventViewReadRAWZoomOut: {

            uint8_t zoom = subghz_read_raw_get_zoom_level(subghz->subghz_read_raw);
            uint8_t center = subghz_read_raw_get_seek_pct(subghz->subghz_read_raw);
            if(zoom > 0) {
                subghz_scene_read_raw_load_envelope_zoomed(subghz, zoom, center);
            } else {
                subghz_scene_read_raw_load_envelope(subghz);
            }
            subghz->last_settings->raw_playback_zoom_level = zoom;
            subghz_garage_last_settings_save(subghz->last_settings);
            consumed = true;
            break;
        }

        case SubGhzCustomEventViewReadRAWTXRXStop:
            subghz_txrx_stop(subghz->txrx);
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWConfig:
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerSet);
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiverConfig);
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWErase:
            if((subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateAddKey) ||
               (subghz_rx_key_state_get(subghz) == SubGhzRxKeyStateBack)) {
                if(subghz_scene_read_raw_update_filename(subghz)) {
                    furi_string_set(subghz->file_path_tmp, subghz->file_path);
                    subghz_delete_file(subghz);
                }
            }
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateIDLE);
            notification_message(subghz->notifications, &sequence_reset_rgb);

            s_manual_start_countdown_sec = SUBGHZ_AUTO_START_COUNTDOWN_SEC;
            s_manual_start_subtick = 0;
            s_manual_start_countdown_fired = false;
            s_manual_start_cancelled = false;
            subghz_read_raw_set_start_countdown(subghz->subghz_read_raw, s_manual_start_countdown_sec);
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWMore:
            if(subghz_file_available(subghz)) {
                if(subghz_scene_read_raw_update_filename(subghz)) {
                    scene_manager_set_scene_state(
                        subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerSet);
                    if(subghz_rx_key_state_get(subghz) != SubGhzRxKeyStateRAWLoad) {
                        subghz_rx_key_state_set(subghz, SubGhzRxKeyStateRAWMore);
                    }
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneMoreRAW);
                    consumed = true;
                } else {
                    furi_crash("SubGhz: RAW file name update error.");
                }
            } else {

                subghz_txrx_release_protocol_group(subghz->txrx);
                subghz_return_to_launcher(subghz);
            }
            break;

        case SubGhzCustomEventViewReadRAWSendStart:

            if(subghz_file_available(subghz) && subghz_scene_read_raw_update_filename(subghz)) {

                subghz->state_notifications = SubGhzNotificationStateIDLE;
                if(!subghz_tx_start(subghz, subghz_txrx_get_fff_data(subghz->txrx))) {
                    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateBack);
                    subghz_read_raw_set_status(
                        subghz->subghz_read_raw,
                        SubGhzReadRAWStatusIDLE,
                        "",
                        subghz_threshold_rssi_get(subghz->threshold_rssi));
                } else {
                    subghz_txrx_set_raw_file_encoder_worker_callback_end(
                        subghz->txrx, subghz_scene_read_raw_callback_end_tx, subghz);
                    subghz->state_notifications = SubGhzNotificationStateTx;
                }
            } else {

                subghz_txrx_release_protocol_group(subghz->txrx);
                subghz_return_to_launcher(subghz);
            }
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWSendStop:
            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_txrx_stop(subghz->txrx);
            subghz_read_raw_stop_send(subghz->subghz_read_raw);
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWIDLE:
            subghz_txrx_stop(subghz->txrx);
            size_t spl_count = subghz_protocol_raw_get_sample_write(decoder_raw);

            subghz_protocol_raw_save_to_file_stop(decoder_raw);

            FuriString* temp_str = furi_string_alloc();
            furi_string_printf(
                temp_str,
                "%s/%s%s",
                SUBGHZ_RAW_FOLDER,
                RAW_FILE_NAME,
                SUBGHZ_APP_FILENAME_EXTENSION);

            {

                uint32_t rec_ticks  = subghz_read_raw_get_recording_ticks(subghz->subghz_read_raw);
                uint32_t expected_us = (rec_ticks > 0 ? rec_ticks : 1u) * 100000u;

                if(expected_us > 0) {
                    Storage* pad_storage  = furi_record_open(RECORD_STORAGE);
                    File*    pad_file     = storage_file_alloc(pad_storage);

                    if(storage_file_open(
                           pad_file,
                           furi_string_get_cstr(temp_str),
                           FSAM_READ_WRITE,
                           FSOM_OPEN_EXISTING)) {

                        uint64_t fsz = storage_file_size(pad_file);

                        bool has_raw_data = false;
                        if(fsz >= 10u) {
                            uint8_t  scan_buf[256];
                            storage_file_seek(pad_file, 0, true);
                            uint16_t n_read = storage_file_read(pad_file, scan_buf, sizeof(scan_buf) - 1);
                            scan_buf[n_read] = '\0';

                            uint64_t remaining = fsz;
                            has_raw_data = (strstr((char*)scan_buf, "RAW_Data") != NULL);
                            if(!has_raw_data && remaining > (uint64_t)n_read) {

                                while(remaining > (uint64_t)n_read && !has_raw_data) {
                                    n_read = storage_file_read(pad_file, scan_buf, sizeof(scan_buf) - 1);
                                    if(n_read == 0) break;
                                    scan_buf[n_read] = '\0';
                                    has_raw_data = (strstr((char*)scan_buf, "RAW_Data") != NULL);
                                }
                            }
                        }

                        uint32_t rem = expected_us;

                        const uint32_t SILENCE_CHUNK = 999999u;
                        char  buf[32];
                        int   wn;
                        bool  first_chunk = true;
                        uint32_t rem2 = rem;

                        if(has_raw_data) {
                            storage_file_seek(pad_file, (uint32_t)(fsz - 1u), true);
                        } else {
                            storage_file_seek(pad_file, (uint32_t)fsz, true);
                        }

                        while(rem2 > 0) {
                            uint32_t chunk = (rem2 > SILENCE_CHUNK) ? SILENCE_CHUNK : rem2;
                            uint32_t sil   = (chunk > 1u) ? chunk - 1u : 0u;
                            if(first_chunk) {
                                if(has_raw_data) {
                                    wn = sil ? snprintf(buf, sizeof(buf), " 1 -%lu", (unsigned long)sil)
                                             : snprintf(buf, sizeof(buf), " 1");
                                } else {
                                    wn = sil ? snprintf(buf, sizeof(buf), "RAW_Data: 1 -%lu", (unsigned long)sil)
                                             : snprintf(buf, sizeof(buf), "RAW_Data: 1");
                                }
                                first_chunk = false;
                            } else {
                                wn = sil ? snprintf(buf, sizeof(buf), " 1 -%lu", (unsigned long)sil)
                                         : snprintf(buf, sizeof(buf), " 1");
                            }
                            if(wn > 0) storage_file_write(pad_file, buf, (size_t)wn);
                            rem2 -= chunk;
                        }

                        storage_file_write(pad_file, " 1\n", 3u);
                    }

                    storage_file_close(pad_file);
                    storage_file_free(pad_file);
                    furi_record_close(RECORD_STORAGE);
                }
            }
            subghz_protocol_raw_gen_fff_data(
                subghz_txrx_get_fff_data(subghz->txrx),
                furi_string_get_cstr(temp_str),
                subghz_txrx_radio_device_get_name(subghz->txrx));

            furi_string_set(subghz->file_path, temp_str);
            furi_string_free(temp_str);

            subghz_scene_read_raw_load_envelope(subghz);

            if(spl_count > 0) {
                notification_message(subghz->notifications, &sequence_set_green_255);
            } else {
                notification_message(subghz->notifications, &sequence_reset_rgb);
            }

            subghz->state_notifications = SubGhzNotificationStateIDLE;
            subghz_rx_key_state_set(subghz, SubGhzRxKeyStateAddKey);

            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWREC:
            if(subghz_rx_key_state_get(subghz) != SubGhzRxKeyStateIDLE) {
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneNeedSaving);
            } else {

                subghz_low_ram_mitigate(subghz, SUBGHZ_LOW_RAM_FREE_HEAP_CHAIN_START);

                SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
                if(subghz_protocol_raw_save_to_file_init(decoder_raw, RAW_FILE_NAME, &preset)) {
                    subghz_txrx_rx_start(subghz->txrx);
                    subghz_read_raw_set_radio_device_type(
                        subghz->subghz_read_raw, subghz_txrx_radio_device_get(subghz->txrx));
                    subghz->state_notifications = SubGhzNotificationStateRx;

                    subghz_rx_key_state_set(subghz, SubGhzRxKeyStateAddKey);
                } else if(subghz_low_ram_mitigate(subghz, SUBGHZ_LOW_RAM_FREE_HEAP_CHAIN_START)) {

                    FURI_LOG_W(
                        TAG,
                        "REC: save_to_file_init failed with free heap %zu still low after mitigation",
                        memmgr_get_free_heap());

                    subghz_txrx_release_protocol_group(subghz->txrx);
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneLowRamWarning);
                } else {
                    furi_string_set(subghz->error_str, "Function requires\nan SD card.");
                    scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
                }
            }
            consumed = true;
            break;

        case SubGhzCustomEventViewReadRAWSave:
            if(subghz_file_available(subghz) && subghz_scene_read_raw_update_filename(subghz)) {
                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneReadRAW, SubGhzCustomEventManagerSetRAW);
                subghz_rx_key_state_set(subghz, SubGhzRxKeyStateBack);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveName);
            } else {

                subghz_txrx_release_protocol_group(subghz->txrx);
                subghz_return_to_launcher(subghz);
            }
            consumed = true;
            break;

        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        switch(subghz->state_notifications) {
        case SubGhzNotificationStateRx:
            notification_message(subghz->notifications, &sequence_blink_cyan_10);
            subghz_read_raw_recording_tick(subghz->subghz_read_raw);

            subghz_read_raw_update_sample_write(
                subghz->subghz_read_raw, subghz_protocol_raw_get_sample_write(decoder_raw));

            SubGhzThresholdRssiData ret_rssi = subghz_threshold_get_rssi_data(
                subghz->threshold_rssi, subghz_txrx_radio_device_get_rssi(subghz->txrx));
            subghz_read_raw_add_data_rssi(
                subghz->subghz_read_raw, ret_rssi.rssi, true );

            break;
        case SubGhzNotificationStateTx:
            notification_message(subghz->notifications, &sequence_blink_magenta_10);
            subghz_read_raw_tick_tx(subghz->subghz_read_raw);

            if(subghz_read_raw_is_playback_overdue(subghz->subghz_read_raw)) {
                view_dispatcher_send_custom_event(
                    subghz->view_dispatcher, SubGhzCustomEventViewReadRAWSendStop);
            }
            break;
        default:

            if(!s_manual_start_countdown_fired && !s_manual_start_cancelled &&
               subghz_read_raw_get_status(subghz->subghz_read_raw) ==
                   SubGhzReadRAWStatusStart) {
                s_manual_start_subtick++;
                if(s_manual_start_subtick >= 10) {
                    s_manual_start_subtick = 0;
                    if(s_manual_start_countdown_sec > 0) {
                        s_manual_start_countdown_sec--;
                        subghz_read_raw_set_start_countdown(
                            subghz->subghz_read_raw, s_manual_start_countdown_sec);
                    }
                    if(s_manual_start_countdown_sec == 0) {
                        s_manual_start_countdown_fired = true;

                        subghz_read_raw_set_status(
                            subghz->subghz_read_raw,
                            SubGhzReadRAWStatusREC,
                            "",
                            subghz_threshold_rssi_get(subghz->threshold_rssi));
                        view_dispatcher_send_custom_event(
                            subghz->view_dispatcher, SubGhzCustomEventViewReadRAWREC);
                    }
                }
            }
            break;
        }
    }
    return consumed;
}

void subghz_scene_read_raw_on_exit(void* context) {
    SubGhz* subghz = context;

    if(subghz->reader_read_mode) {
        subghz_scene_reader_read_on_exit(subghz);
        return;
    }

    rpc_gui_screen_stream_set_suppressed(false);
    subghz_cli_soft_unlock(subghz);

    subghz_txrx_stop(subghz->txrx);
    subghz->state_notifications = SubGhzNotificationStateIDLE;
    notification_message(subghz->notifications, &sequence_reset_rgb);

    subghz_txrx_receiver_set_filter(subghz->txrx, subghz->filter);
}

void subghz_scene_receiver_on_enter(void* context) {
    SubGhz* subghz = context;
    subghz->reader_read_mode = true;
    subghz_scene_read_raw_on_enter(context);
}

bool subghz_scene_receiver_on_event(void* context, SceneManagerEvent event) {
    return subghz_scene_read_raw_on_event(context, event);
}

void subghz_scene_receiver_on_exit(void* context) {
    SubGhz* subghz = context;
    subghz_scene_read_raw_on_exit(context);
    subghz->reader_read_mode = false;
}
