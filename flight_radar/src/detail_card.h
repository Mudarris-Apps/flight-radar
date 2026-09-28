// detail_card.h: overlay card with the selected aircraft's details (LVGL task only).
#pragma once
#include <lvgl.h>
#include "ui_state.h"
lv_obj_t *detailCardCreate(lv_obj_t *parent, UiState *st);
void      detailCardRefresh(lv_obj_t *card);   // rebuilds text from the selected snap entry; hides when none selected or card_open false
