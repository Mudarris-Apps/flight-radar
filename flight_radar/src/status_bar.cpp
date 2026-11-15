// status_bar.cpp: one centred 12 px label near the top of the scope.
#include "status_bar.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include "config.h"
#include "net_task.h"

// U+00B7 is not in the built-in Montserrat glyph set; U+2022 (LV_SYMBOL_BULLET) is.
#define STATUS_SEP " " LV_SYMBOL_BULLET " "

lv_obj_t *statusBarCreate(lv_obj_t *parent) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_remove_style_all(l);
  lv_coord_t cx = lv_disp_get_hor_res(NULL) / 2;
  lv_obj_set_pos(l, cx - 100, 28);
  lv_obj_set_width(l, 200);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0x3E7A55), 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  lv_label_set_text(l, "");
  statusBarUpdate(l);
  return l;
}

void statusBarUpdate(lv_obj_t *bar) {
  if (!bar) return;
  NetStatus s = netStatusGet();
  char buf[64];
  switch (s.state) {
    case NetState::OK: {
      // Time since the board last received data (not OpenSky's own, older timestamp),
      // so it resets to 0 on every successful poll.
      uint32_t ago = (millis() - s.last_poll_millis) / 1000;
      snprintf(buf, sizeof(buf), "%u aircraft" STATUS_SEP "updated %us ago", (unsigned)s.aircraft_count, (unsigned)ago);
      break;
    }
    case NetState::WIFI_CONNECTING: {
      // Joined, but the first flights have not arrived yet (locating, logging in, first poll).
      char ssid[33];
      netWifiConnectedSsid(ssid, sizeof(ssid));
      snprintf(buf, sizeof(buf), ssid[0] ? "LOADING FLIGHTS..." : "WIFI CONNECTING");
      break;
    }
    case NetState::WIFI_DOWN:
      if (netWifiNeedsSetup()) snprintf(buf, sizeof(buf), "NO WIFI" STATUS_SEP "HOLD KNOB");
      else snprintf(buf, sizeof(buf), "WIFI DOWN");
      break;
    case NetState::AUTH_FAILED:     snprintf(buf, sizeof(buf), "OPENSKY AUTH FAILED"); break;
    case NetState::RATE_LIMITED: {
      uint32_t mins = s.next_poll_in_s > 0 ? (s.next_poll_in_s + 59) / 60 : POLL_BACKOFF_S / 60;
      snprintf(buf, sizeof(buf), "OPENSKY HTTP 429" STATUS_SEP "retry %u min", (unsigned)mins);
      break;
    }
    case NetState::HTTP_ERROR:
      if (s.http_code) snprintf(buf, sizeof(buf), "OPENSKY HTTP %u", (unsigned)s.http_code);
      else snprintf(buf, sizeof(buf), "OPENSKY ERROR");
      break;
    default: snprintf(buf, sizeof(buf), "OPENSKY ERROR"); break;
  }
  const char *cur = lv_label_get_text(bar);
  if (!cur || strcmp(cur, buf) != 0) lv_label_set_text(bar, buf);
}
