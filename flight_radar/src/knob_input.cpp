#include "knob_input.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <ESP_Knob.h>
#include <Button.h>

namespace {

QueueHandle_t g_queue = nullptr;
ESP_Knob *g_knob = nullptr;
Button *g_button = nullptr;

void push(InputEvent e) {
  if (g_queue) xQueueSend(g_queue, &e, 0);
}

// ESP32_Knob callbacks run in the library's own task context; the count is
// ignored and one event is emitted per callback invocation.
void onKnobLeft(int /*count*/, void * /*usr_data*/) { push(InputEvent::ROTATE_LEFT); }
void onKnobRight(int /*count*/, void * /*usr_data*/) { push(InputEvent::ROTATE_RIGHT); }

// ESP32_Button callbacks also run in the library's task context.
void onButtonSingleClick(void * /*button_handle*/, void * /*usr_data*/) { push(InputEvent::PRESS); }
void onButtonLongPressStart(void * /*button_handle*/, void * /*usr_data*/) { push(InputEvent::LONG_PRESS); }

}  // namespace

void knobInputStart() {
  g_queue = xQueueCreate(32, sizeof(InputEvent));

  g_knob = new ESP_Knob(6, 5);
  g_knob->invertDirection();   // hardware: clockwise must emit ROTATE_RIGHT (zoom in)
  g_knob->begin();
  g_knob->attachLeftEventCallback(onKnobLeft);
  g_knob->attachRightEventCallback(onKnobRight);

  // GPIO0 is the BOOT strap pin: it must stay an input with a pull-up and
  // never be driven as an output. The two-argument GPIO constructor is the
  // plain-GPIO form (as opposed to the ADC-button constructor); it sets the
  // pull mode itself and treats the pin purely as an input.
  g_button = new Button(GPIO_NUM_0, true);
  g_button->attachSingleClickEventCb(onButtonSingleClick, nullptr);
  g_button->attachLongPressStartEventCb(onButtonLongPressStart, nullptr);
}

bool knobInputPop(InputEvent &out) {
  if (!g_queue) return false;
  return xQueueReceive(g_queue, &out, 0) == pdTRUE;
}
