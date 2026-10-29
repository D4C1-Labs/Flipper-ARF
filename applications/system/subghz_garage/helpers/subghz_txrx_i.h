#pragma once

#include "subghz_txrx.h"
#include <lib/flipper_application/plugins/plugin_manager.h>
#include <lib/flipper_application/plugins/composite_resolver.h>

struct SubGhzTxRx {
    SubGhzGarageWorker* worker;

    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzTransmitter* transmitter;
    SubGhzProtocolDecoderBase* decoder_result;
    FlipperFormat* fff_data;

    SubGhzRadioPreset* preset;
    SubGhzSetting* setting;

    uint8_t hopper_timeout;
    uint8_t hopper_idx_frequency;
    bool is_database_loaded;

    bool keystore_loaded;

    bool external_device_loaded;

    bool radio_initialized;

    bool radio_registry_loaded;
    SubGhzHopperState hopper_state;

    SubGhzTxRxState txrx_state;
    SubGhzSpeakerState speaker_state;
    const SubGhzDevice* radio_device;
    SubGhzRadioDeviceType radio_device_type;

    SubGhzTxRxNeedSaveCallback need_save_callback;
    void* need_save_context;

    bool debug_pin_state;

    SubGhzGarageProtocolGroup active_protocol_group;
    const SubGhzProtocolRegistry* protocol_registry;
    const SubGhzGarageProtocolPlugin* protocol_plugin;
    PluginManager* protocol_plugin_manager;
    CompositeApiResolver* protocol_plugin_resolver;

    bool protocol_plugin_load_failed;

    bool tx_protocol_plugin_loaded;
    SubGhzGarageTxProtocol active_tx_protocol;
    const SubGhzGarageProtocolPlugin* tx_protocol_plugin;
    PluginManager* tx_protocol_plugin_manager;
    CompositeApiResolver* tx_protocol_plugin_resolver;

    SubGhzProtocolFlag receiver_filter;
    bool receiver_filter_set;
    SubGhzReceiverCallback rx_callback;
    void* rx_callback_context;
};
