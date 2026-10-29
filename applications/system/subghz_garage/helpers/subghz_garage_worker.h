#pragma once

#include <furi_hal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzGarageWorker SubGhzGarageWorker;

typedef void (*SubGhzGarageWorkerOverrunCallback)(void* context);

typedef void (*SubGhzGarageWorkerPairCallback)(void* context, bool level, uint32_t duration);

void subghz_garage_worker_rx_callback(bool level, uint32_t duration, void* context);

SubGhzGarageWorker* subghz_garage_worker_alloc(void);

void subghz_garage_worker_free(SubGhzGarageWorker* instance);

void subghz_garage_worker_set_overrun_callback(
    SubGhzGarageWorker* instance,
    SubGhzGarageWorkerOverrunCallback callback);

void subghz_garage_worker_set_pair_callback(
    SubGhzGarageWorker* instance,
    SubGhzGarageWorkerPairCallback callback);

void subghz_garage_worker_set_context(SubGhzGarageWorker* instance, void* context);

void subghz_garage_worker_start(SubGhzGarageWorker* instance);

void subghz_garage_worker_stop(SubGhzGarageWorker* instance);

bool subghz_garage_worker_is_running(SubGhzGarageWorker* instance);

void subghz_garage_worker_set_filter(SubGhzGarageWorker* instance, uint16_t timeout);

#ifdef __cplusplus
}
#endif
