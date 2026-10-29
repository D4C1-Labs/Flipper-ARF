#pragma once

#include <lib/flipper_application/flipper_application.h>
#include <lib/subghz/types.h>
#include "protocol_items.h"

#define SUBGHZ_GARAGE_PROTOCOL_PLUGIN_APP_ID      "subghz_garage_protocol_plugin"
#define SUBGHZ_GARAGE_PROTOCOL_PLUGIN_API_VERSION 1U

typedef struct {
    const SubGhzProtocolRegistry* registry;

    void (*faac_slh_reset_prog_mode)(void);
} SubGhzGarageProtocolPlugin;
