#pragma once
#include "zoom_controller.h"
void knobInputStart();                 // creates queue, ESP_Knob(6,5), Button(GPIO0)
bool knobInputPop(InputEvent &out);    // non-blocking
