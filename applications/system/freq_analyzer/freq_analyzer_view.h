#pragma once

#include <gui/view.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/subghz_setting.h>

typedef enum {
    FreqAnalyzerFeedbackLevelAll,
    FreqAnalyzerFeedbackLevelVibro,
    FreqAnalyzerFeedbackLevelMute,
} FreqAnalyzerFeedbackLevel;

typedef enum {
    FreqAnalyzerViewEventLock,
    FreqAnalyzerViewEventUnlock,
    FreqAnalyzerViewEventOpenReceiver,
} FreqAnalyzerViewEvent;

typedef struct FreqAnalyzerView FreqAnalyzerView;

typedef void (*FreqAnalyzerViewCallback)(FreqAnalyzerViewEvent event, void* context);

FreqAnalyzerView* freq_analyzer_view_alloc(
    const SubGhzDevice* device,
    SubGhzSetting* setting,
    float trigger_level,
    FreqAnalyzerFeedbackLevel feedback_level,
    bool ext_radio);

void freq_analyzer_view_free(FreqAnalyzerView* instance);

View* freq_analyzer_view_get_view(FreqAnalyzerView* instance);

void freq_analyzer_view_set_callback(
    FreqAnalyzerView* instance,
    FreqAnalyzerViewCallback callback,
    void* context);

FreqAnalyzerFeedbackLevel freq_analyzer_view_get_feedback_level(FreqAnalyzerView* instance);
float freq_analyzer_view_get_trigger_level(FreqAnalyzerView* instance);

uint32_t freq_analyzer_view_get_handoff_frequency(FreqAnalyzerView* instance);
