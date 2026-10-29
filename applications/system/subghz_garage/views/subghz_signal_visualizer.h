#pragma once

#include <gui/view.h>
#include "../helpers/subghz_custom_event.h"
#include "../helpers/subghz_txrx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzSignalVisualizer SubGhzSignalVisualizer;

typedef void (*SubGhzSignalVisualizerCallback)(SubGhzCustomEvent event, void* context);

void subghz_signal_visualizer_set_callback(
    SubGhzSignalVisualizer* instance,
    SubGhzSignalVisualizerCallback callback,
    void* context);

SubGhzSignalVisualizer* subghz_signal_visualizer_alloc(SubGhzTxRx* txrx);

void subghz_signal_visualizer_free(SubGhzSignalVisualizer* instance);

View* subghz_signal_visualizer_get_view(SubGhzSignalVisualizer* instance);

void subghz_signal_visualizer_start(SubGhzSignalVisualizer* instance);

void subghz_signal_visualizer_stop(SubGhzSignalVisualizer* instance);

uint32_t subghz_signal_visualizer_get_mode(SubGhzSignalVisualizer* instance);

void subghz_signal_visualizer_set_mode(SubGhzSignalVisualizer* instance, uint32_t mode);

#ifdef __cplusplus
}
#endif
