#include "gallagher/gallagher_util.h"
#include "mosgortrans/mosgortrans_util.h"
#include "../nfc_app_i.h"
#include "../helpers/protocol_support/nfc_protocol_support_gui_common.h"
#include "../helpers/protocol_support/nfc_protocol_support_unlock_helper.h"
#include "../helpers/nfc_emv_parser.h"
#include "../helpers/protocol_support/emv/emv_render.h"
#include <nfc/nfc_device.h>
#include <nfc/nfc_listener.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/felica/felica.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3b/iso14443_3b.h>
#include <nfc/protocols/iso14443_4a/iso14443_4a.h>
#include <nfc/protocols/iso14443_4b/iso14443_4b.h>
#include <nfc/protocols/iso15693_3/iso15693_3.h>
#include <nfc/protocols/mf_classic/mf_classic.h>
#include <nfc/protocols/mf_classic/mf_classic_poller_sync.h>
#include <nfc/protocols/mf_desfire/mf_desfire.h>
#include <nfc/protocols/mf_plus/mf_plus.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <nfc/protocols/ntag4xx/ntag4xx.h>
#include <nfc/protocols/slix/slix.h>
#include <nfc/protocols/st25tb/st25tb.h>
#include <nfc/protocols/type_4_tag/type_4_tag.h>

/*
 * A list of app's private functions and objects to expose for plugins.
 * It is used to generate a table of symbols for import resolver to use.
 *
 * The NFC protocol library now lives inside this FAP (it was removed from the
 * firmware), so plugins (.fal) that import lib/nfc symbols must resolve them
 * against this table via the app's composite resolver. The entries below the
 * app-private helpers are the lib/nfc symbols the bundled card parsers and
 * protocol plugins reference at runtime.
 */
static constexpr auto nfc_app_api_table = sort(create_array_t<sym_entry>(
    API_METHOD(
        gallagher_deobfuscate_and_parse_credential,
        void,
        (GallagherCredential * credential, const uint8_t* cardholder_data_obfuscated)),
    API_VARIABLE(GALLAGHER_CARDAX_ASCII, const uint8_t*),
    API_METHOD(
        mosgortrans_parse_transport_block,
        bool,
        (const MfClassicBlock* block, FuriString* result)),
    API_METHOD(
        render_section_header,
        void,
        (FuriString * str,
         const char* name,
         uint8_t prefix_separator_cnt,
         uint8_t suffix_separator_cnt)),
    API_METHOD(
        nfc_append_filename_string_when_present,
        void,
        (NfcApp * instance, FuriString* string)),
    API_METHOD(nfc_protocol_support_common_submenu_callback, void, (void* context, uint32_t index)),
    API_METHOD(
        nfc_protocol_support_common_widget_callback,
        void,
        (GuiButtonType result, InputType type, void* context)),
    API_METHOD(nfc_protocol_support_common_on_enter_empty, void, (NfcApp * instance)),
    API_METHOD(
        nfc_protocol_support_common_on_event_empty,
        bool,
        (NfcApp * instance, SceneManagerEvent event)),
    API_METHOD(nfc_unlock_helper_setup_from_state, void, (NfcApp * instance)),
    API_METHOD(nfc_unlock_helper_card_detected_handler, void, (NfcApp * instance)),
    API_METHOD(
        felica_get_ic_name,
        void,
        (const FelicaData* data, FuriString* ic_name)),
    API_METHOD(
        felica_write_directory_tree,
        void,
        (const FelicaSystem* system, FuriString* str)),
    API_METHOD(
        iso14443_3a_is_equal,
        bool,
        (const Iso14443_3aData* data, const Iso14443_3aData* other)),
    API_METHOD(
        iso14443_3a_supports_iso14443_4,
        bool,
        (const Iso14443_3aData* data)),
    API_METHOD(
        iso14443_3b_get_application_data,
        const uint8_t*,
        (const Iso14443_3bData* data, size_t* data_size)),
    API_METHOD(
        iso14443_3b_get_frame_size_max,
        uint16_t,
        (const Iso14443_3bData* data)),
    API_METHOD(
        iso14443_3b_get_fwt_fc_max,
        uint32_t,
        (const Iso14443_3bData* data)),
    API_METHOD(
        iso14443_3b_get_uid,
        const uint8_t*,
        (const Iso14443_3bData* data, size_t* uid_len)),
    API_METHOD(
        iso14443_3b_supports_bit_rate,
        bool,
        (const Iso14443_3bData* data, Iso14443_3bBitRate bit_rate)),
    API_METHOD(
        iso14443_3b_supports_frame_option,
        bool,
        (const Iso14443_3bData* data, Iso14443_3bFrameOption option)),
    API_METHOD(
        iso14443_3b_supports_iso14443_4,
        bool,
        (const Iso14443_3bData* data)),
    API_METHOD(
        iso14443_4a_get_base_data,
        Iso14443_3aData*,
        (const Iso14443_4aData* data)),
    API_METHOD(
        iso14443_4a_get_frame_size_max,
        uint16_t,
        (const Iso14443_4aData* data)),
    API_METHOD(
        iso14443_4a_get_fwt_fc_max,
        uint32_t,
        (const Iso14443_4aData* data)),
    API_METHOD(
        iso14443_4a_get_historical_bytes,
        const uint8_t*,
        (const Iso14443_4aData* data, uint32_t* count)),
    API_METHOD(
        iso14443_4a_supports_bit_rate,
        bool,
        (const Iso14443_4aData* data, Iso14443_4aBitRate bit_rate)),
    API_METHOD(
        iso14443_4a_supports_frame_option,
        bool,
        (const Iso14443_4aData* data, Iso14443_4aFrameOption option)),
    API_METHOD(
        iso14443_4b_get_base_data,
        Iso14443_3bData*,
        (const Iso14443_4bData* data)),
    API_METHOD(
        iso15693_3_get_block_count,
        uint16_t,
        (const Iso15693_3Data* data)),
    API_METHOD(
        iso15693_3_get_block_data,
        const uint8_t*,
        (const Iso15693_3Data* data, uint8_t block_index)),
    API_METHOD(
        iso15693_3_get_block_size,
        uint8_t,
        (const Iso15693_3Data* data)),
    API_METHOD(
        iso15693_3_get_uid,
        const uint8_t*,
        (const Iso15693_3Data* data, size_t* uid_len)),
    API_METHOD(
        iso15693_3_is_block_locked,
        bool,
        (const Iso15693_3Data* data, uint8_t block_index)),
    API_METHOD(
        mf_classic_alloc,
        MfClassicData*,
        ()),
    API_METHOD(
        mf_classic_block_to_value,
        bool,
        (const MfClassicBlock* block, int32_t* value, uint8_t* addr)),
    API_METHOD(
        mf_classic_free,
        void,
        (MfClassicData* data)),
    API_METHOD(
        mf_classic_get_device_name,
        const char*,
        (const MfClassicData* data, NfcDeviceNameType name_type)),
    API_METHOD(
        mf_classic_get_first_block_num_of_sector,
        uint8_t,
        (uint8_t sector)),
    API_METHOD(
        mf_classic_get_read_sectors_and_keys,
        void,
        (const MfClassicData* data, uint8_t* sectors_read, uint8_t* keys_found)),
    API_METHOD(
        mf_classic_get_sector_by_block,
        uint8_t,
        (uint8_t block)),
    API_METHOD(
        mf_classic_get_sector_trailer_by_sector,
        MfClassicSectorTrailer*,
        (const MfClassicData* data, uint8_t sector_num)),
    API_METHOD(
        mf_classic_get_sector_trailer_num_by_block,
        uint8_t,
        (uint8_t block)),
    API_METHOD(
        mf_classic_get_sector_trailer_num_by_sector,
        uint8_t,
        (uint8_t sector)),
    API_METHOD(
        mf_classic_get_total_block_num,
        uint16_t,
        (MfClassicType type)),
    API_METHOD(
        mf_classic_get_total_sectors_num,
        uint8_t,
        (MfClassicType type)),
    API_METHOD(
        mf_classic_get_uid,
        const uint8_t*,
        (const MfClassicData* data, size_t* uid_len)),
    API_METHOD(
        mf_classic_is_block_read,
        bool,
        (const MfClassicData* data, uint8_t block_num)),
    API_METHOD(
        mf_classic_is_card_read,
        bool,
        (const MfClassicData* data)),
    API_METHOD(
        mf_classic_is_key_found,
        bool,
        (const MfClassicData* data, uint8_t sector_num, MfClassicKeyType key_type)),
    API_METHOD(
        mf_classic_is_sector_trailer,
        bool,
        (uint8_t block)),
    API_METHOD(
        mf_classic_poller_sync_auth,
        MfClassicError,
        (Nfc* nfc, uint8_t block_num, MfClassicKey* key, MfClassicKeyType key_type, MfClassicAuthContext* data)),
    API_METHOD(
        mf_classic_poller_sync_detect_type,
        MfClassicError,
        (Nfc* nfc, MfClassicType* type)),
    API_METHOD(
        mf_classic_poller_sync_read,
        MfClassicError,
        (Nfc* nfc, const MfClassicDeviceKeys* keys, MfClassicData* data)),
    API_METHOD(
        mf_classic_poller_sync_read_block,
        MfClassicError,
        (Nfc* nfc, uint8_t block_num, MfClassicKey* key, MfClassicKeyType key_type, MfClassicBlock* data)),
    API_METHOD(
        mf_desfire_get_application,
        const MfDesfireApplication*,
        (const MfDesfireData* data, const MfDesfireApplicationId* app_id)),
    API_METHOD(
        mf_desfire_get_base_data,
        Iso14443_4aData*,
        (const MfDesfireData* data)),
    API_METHOD(
        mf_desfire_get_file_data,
        const MfDesfireFileData*,
        (const MfDesfireApplication* data, const MfDesfireFileId* file_id)),
    API_METHOD(
        mf_desfire_get_file_settings,
        const MfDesfireFileSettings*,
        (const MfDesfireApplication* data, const MfDesfireFileId* file_id)),
    API_METHOD(
        mf_plus_get_base_data,
        Iso14443_4aData*,
        (const MfPlusData* data)),
    API_METHOD(
        mf_plus_get_device_name,
        const char*,
        (const MfPlusData* data, NfcDeviceNameType name_type)),
    API_METHOD(
        mf_ultralight_get_config_page,
        bool,
        (const MfUltralightData* data, MfUltralightConfigPages** config)),
    API_METHOD(
        mf_ultralight_get_pages_total,
        uint16_t,
        (MfUltralightType type)),
    API_METHOD(
        mf_ultralight_is_all_data_read,
        bool,
        (const MfUltralightData* data)),
    API_METHOD(
        nfc_device_copy_data,
        void,
        (const NfcDevice* instance, NfcProtocol protocol, NfcDeviceData* protocol_data)),
    API_METHOD(
        nfc_device_get_data,
        const NfcDeviceData*,
        (const NfcDevice* instance, NfcProtocol protocol)),
    API_METHOD(
        nfc_device_get_name,
        const char*,
        (const NfcDevice* instance, NfcDeviceNameType name_type)),
    API_METHOD(
        nfc_device_get_protocol,
        NfcProtocol,
        (const NfcDevice* instance)),
    API_METHOD(
        nfc_device_get_uid,
        const uint8_t*,
        (const NfcDevice* instance, size_t* uid_len)),
    API_METHOD(
        nfc_device_set_data,
        void,
        (NfcDevice* instance, NfcProtocol protocol, const NfcDeviceData* protocol_data)),
    API_METHOD(
        nfc_emv_parser_get_country_name,
        bool,
        (Storage* storage, uint16_t country_code, FuriString* country_name)),
    API_METHOD(
        nfc_emv_parser_get_currency_name,
        bool,
        (Storage* storage, uint16_t currency_code, FuriString* currency_name)),
    API_METHOD(
        nfc_listener_alloc,
        NfcListener*,
        (Nfc* nfc, NfcProtocol protocol, const NfcDeviceData* data)),
    API_METHOD(
        nfc_listener_start,
        void,
        (NfcListener* instance, NfcGenericCallback callback, void* context)),
    API_METHOD(
        nfc_poller_alloc,
        NfcPoller*,
        (Nfc* nfc, NfcProtocol protocol)),
    API_METHOD(
        nfc_poller_get_data,
        const NfcDeviceData*,
        (const NfcPoller* instance)),
    API_METHOD(
        nfc_poller_start,
        void,
        (NfcPoller* instance, NfcGenericCallback callback, void* context)),
    API_METHOD(
        nfc_render_emv_name,
        void,
        (const char* data, FuriString* str)),
    API_METHOD(
        ntag4xx_get_base_data,
        Iso14443_4aData*,
        (const Ntag4xxData* data)),
    API_METHOD(
        ntag4xx_get_type_from_version,
        Ntag4xxType,
        (const Ntag4xxVersion* const version)),
    API_METHOD(
        slix_get_base_data,
        const Iso15693_3Data*,
        (const SlixData* data)),
    API_METHOD(
        slix_get_type,
        SlixType,
        (const SlixData* data)),
    API_METHOD(
        slix_type_has_features,
        bool,
        (SlixType slix_type, SlixTypeFeatures features)),
    API_METHOD(
        slix_type_supports_password,
        bool,
        (SlixType slix_type, SlixPasswordType password_type)),
    API_METHOD(
        st25tb_get_block_count,
        uint8_t,
        (St25tbType type)),
    API_METHOD(
        type_4_tag_get_base_data,
        Iso14443_4aData*,
        (const Type4TagData* data))));
