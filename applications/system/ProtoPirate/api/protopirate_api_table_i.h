#include <subghz/devices/cc1101_configs.h>
#include <subghz/environment.h>
#include <subghz/receiver.h>
#include <subghz/transmitter.h>
#include <subghz/subghz_setting.h>
#include <subghz/subghz_keystore.h>
#include <subghz/subghz_worker.h>
#include <subghz/devices/devices.h>
#include <subghz/blocks/generic.h>
#include <subghz/blocks/decoder.h>
#include <subghz/blocks/math.h>
#include <subghz/protocols/base.h>

/*
 * ProtoPirate private API table. ProtoPirate loads protocol plugins (.fal) at
 * runtime; those plugins import SubGhz library symbols that used to live in the
 * firmware API but now live inside this app's private SubGhz library. This
 * table exposes them (plus the CC1101 presets for radio_device_cc1101_ext) so
 * the composite resolver can satisfy the plugins at load time.
 */
static constexpr auto subghz_app_api_table = sort(create_array_t<sym_entry>(
    API_METHOD(
        subghz_block_generic_deserialize,
        SubGhzProtocolStatus,
        (SubGhzBlockGeneric* instance, FlipperFormat* flipper_format)),
    API_METHOD(
        subghz_block_generic_deserialize_check_count_bit,
        SubGhzProtocolStatus,
        (SubGhzBlockGeneric* instance, FlipperFormat* flipper_format, uint16_t count_bit)),
    API_METHOD(
        subghz_block_generic_serialize,
        SubGhzProtocolStatus,
        (SubGhzBlockGeneric* instance, FlipperFormat* flipper_format, SubGhzRadioPreset* preset)),
    API_METHOD(
        subghz_devices_idle,
        void,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_devices_load_preset,
        void,
        (const SubGhzDevice* device, FuriHalSubGhzPreset preset, uint8_t* preset_data)),
    API_METHOD(
        subghz_devices_reset,
        void,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_devices_set_frequency,
        uint32_t,
        (const SubGhzDevice* device, uint32_t frequency)),
    API_METHOD(
        subghz_devices_set_tx,
        bool,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_devices_start_async_tx,
        bool,
        (const SubGhzDevice* device, void* callback, void* context)),
    API_METHOD(
        subghz_devices_stop_async_tx,
        void,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_environment_get_keystore,
        SubGhzKeystore*,
        (SubGhzEnvironment* instance)),
    API_METHOD(
        subghz_keystore_get_data,
        SubGhzKeyArray_t*,
        (SubGhzKeystore* instance)),
    API_METHOD(
        subghz_keystore_raw_get_data,
        bool,
        (const char* file_name, size_t offset, uint8_t* data, size_t len)),
    API_METHOD(
        subghz_protocol_blocks_add_bit,
        void,
        (SubGhzBlockDecoder* decoder, uint8_t bit)),
    API_METHOD(
        subghz_protocol_blocks_crc16,
        uint16_t,
        (uint8_t const message[], size_t size, uint16_t polynomial, uint16_t init)),
    API_METHOD(
        subghz_protocol_blocks_crc8,
        uint8_t,
        (uint8_t const message[], size_t size, uint8_t polynomial, uint8_t init)),
    API_METHOD(
        subghz_protocol_blocks_get_hash_data,
        uint8_t,
        (SubGhzBlockDecoder* decoder, size_t len)),
    API_METHOD(
        subghz_protocol_blocks_parity8,
        uint8_t,
        (uint8_t byte)),
    API_METHOD(
        subghz_protocol_blocks_reverse_key,
        uint64_t,
        (uint64_t key, uint8_t bit_count)),
    API_METHOD(
        subghz_protocol_decoder_base_get_hash_data,
        uint8_t,
        (SubGhzProtocolDecoderBase* decoder_base)),
    API_METHOD(
        subghz_protocol_decoder_base_get_string,
        bool,
        (SubGhzProtocolDecoderBase* decoder_base, FuriString* output)),
    API_METHOD(
        subghz_protocol_decoder_base_serialize,
        SubGhzProtocolStatus,
        (SubGhzProtocolDecoderBase* decoder_base, FlipperFormat* flipper_format, SubGhzRadioPreset* preset)),
    API_METHOD(
        subghz_receiver_decode,
        void,
        (SubGhzReceiver* instance, bool level, uint32_t duration)),
    API_METHOD(
        subghz_receiver_reset,
        void,
        (SubGhzReceiver* instance)),
    API_METHOD(
        subghz_receiver_set_rx_callback,
        void,
        (SubGhzReceiver* instance, SubGhzReceiverCallback callback, void* context)),
    API_METHOD(
        subghz_setting_get_inx_preset_by_name,
        int,
        (SubGhzSetting* instance, const char* preset_name)),
    API_METHOD(
        subghz_setting_get_preset_count,
        size_t,
        (SubGhzSetting* instance)),
    API_METHOD(
        subghz_setting_get_preset_data,
        uint8_t*,
        (SubGhzSetting* instance, size_t idx)),
    API_METHOD(
        subghz_setting_get_preset_data_size,
        size_t,
        (SubGhzSetting* instance, size_t idx)),
    API_METHOD(
        subghz_setting_get_preset_name,
        const char*,
        (SubGhzSetting* instance, size_t idx)),
    API_METHOD(
        subghz_transmitter_alloc_init,
        SubGhzTransmitter*,
        (SubGhzEnvironment* environment, const char* protocol_name)),
    API_METHOD(
        subghz_transmitter_deserialize,
        SubGhzProtocolStatus,
        (SubGhzTransmitter* instance, FlipperFormat* flipper_format)),
    API_METHOD(
        subghz_transmitter_free,
        void,
        (SubGhzTransmitter* instance)),
    API_METHOD(
        subghz_transmitter_stop,
        bool,
        (SubGhzTransmitter* instance)),
    API_METHOD(
        subghz_transmitter_yield,
        LevelDuration,
        (void* context)),
    /* RX-path symbols imported by protopirate_config_plugin (v3.4) */
    API_METHOD(
        subghz_devices_init,
        void,
        (void)),
    API_METHOD(
        subghz_devices_deinit,
        void,
        (void)),
    API_METHOD(
        subghz_devices_begin,
        bool,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_devices_end,
        void,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_devices_get_by_name,
        const SubGhzDevice*,
        (const char* device_name)),
    API_METHOD(
        subghz_devices_is_connect,
        bool,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_environment_alloc,
        SubGhzEnvironment*,
        (void)),
    API_METHOD(
        subghz_environment_free,
        void,
        (SubGhzEnvironment* instance)),
    API_METHOD(
        subghz_environment_load_keystore,
        bool,
        (SubGhzEnvironment* instance, const char* filename)),
    API_METHOD(
        subghz_devices_flush_rx,
        void,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_devices_get_rssi,
        float,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_devices_is_frequency_valid,
        bool,
        (const SubGhzDevice* device, uint32_t frequency)),
    API_METHOD(
        subghz_devices_set_async_mirror_pin,
        void,
        (const SubGhzDevice* device, const GpioPin* gpio)),
    API_METHOD(
        subghz_devices_set_rx,
        void,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_devices_sleep,
        void,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_devices_start_async_rx,
        void,
        (const SubGhzDevice* device, void* callback, void* context)),
    API_METHOD(
        subghz_devices_stop_async_rx,
        void,
        (const SubGhzDevice* device)),
    API_METHOD(
        subghz_environment_set_protocol_registry,
        void,
        (SubGhzEnvironment* instance, const SubGhzProtocolRegistry* protocol_registry_items)),
    API_METHOD(
        subghz_receiver_alloc_init,
        SubGhzReceiver*,
        (SubGhzEnvironment* environment)),
    API_METHOD(
        subghz_receiver_free,
        void,
        (SubGhzReceiver* instance)),
    API_METHOD(
        subghz_receiver_set_filter,
        void,
        (SubGhzReceiver* instance, SubGhzProtocolFlag filter)),
    API_METHOD(
        subghz_setting_get_default_frequency,
        uint32_t,
        (SubGhzSetting* instance)),
    API_METHOD(
        subghz_setting_get_frequency,
        uint32_t,
        (SubGhzSetting* instance, size_t idx)),
    API_METHOD(
        subghz_setting_get_frequency_count,
        size_t,
        (SubGhzSetting* instance)),
    API_METHOD(
        subghz_setting_get_frequency_default_index,
        uint32_t,
        (SubGhzSetting* instance)),
    API_METHOD(
        subghz_setting_get_hopper_frequency,
        uint32_t,
        (SubGhzSetting* instance, size_t idx)),
    API_METHOD(
        subghz_setting_get_hopper_frequency_count,
        size_t,
        (SubGhzSetting* instance)),
    API_METHOD(
        subghz_worker_free,
        void,
        (SubGhzWorker* instance)),
    API_METHOD(
        subghz_worker_is_running,
        bool,
        (SubGhzWorker* instance)),
    API_METHOD(
        subghz_worker_rx_callback,
        void,
        (bool level, uint32_t duration, void* context)),
    API_METHOD(
        subghz_worker_start,
        void,
        (SubGhzWorker* instance)),
    API_METHOD(
        subghz_worker_stop,
        void,
        (SubGhzWorker* instance)),
    API_VARIABLE(subghz_device_cc1101_preset_ook_270khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_ook_650khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_2fsk_dev2_38khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_2fsk_dev12khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_2fsk_dev47_6khz_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_msk_99_97kb_async_regs, const uint8_t[]),
    API_VARIABLE(subghz_device_cc1101_preset_gfsk_9_99kb_async_regs, const uint8_t[])));
