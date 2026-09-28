#pragma once
#include <lvgl.h>
lv_obj_t *statusBarCreate(lv_obj_t *parent);
void      statusBarUpdate(lv_obj_t *bar);   // reads netStatusGet(), formats per spec
