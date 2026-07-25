#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <lib/ble_central/ble_central.h>
#include <string.h>

#define TAG "BleScanner"
#define BLE_SCANNER_MAX_DEVICES 32
#define BLE_SCANNER_NAME_MAX_LEN 32

typedef struct {
    uint32_t prefix;
    const char* vendor;
} OuiEntry;

static const OuiEntry oui_table[] = {
    {0x70B5E8, "ZTE"},
    {0x8CD3A8, "Xiaomi"},
    {0x9CE338, "Xiaomi"},
    {0x48E7DA, "Xiaomi"},
    {0x04CF8C, "Xiaomi"},
    {0x6802B8, "Xiaomi"},
    {0xA4C138, "Xiaomi"},
    {0xF8A45F, "Xiaomi"},
    {0x30C6F7, "Xiaomi"},
    {0xCCE7DF, "Xiaomi"},
    {0xACA220, "Xiaomi"},
    {0x18A6F7, "Samsung"},
    {0x2C54CF, "Samsung"},
    {0x5CF9DD, "Samsung"},
    {0x8C8EF2, "Samsung"},
    {0x9C2A70, "Samsung"},
    {0xA40CC3, "Samsung"},
    {0xB8AD0E, "Samsung"},
    {0xCC3A61, "Samsung"},
    {0xE0B9BA, "Samsung"},
    {0xEC1FA6, "Samsung"},
    {0xF0B0E7, "Samsung"},
    {0x58500E, "Samsung"},
    {0x34C34C, "Samsung"},
    {0x3C7DB1, "Samsung"},
    {0xA88792, "Samsung"},
    {0xDC0B6C, "Samsung"},
    {0xE87DBD, "Samsung"},
    {0xAC5F3E, "Samsung"},
    {0x38C7BA, "Samsung"},
    {0x001122, "Apple"},
    {0x0025BC, "Apple"},
    {0x003065, "Apple"},
    {0x003F2E, "Apple"},
    {0x003F35, "Apple"},
    {0x00601C, "Apple"},
    {0x00719B, "Apple"},
    {0x00A040, "Apple"},
    {0x00D8E1, "Apple"},
    {0x04B133, "Apple"},
    {0x04E536, "Apple"},
    {0x08EBED, "Apple"},
    {0x0C3076, "Apple"},
    {0x0C9361, "Apple"},
    {0x10A932, "Apple"},
    {0x140D4F, "Apple"},
    {0x181F32, "Apple"},
    {0x1C36F3, "Apple"},
    {0x1C9272, "Apple"},
    {0x203565, "Apple"},
    {0x28CFE9, "Apple"},
    {0x2C200B, "Apple"},
    {0x2CF0A2, "Apple"},
    {0x30D366, "Apple"},
    {0x349A0D, "Apple"},
    {0x3820D1, "Apple"},
    {0x3C0754, "Apple"},
    {0x3CD0F8, "Apple"},
    {0x401D58, "Apple"},
    {0x44239C, "Apple"},
    {0x4843CD, "Apple"},
    {0x4C6B39, "Apple"},
    {0x54132F, "Apple"},
    {0x58676A, "Apple"},
    {0x5C34EF, "Apple"},
    {0x603B6E, "Apple"},
    {0x64A31B, "Apple"},
    {0x68AE20, "Apple"},
    {0x6C3BA1, "Apple"},
    {0x6C720E, "Apple"},
    {0x70D88E, "Apple"},
    {0x78A351, "Apple"},
    {0x7C11BE, "Apple"},
    {0x800017, "Apple"},
    {0x84968C, "Apple"},
    {0x88D51C, "Apple"},
    {0x8C8590, "Apple"},
    {0x8CDE52, "Apple"},
    {0x909FB9, "Apple"},
    {0x98FE94, "Apple"},
    {0xA095B0, "Apple"},
    {0xA4D1D2, "Apple"},
    {0xA8B84F, "Apple"},
    {0xB0487A, "Apple"},
    {0xB0B2DC, "Apple"},
    {0xB45987, "Apple"},
    {0xB89675, "Apple"},
    {0xBC1665, "Apple"},
    {0xC0B593, "Apple"},
    {0xC44B87, "Apple"},
    {0xC81A9E, "Apple"},
    {0xC8B5AD, "Apple"},
    {0xCC25EF, "Apple"},
    {0xD039B3, "Apple"},
    {0xD42C3A, "Apple"},
    {0xD44F82, "Apple"},
    {0xD8031F, "Apple"},
    {0xE062E6, "Apple"},
    {0xE0F5C6, "Apple"},
    {0xE8A7A7, "Apple"},
    {0xF0B0E8, "Apple"},
    {0xF0D1B9, "Apple"},
    {0xF47F35, "Apple"},
    {0xF8313E, "Apple"},
    {0xFC145E, "Apple"},
    {0xFC9F5E, "Apple"},
    {0xA03860, "Google"},
    {0x94885E, "Google"},
    {0x7483C2, "Google"},
    {0x643F5F, "Google"},
    {0x286AB8, "Google"},
    {0x1868CB, "Google"},
    {0x0CCD9F, "Google"},
    {0x60A4B7, "Google"},
    {0x14A764, "Google"},
    {0x8871E5, "Google"},
    {0x486C8C, "Google"},
    {0x24AB81, "Google"},
    {0x3C28A6, "Google"},
    {0xD0E178, "Google"},
    {0x2C5BE7, "Google"},
    {0x28B0CC, "Google"},
    {0x94A7B7, "Google"},
    {0xA44E2F, "Google"},
    {0x5C8FE6, "Google"},
    {0x3898D8, "Google"},
    {0x9CADEF, "Google"},
    {0x5859D5, "Google"},
    {0x30C7AE, "Google"},
    {0x18DED7, "Google"},
    {0x24A642, "Google"},
    {0x70781E, "Google"},
    {0x5C639C, "Google"},
    {0xD089E2, "Google"},
    {0x54E1AD, "Google"},
    {0x00C538, "Huawei"},
    {0x34C9F0, "Huawei"},
    {0x0C5A9B, "Huawei"},
    {0xCCF1A0, "Huawei"},
    {0x3075B0, "Huawei"},
    {0x18E7F4, "Huawei"},
    {0xFCBDE8, "Huawei"},
    {0xF8CEBA, "Huawei"},
    {0x98039B, "Huawei"},
    {0x704834, "Huawei"},
    {0x50B8A2, "Huawei"},
    {0x44D9E7, "Huawei"},
    {0xD46A10, "Huawei"},
    {0x00EDB9, "Sony"},
    {0x18264E, "Sony"},
    {0x2053CA, "Sony"},
    {0x4C0F9E, "Sony"},
    {0x5C515E, "Sony"},
    {0x700BC7, "Sony"},
    {0x9019D9, "Sony"},
    {0xF44B2A, "Sony"},
    {0xFCA59C, "Sony"},
    {0x385E9B, "Sony"},
    {0x5C9AD8, "Sony"},
    {0x2CAB25, "Sony"},
    {0x3816D1, "Sony"},
    {0x5863F6, "Sony"},
    {0x5084C2, "Sony"},
    {0x68DB54, "Sony"},
    {0x001CDF, "OnePlus"},
    {0x00503D, "Intel"},
    {0x00237B, "Intel"},
    {0x0030D7, "Motorola"},
    {0x00D0A9, "LG"},
    {0x886B76, "LG"},
    {0x482C71, "LG"},
    {0xC8F733, "LG"},
    {0x38B12D, "LG"},
    {0x9815A4, "LG"},
    {0xECF236, "LG"},
    {0x605718, "LG"},
    {0x682C7B, "LG"},
    {0x4851B7, "Bose"},
    {0x042C97, "Bose"},
    {0x00A050, "Bose"},
    {0x74EF7E, "Bose"},
    {0xF872EA, "Bose"},
    {0x382565, "Bose"},
    {0x00E06C, "JBL/Harman"},
    {0x2CDD0C, "JBL/Harman"},
    {0x64A6E6, "JBL/Harman"},
    {0x8C6B97, "JBL/Harman"},
    {0xF81D93, "JBL/Harman"},
    {0x38F7D2, "JBL/Harman"},
    {0x504A5E, "JBL/Harman"},
    {0xE039D7, "JBL/Harman"},
    {0x34DF2A, "Sennheiser"},
    {0x1CE63B, "Sennheiser"},
    {0x001C4A, "Plantronics"},
    {0x5C4A9E, "Plantronics"},
    {0x6C8336, "Plantronics"},
    {0x801F02, "Plantronics"},
    {0x00D02D, "Nest/Google"},
    {0x18B430, "Nest/Google"},
    {0x64DBA0, "Nest/Google"},
    {0x7CC5A1, "Nest/Google"},
    {0xB839D4, "Nest/Google"},
    {0x00258A, "Roku"},
    {0x00A0D2, "Roku"},
    {0x005AA0, "Roku"},
    {0x38062C, "Roku"},
    {0x400B20, "Roku"},
    {0x9CEBE8, "Roku"},
    {0x0025E0, "Raspberry Pi"},
    {0xB827EB, "Raspberry Pi"},
    {0xDCA632, "Raspberry Pi"},
    {0xE45F01, "Raspberry Pi"},
    {0x287184, "Fitbit"},
    {0x48D638, "Fitbit"},
    {0x68372D, "Fitbit"},
    {0x883AA3, "Fitbit"},
    {0xAAF191, "Fitbit"},
    {0xD8DCB5, "Fitbit"},
    {0x00D8D7, "Garmin"},
    {0x182666, "Garmin"},
    {0x4C8AEE, "Garmin"},
    {0x5C3A6D, "Garmin"},
    {0x447E95, "Garmin"},
    {0x68572D, "Garmin"},
    {0x6C88D5, "Garmin"},
    {0x90CF33, "Garmin"},
    {0xB815F0, "Garmin"},
    {0xB8921D, "Garmin"},
    {0x0023AE, "Nokia"},
    {0x644BC7, "Nokia"},
    {0x14B17C, "Nokia"},
    {0x289124, "Nokia"},
    {0x482C6C, "Nokia"},
    {0x4C3463, "Nokia"},
    {0x802CA5, "Nokia"},
    {0x84261F, "Nokia"},
    {0x8C4D3E, "Nokia"},
    {0xBC6373, "Nokia"},
    {0xC8628B, "Nokia"},
    {0x08229B, "Oculus/Meta"},
    {0xCCA52A, "Oculus/Meta"},
    {0x0CD6BD, "Oculus/Meta"},
    {0x3871C5, "Oculus/Meta"},
    {0xFC5CEF, "Oculus/Meta"},
    {0x6CB06E, "Oculus/Meta"},
    {0xE020E0, "Oculus/Meta"},
    {0x3010A4, "Oculus/Meta"},
    {0x00140A, "Microsoft"},
    {0x00061B, "Microsoft"},
    {0x0025AE, "Microsoft"},
    {0x04A316, "Microsoft"},
    {0x181804, "Microsoft"},
    {0x1C6568, "Microsoft"},
    {0x287184, "Microsoft"},
    {0x44DF65, "Microsoft"},
    {0x48D6D5, "Microsoft"},
    {0x503EAA, "Microsoft"},
    {0x60C5A8, "Microsoft"},
    {0x64167F, "Microsoft"},
    {0x7C1E52, "Microsoft"},
    {0x8490AD, "Microsoft"},
    {0x986DC0, "Microsoft"},
    {0xA088B4, "Microsoft"},
    {0x38700C, "TP-Link"},
    {0x50C7BF, "TP-Link"},
    {0x64D98B, "TP-Link"},
    {0x84D46B, "TP-Link"},
    {0xC02506, "TP-Link"},
    {0xEC2280, "TP-Link"},
    {0x001848, "Espressif"},
    {0x18FE34, "Espressif"},
    {0x24B2DE, "Espressif"},
    {0x24E7C5, "Espressif"},
    {0x24818D, "Espressif"},
    {0x30AEA4, "Espressif"},
    {0x3C71BF, "Espressif"},
    {0x40F520, "Espressif"},
    {0x5CE3B6, "Espressif"},
    {0x68C63A, "Espressif"},
    {0x84CCA8, "Espressif"},
    {0x84F3EB, "Espressif"},
    {0x8C2DAA, "Espressif"},
    {0x8C7B9D, "Espressif"},
    {0xA4611B, "Espressif"},
    {0xAC67B2, "Espressif"},
    {0xB4E62D, "Espressif"},
    {0xC8F09E, "Espressif"},
    {0xCC50E3, "Espressif"},
    {0xDC4F22, "Espressif"},
    {0xE0B9A5, "Espressif"},
    {0xECFA5C, "Espressif"},
    {0xF4CFA2, "Espressif"},
};

static const char* ble_scanner_lookup_oui(const uint8_t* addr) {
    uint32_t prefix = ((uint32_t)addr[0] << 16) | ((uint32_t)addr[1] << 8) | addr[2];
    for(size_t i = 0; i < COUNT_OF(oui_table); i++) {
        if(oui_table[i].prefix == prefix) return oui_table[i].vendor;
    }
    return NULL;
}

typedef struct {
    char name[BLE_SCANNER_NAME_MAX_LEN];
    uint8_t address[6];
    int8_t rssi;
    uint8_t address_type;
} BleScannerDevice;

typedef enum {
    BleScannerView_Submenu,
    BleScannerView_TextBox,
} BleScannerView;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* text_box;

    BleScannerDevice devices[BLE_SCANNER_MAX_DEVICES];
    size_t device_count;

    FuriString* log_text;
} BleScannerApp;

static void ble_scanner_central_callback(BleCentralEventType event, void* device, void* context) {
    BleScannerApp* app = context;

    if(event == BleCentralEventDeviceFound) {
        BleCentralAdvertisedDevice* adv = device;
        if(app->device_count < BLE_SCANNER_MAX_DEVICES) {
            BleScannerDevice* dev = &app->devices[app->device_count];
            strncpy(dev->name, adv->name, BLE_SCANNER_NAME_MAX_LEN - 1);
            dev->name[BLE_SCANNER_NAME_MAX_LEN - 1] = '\0';
            memcpy(dev->address, adv->address, 6);
            dev->rssi = adv->rssi;
            dev->address_type = adv->address_type;
            app->device_count++;
        }
    }
}

static bool ble_scanner_back_event_callback(void* context) {
    UNUSED(context);
    return false;
}

static BleScannerApp* ble_scanner_app_alloc(void) {
    BleScannerApp* app = malloc(sizeof(BleScannerApp));
    memset(app, 0, sizeof(BleScannerApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, ble_scanner_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BleScannerView_Submenu, submenu_get_view(app->submenu));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BleScannerView_TextBox, text_box_get_view(app->text_box));

    app->log_text = furi_string_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void ble_scanner_app_free(BleScannerApp* app) {
    furi_string_free(app->log_text);
    view_dispatcher_remove_view(app->view_dispatcher, BleScannerView_Submenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, BleScannerView_TextBox);
    text_box_free(app->text_box);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t ble_scanner_app(void* p) {
    UNUSED(p);
    BleScannerApp* app = ble_scanner_app_alloc();

    submenu_set_header(app->submenu, "BLE Scanner");
    submenu_add_item(app->submenu, "Scan for devices", 0, NULL, NULL);
    view_dispatcher_switch_to_view(app->view_dispatcher, BleScannerView_Submenu);

    app->device_count = 0;

    bool ok = ble_central_scan_start(ble_scanner_central_callback, app);
    if(!ok) {
        furi_string_set_str(app->log_text, "ERROR: scan start failed!\n");
        text_box_set_text(app->text_box, furi_string_get_cstr(app->log_text));
        view_dispatcher_switch_to_view(app->view_dispatcher, BleScannerView_TextBox);
        view_dispatcher_run(app->view_dispatcher);
        ble_scanner_app_free(app);
        return 0;
    }

    furi_delay_ms(4000);
    ble_central_scan_stop();

    furi_string_reset(app->log_text);
    furi_string_cat_printf(app->log_text, "Found %zu devices:\n\n", app->device_count);
    for(size_t i = 0; i < app->device_count; i++) {
        BleScannerDevice* dev = &app->devices[i];
        const char* vendor = ble_scanner_lookup_oui(dev->address);
        if(dev->name[0]) {
            furi_string_cat_printf(app->log_text, "%zu. %s\n", i + 1, dev->name);
        } else if(vendor) {
            furi_string_cat_printf(app->log_text, "%zu. [%s device]\n", i + 1, vendor);
        } else {
            furi_string_cat_printf(app->log_text, "%zu. (no name)\n", i + 1);
        }
        furi_string_cat_printf(app->log_text, "   RSSI: %d\n\n", dev->rssi);
    }

    text_box_set_text(app->text_box, furi_string_get_cstr(app->log_text));
    view_dispatcher_switch_to_view(app->view_dispatcher, BleScannerView_TextBox);

    view_dispatcher_run(app->view_dispatcher);
    ble_scanner_app_free(app);
    return 0;
}
