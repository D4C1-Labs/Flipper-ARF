#pragma once

#include <lib/flipper_application/flipper_application.h>
#include <lib/subghz/types.h>
#include "protocol_items.h"

#define GDR_PROTOCOL_PLUGIN_APP_ID      "gdr_protocol_plugins"
#define GDR_PROTOCOL_PLUGIN_API_VERSION 1U

typedef struct {
    const char* plugin_name;
    GDRProtocolRegistryFilter filter;
    const SubGhzProtocolRegistry* registry;
} GDRProtocolPlugin;
