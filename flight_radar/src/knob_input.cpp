#include "knob_input.h"
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <ESP_Knob.h>
#include <Button.h>
#include "config.h"

namespace {

QueueHandle_t g_queue = nullptr;
ESP_Knob *g_knob = nullptr;
Button *g_button = nullptr;

uint32_t g_start_ms = 0;

void push(InputEvent e) {
  if (g_queue) xQueueSend(g_queue, &e, 0);
}

// Button events in the first BUTTON_BOOT_IGNORE_MS are dropped: the GPIO0 strap held low
// by esptool's reset reads as a press and would fire a phantom LONG_PRESS at boot.
void pushButton(InputEvent e) {
  uint32_t age = millis() - g_start_ms;
  if (age < BUTTON_BOOT_IGNORE_MS) {
    Serial.printf("[knob] boot: dropped %s at %u ms\n", e == InputEvent::PRESS ? "PRESS" : "LONG_PRESS",
                  (unsigned)age);
    return;
  }
  Serial.printf("[knob] %s at %u ms after start\n", e == InputEvent::PRESS ? "PRESS" : "LONG_PRESS", (unsigned)age);
  push(e);
}

// ESP32_Knob callbacks run in the library's own task context; the count is
// ignored and one event is emitted per callback invocation.
void onKnobLeft(int /*count*/, void * /*usr_data*/) { push(InputEvent::ROTATE_LEFT); }
void onKnobRight(int /*count*/, void * /*usr_data*/) { push(InputEvent::ROTATE_RIGHT); }

// ESP32_Button callbacks also run in the library's task context.
void onButtonSingleClick(void * /*button_handle*/, void * /*usr_data*/) { pushButton(InputEvent::PRESS); }
void onButtonLongPressStart(void * /*button_handle*/, void * /*usr_data*/) { pushButton(InputEvent::LONG_PRESS); }

}  // namespace

void knobInputStart() {
  g_start_ms = millis();
  g_queue = xQueueCreate(32, sizeof(InputEvent));

  g_knob = new ESP_Knob(6, 5);
  g_knob->invertDirection();   // hardware: clockwise must emit ROTATE_RIGHT (zoom in)
  g_knob->begin();
  g_knob->attachLeftEventCallback(onKnobLeft);
  g_knob->attachRightEventCallback(onKnobRight);

  // GPIO0 is the BOOT strap pin: it must stay an input and never be driven as an
  // output. The knob button pulls it to GND, so it is active LOW. The library's
  // second argument is misnamed: it is the active level, and `false` selects active
  // low with the internal pull-up (`true` meant active high with a pull-down, so
  // the idle pin read as held and fired a LONG_PRESS 1.5 s after every boot).
  g_button = new Button(GPIO_NUM_0, false);
  g_button->attachSingleClickEventCb(onButtonSingleClick, nullptr);
  g_button->attachLongPressStartEventCb(onButtonLongPressStart, nullptr);
  Serial.printf("[knob] start at %u ms, gpio0=%d\n", (unsigned)g_start_ms, digitalRead(0));
}

bool knobInputPop(InputEvent &out) {
  if (!g_queue) return false;
  return xQueueReceive(g_queue, &out, 0) == pdTRUE;
}
