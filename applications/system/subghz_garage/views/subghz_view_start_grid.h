#pragma once
#include <gui/view.h>

#define SGRID_IDX_READ          0
#define SGRID_IDX_SAVED         1
#define SGRID_IDX_READRAW       2
#define SGRID_IDX_FREQANA       3
#define SGRID_IDX_MODANA        4
#define SGRID_IDX_PROTOCOLS     5
#define SGRID_BTN_COUNT         6

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzStartGrid SubGhzStartGrid;

typedef void (*SubGhzStartGridCallback)(void* context, uint32_t event);

SubGhzStartGrid* subghz_start_grid_alloc(void);
void subghz_start_grid_free(SubGhzStartGrid* instance);
View* subghz_start_grid_get_view(SubGhzStartGrid* instance);
void subghz_start_grid_set_callback(SubGhzStartGrid* instance,
                                     SubGhzStartGridCallback callback,
                                     void* context);
void subghz_start_grid_set_visible(SubGhzStartGrid* instance,
                                    uint8_t btn_idx,
                                    bool visible);
void subghz_start_grid_set_selected(SubGhzStartGrid* instance,
                                     uint8_t btn_idx);

#ifdef __cplusplus
}
#endif
