#pragma once

#include <gui/view.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/subghz_setting.h>

typedef struct ModAnalyzerView ModAnalyzerView;

typedef enum {
    ModAnalyzerViewEventOpenReceiver,
} ModAnalyzerViewEvent;

typedef void (*ModAnalyzerViewCallback)(ModAnalyzerViewEvent event, void* context);

ModAnalyzerView* mod_analyzer_view_alloc(const SubGhzDevice* device, SubGhzSetting* setting);
void mod_analyzer_view_free(ModAnalyzerView* instance);
View* mod_analyzer_view_get_view(ModAnalyzerView* instance);
void mod_analyzer_view_set_callback(
    ModAnalyzerView* instance,
    ModAnalyzerViewCallback callback,
    void* context);
uint32_t mod_analyzer_view_get_frequency(ModAnalyzerView* instance);
void mod_analyzer_view_set_frequency(ModAnalyzerView* instance, uint32_t frequency);
