// wifi_screen.cpp: Wi-Fi setup screen, shown at every boot. Four pages share one full-screen panel:
//   OFFER "Connect to <saved network>" when one is in range, or "Other networks"
//   LIST  scanned networks, then "Other network", "Scan again", "Close"
//   KEYS  a text box and a 4-row keyboard (letters / CAPS / two symbol pages)
//   MSG   "Connecting...", "Connected", or why a join failed
// Knob: turn moves the highlight (row or key), press chooses, hold goes back.
// Touch: tap a row or a key. Layout keeps everything inside the 466 px circle.
#include "wifi_screen.h"
#include <Arduino.h>
#include <cstring>
#include "net_task.h"
#include "wifi_store.h"

namespace {

enum class Page : uint8_t { OFFER, LIST, KEYS, MSG };
enum class Target : uint8_t { SSID, PASS };
enum class Next : uint8_t { NONE, CLOSE, KEYS_PASS, LIST };
enum class RowKind : uint8_t { NET, OTHER, RESCAN, CLOSE };
enum Mode { LOWER, UPPER, SYM1, SYM2, N_MODES };

const int MAX_ROWS = WIFI_SCAN_MAX + 3;
const int MAX_KEYS = 48;
const uint32_t POLL_MS = 300;

const uint32_t COL_BG = 0x05080C, COL_PANEL = 0x0C1218, COL_KEY = 0x17222C, COL_TEXT = 0xE0E8F0,
               COL_DIM = 0x7F8C98, COL_ACCENT = 0x2F8F5A, COL_TITLE = 0x5FBF85;

// Keyboard maps. Single-character labels type themselves; longer labels are commands.
#define K_BKSP LV_SYMBOL_BACKSPACE
#define K_BACK LV_SYMBOL_CLOSE
#define K_OK   LV_SYMBOL_OK
const char *MAP_LOWER[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
                                 "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
                                 "ABC", "z", "x", "c", "v", "b", "n", "m", K_BKSP, "\n",
                                 "123", K_BACK, "space", K_OK, ""};
const char *MAP_UPPER[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
                                 "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
                                 "abc", "Z", "X", "C", "V", "B", "N", "M", K_BKSP, "\n",
                                 "123", K_BACK, "space", K_OK, ""};
const char *MAP_SYM1[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
                                "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "\n",
                                "#+=", "-", "_", "=", "+", ";", ":", K_BKSP, "\n",
                                "abc", K_BACK, "space", K_OK, ""};
const char *MAP_SYM2[] = {"[", "]", "{", "}", "<", ">", "/", "\\", "|", "~", "\n",
                                "'", "\"", "`", "?", ".", ",", "\n",
                                "123", "-", "_", "@", ":", K_BKSP, "\n",
                                "abc", K_BACK, "space", K_OK, ""};
const char **const MAPS[N_MODES] = {MAP_LOWER, MAP_UPPER, MAP_SYM1, MAP_SYM2};

struct Wifi {
  lv_obj_t *root = nullptr, *title = nullptr, *sub = nullptr, *hint = nullptr;
  lv_obj_t *list = nullptr, *ta = nullptr, *kb = nullptr, *msg = nullptr;
  lv_obj_t *offer = nullptr, *offer_label = nullptr, *offer_btn[2] = {};
  int offer_hl = 0;            // 0 = Connect, 1 = Other networks
  bool offer_pending = false;  // just opened: offer a saved network once the scan finds one
  char offer_ssid[33] = "";
  lv_obj_t *rows[MAX_ROWS] = {};
  RowKind kind[MAX_ROWS] = {};
  int row_net[MAX_ROWS] = {};
  int n_rows = 0, hl = 0;
  Page page = Page::LIST;
  Target target = Target::PASS;
  Mode mode = LOWER;
  lv_btnmatrix_ctrl_t ctrl[N_MODES][MAX_KEYS] = {};
  uint16_t n_keys[N_MODES] = {};
  WifiScanEntry scan[WIFI_SCAN_MAX];
  int scan_n = 0;
  uint32_t scan_gen = 0;
  bool scanning = false, shown_scanning = false;
  char ssid[33] = "";
  char connected[33] = "";
  bool ssid_typed = false;      // came from "Other network": back returns to the name
  bool tried_saved = false;     // join used a saved password; on failure ask for a new one
  bool auto_opened = false;
  bool prev_need = false;
  uint32_t last_poll_ms = 0;
  uint32_t msg_until = 0;
  Next msg_next = Next::NONE;
};
Wifi g;

void rowsBuild();
void showPage(Page p);

// ---------- helpers ----------

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

void buildCtrlMaps() {
  for (int m = 0; m < N_MODES; ++m) {
    uint16_t n = 0;
    for (const char *const *p = MAPS[m]; (*p)[0] != '\0' && n < MAX_KEYS; ++p) {
      const char *t = *p;
      if (strcmp(t, "\n") == 0) continue;
      uint8_t width = 2;
      if (strcmp(t, "space") == 0) width = 6;
      else if (strcmp(t, K_BACK) == 0) width = 2;
      else if (strlen(t) > 1) width = 3;   // ABC / abc / 123 / #+= / backspace / OK
      lv_btnmatrix_ctrl_t c = (lv_btnmatrix_ctrl_t)(width | LV_BTNMATRIX_CTRL_CLICK_TRIG);
      if (strcmp(t, K_BKSP) != 0) c |= LV_BTNMATRIX_CTRL_NO_REPEAT;   // only backspace repeats when held
      g.ctrl[m][n++] = c;
    }
    g.n_keys[m] = n;
  }
}

void setMode(Mode m) {
  uint16_t sel = lv_btnmatrix_get_selected_btn(g.kb);
  g.mode = m;
  lv_btnmatrix_set_map(g.kb, MAPS[m]);
  lv_btnmatrix_set_ctrl_map(g.kb, g.ctrl[m]);
  if (sel == LV_BTNMATRIX_BTN_NONE || sel >= g.n_keys[m]) sel = 0;
  lv_btnmatrix_set_selected_btn(g.kb, sel);   // the knob stays on the same spot
}

void showMsg(const char *text, uint32_t hold_ms, Next next) {
  lv_label_set_text(g.msg, text);
  g.msg_until = hold_ms ? millis() + hold_ms : 0;
  g.msg_next = next;
  showPage(Page::MSG);
}

void openKeys(Target t, const char *initial) {
  g.target = t;
  if (t == Target::SSID) {
    lv_label_set_text(g.title, "NETWORK NAME");
    lv_textarea_set_max_length(g.ta, wifistore::SSID_MAX);
    lv_textarea_set_placeholder_text(g.ta, "type the name");
  } else {
    lv_label_set_text_fmt(g.title, "PASSWORD FOR %s", g.ssid);
    lv_textarea_set_max_length(g.ta, wifistore::PASS_MAX);
    lv_textarea_set_placeholder_text(g.ta, "type the password");
  }
  lv_textarea_set_text(g.ta, initial ? initial : "");
  setMode(LOWER);
  lv_btnmatrix_set_selected_btn(g.kb, 0);
  showPage(Page::KEYS);
}

void startJoin(const char *pass) {
  netWifiRequestJoin(g.ssid, pass);
  char buf[80];
  snprintf(buf, sizeof(buf), "Connecting to\n%s ...", g.ssid);
  showMsg(buf, 0, Next::NONE);
}

void closeScreen() {
  lv_obj_add_flag(g.root, LV_OBJ_FLAG_HIDDEN);
  lv_textarea_set_text(g.ta, "");   // do not leave a typed password in memory longer than needed
  g.auto_opened = false;
  Serial.println("[wifi] screen closed");
}

// ---------- pages ----------

void showPage(Page p) {
  g.page = p;
  bool list = p == Page::LIST, keys = p == Page::KEYS, msg = p == Page::MSG, offer = p == Page::OFFER;
  auto vis = [](lv_obj_t *o, bool on) {
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  };
  vis(g.list, list);
  vis(g.offer, offer);
  vis(g.sub, list || offer);
  vis(g.ta, keys);
  vis(g.kb, keys);
  vis(g.msg, msg);
  vis(g.title, !msg);
  vis(g.hint, !msg);
  if (list) {
    lv_label_set_text(g.title, "WI-FI");
    lv_label_set_text(g.hint, "turn to choose  " LV_SYMBOL_BULLET "  press to pick  " LV_SYMBOL_BULLET "  hold to close");
    g.shown_scanning = !g.scanning;   // force the subtitle to refresh
    rowsBuild();
  } else if (keys) {
    lv_label_set_text(g.hint, "hold knob to go back");
  } else if (offer) {
    lv_label_set_text(g.title, "WI-FI");
    lv_label_set_text(g.hint, "turn to choose  " LV_SYMBOL_BULLET "  press to pick");
  }
}

void paintOffer() {
  for (int i = 0; i < 2; ++i) {
    bool on = i == g.offer_hl;
    lv_obj_set_style_bg_color(g.offer_btn[i], lv_color_hex(on ? COL_ACCENT : COL_KEY), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(g.offer_btn[i], 0), on ? lv_color_black() : lv_color_hex(COL_TEXT), 0);
  }
}

void showOffer(const char *ssid) {
  strncpy(g.offer_ssid, ssid, sizeof(g.offer_ssid) - 1);
  g.offer_ssid[sizeof(g.offer_ssid) - 1] = '\0';
  lv_label_set_text_fmt(g.offer_label, "%s\nis in range", g.offer_ssid);
  lv_label_set_text_fmt(lv_obj_get_child(g.offer_btn[0], 0), LV_SYMBOL_WIFI "  Connect to %s", g.offer_ssid);
  g.offer_hl = 0;
  g.offer_pending = false;
  paintOffer();
  showPage(Page::OFFER);
}

void joinSaved(const char *ssid);

void offerChoose(int i) {
  if (i == 0) joinSaved(g.offer_ssid);
  else showPage(Page::LIST);
}

void offerClickCb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  g.offer_hl = i;
  paintOffer();
  offerChoose(i);
}

void back() {
  if (g.page == Page::KEYS) {
    if (g.target == Target::PASS && g.ssid_typed) {
      openKeys(Target::SSID, g.ssid);
      return;
    }
    showPage(Page::LIST);
  } else if (g.page == Page::OFFER) {
    showPage(Page::LIST);
  } else if (g.page == Page::LIST) {
    closeScreen();
  }
}

void submit() {
  const char *text = lv_textarea_get_text(g.ta);
  if (g.target == Target::SSID) {
    if (!text[0]) return;
    strncpy(g.ssid, text, sizeof(g.ssid) - 1);
    g.ssid[sizeof(g.ssid) - 1] = '\0';
    g.ssid_typed = true;
    openKeys(Target::PASS, "");
    return;
  }
  size_t n = strlen(text);
  if (n > 0 && n < 8) {   // WPA/WPA2 passphrases are 8-63 characters; empty means an open network
    showMsg("Wi-Fi passwords are\nat least 8 characters", 2000, Next::KEYS_PASS);
    return;
  }
  g.tried_saved = false;
  char pass[wifistore::PASS_MAX + 1];
  strncpy(pass, text, sizeof(pass) - 1);
  pass[sizeof(pass) - 1] = '\0';
  startJoin(pass);
  memset(pass, 0, sizeof(pass));
}

void keyPressed(uint16_t id) {
  const char *t = lv_btnmatrix_get_btn_text(g.kb, id);
  if (!t) return;
  if (strcmp(t, "ABC") == 0) setMode(UPPER);
  else if (strcmp(t, "abc") == 0) setMode(LOWER);
  else if (strcmp(t, "123") == 0) setMode(SYM1);
  else if (strcmp(t, "#+=") == 0) setMode(SYM2);
  else if (strcmp(t, K_BKSP) == 0) lv_textarea_del_char(g.ta);
  else if (strcmp(t, K_BACK) == 0) back();
  else if (strcmp(t, K_OK) == 0) submit();
  else if (strcmp(t, "space") == 0) lv_textarea_add_char(g.ta, ' ');
  else lv_textarea_add_text(g.ta, t);
}

void kbEventCb(lv_event_t *e) {
  if (g.page != Page::KEYS) return;
  const uint32_t *id = static_cast<const uint32_t *>(lv_event_get_param(e));
  if (id) keyPressed((uint16_t)*id);
}

// Joins a known network with its stored password; on failure the keyboard asks for a new one.
void joinSaved(const char *ssid) {
  strncpy(g.ssid, ssid, sizeof(g.ssid) - 1);
  g.ssid[sizeof(g.ssid) - 1] = '\0';
  g.ssid_typed = false;
  char pass[wifistore::PASS_MAX + 1];
  if (wifistore::find(ssid, pass, sizeof(pass))) {
    g.tried_saved = true;
    startJoin(pass);
  } else {
    openKeys(Target::PASS, "");
  }
  memset(pass, 0, sizeof(pass));
}

void activateRow(int i) {
  if (i < 0 || i >= g.n_rows) return;
  switch (g.kind[i]) {
    case RowKind::NET: {
      const WifiScanEntry &e = g.scan[g.row_net[i]];
      strncpy(g.ssid, e.ssid, sizeof(g.ssid) - 1);
      g.ssid[sizeof(g.ssid) - 1] = '\0';
      g.ssid_typed = false;
      char pass[wifistore::PASS_MAX + 1];
      if (e.open) {
        g.tried_saved = false;
        startJoin("");
      } else if (e.known && wifistore::find(e.ssid, pass, sizeof(pass))) {
        g.tried_saved = true;   // try the saved password first; ask for a new one if it fails
        startJoin(pass);
      } else {
        openKeys(Target::PASS, "");
      }
      memset(pass, 0, sizeof(pass));
      break;
    }
    case RowKind::OTHER:
      g.ssid[0] = '\0';
      openKeys(Target::SSID, "");
      break;
    case RowKind::RESCAN:
      netWifiRequestScan();
      break;
    case RowKind::CLOSE:
      closeScreen();
      break;
  }
}

void paintRows() {
  for (int i = 0; i < g.n_rows; ++i) {
    bool on = i == g.hl;
    lv_obj_set_style_bg_color(g.rows[i], lv_color_hex(on ? COL_ACCENT : COL_KEY), 0);
    uint32_t tc = on ? 0x000000 : COL_TEXT;
    for (uint32_t c = 0; c < lv_obj_get_child_cnt(g.rows[i]); ++c)
      lv_obj_set_style_text_color(lv_obj_get_child(g.rows[i], c), lv_color_hex(tc), 0);
  }
  if (g.hl >= 0 && g.hl < g.n_rows) lv_obj_scroll_to_view(g.rows[g.hl], LV_ANIM_ON);
}

void rowClickCb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  g.hl = i;
  paintRows();
  activateRow(i);
}

void addRow(RowKind k, int net, const char *left, const char *right) {
  if (g.n_rows >= MAX_ROWS) return;
  int i = g.n_rows++;
  lv_obj_t *r = lv_obj_create(g.list);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), 42);
  lv_obj_set_style_radius(r, 10, 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(r, 14, 0);
  lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(r, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_event_cb(r, rowClickCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

  lv_obj_t *l = lv_label_create(r);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, right && right[0] ? lv_pct(78) : lv_pct(100));
  lv_label_set_text(l, left);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  if (right && right[0]) {
    lv_obj_t *rl = lv_label_create(r);
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_14, 0);
    lv_label_set_text(rl, right);
    lv_obj_align(rl, LV_ALIGN_RIGHT_MID, 0, 0);
  }
  g.rows[i] = r;
  g.kind[i] = k;
  g.row_net[i] = net;
}

void rowsBuild() {
  // Keep the highlight on the same network (or the same action row) across rebuilds.
  char keep_ssid[33] = "";
  RowKind keep_kind = RowKind::NET;
  if (g.hl >= 0 && g.hl < g.n_rows) {
    keep_kind = g.kind[g.hl];
    if (keep_kind == RowKind::NET) strncpy(keep_ssid, g.scan[g.row_net[g.hl]].ssid, sizeof(keep_ssid) - 1);
  }
  g.scan_n = netWifiScanResults(g.scan, WIFI_SCAN_MAX, &g.scan_gen, &g.scanning);
  netWifiConnectedSsid(g.connected, sizeof(g.connected));

  lv_obj_clean(g.list);
  g.n_rows = 0;
  for (int i = 0; i < g.scan_n; ++i) {
    const WifiScanEntry &e = g.scan[i];
    const char *right = strcmp(e.ssid, g.connected) == 0 ? LV_SYMBOL_OK : (e.open ? "open" : (e.known ? "saved" : ""));
    char left[48];
    snprintf(left, sizeof(left), LV_SYMBOL_WIFI "  %s", e.ssid);
    addRow(RowKind::NET, i, left, right);
  }
  addRow(RowKind::OTHER, -1, LV_SYMBOL_EDIT "  Other network", "");
  addRow(RowKind::RESCAN, -1, LV_SYMBOL_REFRESH "  Scan again", "");
  addRow(RowKind::CLOSE, -1, LV_SYMBOL_CLOSE "  Close", "");

  int hl = -1;
  for (int i = 0; i < g.n_rows && hl < 0; ++i) {
    if (keep_kind == RowKind::NET) {
      if (g.kind[i] == RowKind::NET && strcmp(g.scan[g.row_net[i]].ssid, keep_ssid) == 0) hl = i;
    } else if (g.kind[i] == keep_kind) {
      hl = i;
    }
  }
  g.hl = hl >= 0 ? hl : 0;
  paintRows();
}

void updateSubtitle() {
  char buf[112];
  if (g.scanning) snprintf(buf, sizeof(buf), "scanning ...");
  else if (g.connected[0]) {
    char place[40];
    netPlace(place, sizeof(place));
    snprintf(buf, sizeof(buf), "connected to %s  " LV_SYMBOL_BULLET "  %s", g.connected, place);
  }
  else snprintf(buf, sizeof(buf), "not connected");
  if (strcmp(lv_label_get_text(g.sub), buf) != 0) lv_label_set_text(g.sub, buf);
}

}  // namespace

lv_obj_t *wifiScreenCreate(lv_obj_t *parent) {
  buildCtrlMaps();
  lv_coord_t W = lv_disp_get_hor_res(NULL), H = lv_disp_get_ver_res(NULL);
  lv_coord_t cy = H / 2;

  lv_obj_t *root = lv_obj_create(parent);
  lv_obj_remove_style_all(root);
  lv_obj_set_size(root, W, H);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_style_bg_color(root, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);   // swallow touches meant for the radar underneath
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
  g.root = root;

  g.title = makeLabel(root, &lv_font_montserrat_18, COL_TITLE);
  lv_label_set_long_mode(g.title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g.title, 300);
  lv_obj_align(g.title, LV_ALIGN_TOP_MID, 0, cy - 160);

  g.sub = makeLabel(root, &lv_font_montserrat_12, COL_DIM);
  lv_label_set_long_mode(g.sub, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g.sub, 300);
  lv_obj_align(g.sub, LV_ALIGN_TOP_MID, 0, cy - 134);
  lv_label_set_text(g.sub, "");

  g.list = lv_obj_create(root);
  lv_obj_remove_style_all(g.list);
  lv_obj_set_size(g.list, 320, 222);
  lv_obj_align(g.list, LV_ALIGN_TOP_MID, 0, cy - 112);
  lv_obj_set_flex_flow(g.list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(g.list, 6, 0);
  lv_obj_set_scroll_dir(g.list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g.list, LV_SCROLLBAR_MODE_OFF);

  g.ta = lv_textarea_create(root);
  lv_textarea_set_one_line(g.ta, true);
  lv_obj_set_size(g.ta, 300, 46);
  lv_obj_align(g.ta, LV_ALIGN_TOP_MID, 0, cy - 132);
  lv_obj_set_style_text_font(g.ta, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(g.ta, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_bg_color(g.ta, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_bg_opa(g.ta, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(g.ta, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_border_width(g.ta, 1, 0);
  lv_obj_set_style_radius(g.ta, 10, 0);
  lv_obj_set_style_text_color(g.ta, lv_color_hex(COL_DIM), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_border_color(g.ta, lv_color_hex(COL_TITLE), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_add_state(g.ta, LV_STATE_FOCUSED);   // shows the cursor (no input group is used)
  lv_event_send(g.ta, LV_EVENT_FOCUSED, nullptr);

  g.kb = lv_btnmatrix_create(root);
  lv_obj_set_size(g.kb, 350, 210);
  lv_obj_align(g.kb, LV_ALIGN_TOP_MID, 0, cy - 76);
  lv_obj_set_style_bg_opa(g.kb, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g.kb, 0, 0);
  lv_obj_set_style_pad_all(g.kb, 2, 0);
  lv_obj_set_style_pad_row(g.kb, 5, 0);
  lv_obj_set_style_pad_column(g.kb, 4, 0);
  lv_obj_set_style_outline_width(g.kb, 0, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_bg_color(g.kb, lv_color_hex(COL_KEY), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(g.kb, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_text_color(g.kb, lv_color_hex(COL_TEXT), LV_PART_ITEMS);
  lv_obj_set_style_text_font(g.kb, &lv_font_montserrat_18, LV_PART_ITEMS);
  lv_obj_set_style_radius(g.kb, 7, LV_PART_ITEMS);
  lv_obj_set_style_border_width(g.kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(g.kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(g.kb, lv_color_hex(COL_ACCENT), LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_text_color(g.kb, lv_color_black(), LV_PART_ITEMS | LV_STATE_FOCUSED);
  lv_obj_set_style_bg_color(g.kb, lv_color_hex(COL_TITLE), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(g.kb, lv_color_black(), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_add_state(g.kb, LV_STATE_FOCUSED);   // draws the knob's selected key highlighted
  lv_obj_add_event_cb(g.kb, kbEventCb, LV_EVENT_VALUE_CHANGED, nullptr);
  setMode(LOWER);

  g.msg = makeLabel(root, &lv_font_montserrat_20, COL_TEXT);
  lv_obj_set_width(g.msg, 320);
  lv_label_set_long_mode(g.msg, LV_LABEL_LONG_WRAP);
  lv_obj_center(g.msg);

  g.offer = lv_obj_create(root);
  lv_obj_remove_style_all(g.offer);
  lv_obj_set_size(g.offer, 330, 250);
  lv_obj_align(g.offer, LV_ALIGN_TOP_MID, 0, cy - 112);
  lv_obj_clear_flag(g.offer, LV_OBJ_FLAG_SCROLLABLE);
  g.offer_label = makeLabel(g.offer, &lv_font_montserrat_24, COL_TEXT);
  lv_obj_set_width(g.offer_label, 320);
  lv_label_set_long_mode(g.offer_label, LV_LABEL_LONG_WRAP);
  lv_obj_align(g.offer_label, LV_ALIGN_TOP_MID, 0, 20);
  for (int i = 0; i < 2; ++i) {
    lv_obj_t *b = lv_obj_create(g.offer);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 300, 50);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, 116 + i * 62);
    lv_obj_set_style_radius(b, 25, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b, offerClickCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, 270);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, i == 0 ? "Connect" : "Other networks");
    lv_obj_center(l);
    g.offer_btn[i] = b;
  }

  g.hint = makeLabel(root, &lv_font_montserrat_12, COL_DIM);
  lv_obj_set_width(g.hint, 300);
  lv_obj_align(g.hint, LV_ALIGN_TOP_MID, 0, cy + 142);

  showPage(Page::LIST);
  return root;
}

void wifiScreenOpen(lv_obj_t *w) {
  if (!w) return;
  g.hl = 0;
  g.n_rows = 0;
  g.auto_opened = false;
  g.offer_pending = true;
  netWifiRequestScan();
  showPage(Page::LIST);
  lv_obj_move_foreground(w);
  lv_obj_clear_flag(w, LV_OBJ_FLAG_HIDDEN);
  Serial.println("[wifi] screen open");
}

bool wifiScreenIsOpen(lv_obj_t *w) { return w && !lv_obj_has_flag(w, LV_OBJ_FLAG_HIDDEN); }

void wifiScreenHandle(lv_obj_t *w, InputEvent e) {
  if (!wifiScreenIsOpen(w)) return;
  bool right = e == InputEvent::ROTATE_RIGHT, left = e == InputEvent::ROTATE_LEFT;
  switch (g.page) {
    case Page::OFFER:
      if (right || left) { g.offer_hl ^= 1; paintOffer(); }
      else if (e == InputEvent::PRESS) offerChoose(g.offer_hl);
      else if (e == InputEvent::LONG_PRESS) back();
      break;
    case Page::LIST:
      g.offer_pending = false;   // the user is choosing from the list: do not jump away
      if (right && g.hl < g.n_rows - 1) { ++g.hl; paintRows(); }
      else if (left && g.hl > 0) { --g.hl; paintRows(); }
      else if (e == InputEvent::PRESS) activateRow(g.hl);
      else if (e == InputEvent::LONG_PRESS) back();
      break;
    case Page::KEYS: {
      uint16_t n = g.n_keys[g.mode];
      uint16_t sel = lv_btnmatrix_get_selected_btn(g.kb);
      if (sel == LV_BTNMATRIX_BTN_NONE || sel >= n) sel = 0;
      if (right) lv_btnmatrix_set_selected_btn(g.kb, (uint16_t)((sel + 1) % n));
      else if (left) lv_btnmatrix_set_selected_btn(g.kb, (uint16_t)((sel + n - 1) % n));
      else if (e == InputEvent::PRESS) keyPressed(sel);
      else if (e == InputEvent::LONG_PRESS) back();
      break;
    }
    case Page::MSG:
      if (e == InputEvent::PRESS && g.msg_until) g.msg_until = millis();   // skip the wait
      break;
  }
}

void wifiScreenTick(lv_obj_t *w, uint32_t now) {
  if (!w) return;
  if (now - g.last_poll_ms < POLL_MS) return;
  g.last_poll_ms = now;

  bool need = netWifiNeedsSetup();
  if (need && !g.prev_need && !wifiScreenIsOpen(w)) {
    wifiScreenOpen(w);
    g.auto_opened = true;   // opened because no known network works: closes itself once one does
  }
  g.prev_need = need;
  if (!wifiScreenIsOpen(w)) return;

  switch (g.page) {
    case Page::LIST: {
      uint32_t gen;
      bool scanning;
      WifiScanEntry tmp[1];
      netWifiScanResults(tmp, 0, &gen, &scanning);
      char conn[33];
      netWifiConnectedSsid(conn, sizeof(conn));
      if (gen != g.scan_gen || strcmp(conn, g.connected) != 0) rowsBuild();
      g.scanning = scanning;
      updateSubtitle();
      if (g.offer_pending && !scanning && g.scan_gen > 0) {
        g.offer_pending = false;
        for (int i = 0; i < g.scan_n; ++i)   // strongest saved network that is not already connected
          if (g.scan[i].known && strcmp(g.scan[i].ssid, g.connected) != 0) {
            showOffer(g.scan[i].ssid);
            break;
          }
      }
      if (g.auto_opened && !need && g.connected[0]) closeScreen();   // a known network came into range
      break;
    }
    case Page::MSG:
      if (!g.msg_until) {   // waiting on a join
        WifiJoin j = netWifiJoinStatus();
        char buf[96];
        if (j == WifiJoin::OK) {
          snprintf(buf, sizeof(buf), LV_SYMBOL_OK "  Connected to\n%s", g.ssid);
          showMsg(buf, 1500, Next::CLOSE);
        } else if (j == WifiJoin::FAILED) {
          snprintf(buf, sizeof(buf), "Couldn't join %s.\n%s", g.ssid,
                   g.tried_saved ? "Type the password again." : "Check the password.");
          showMsg(buf, 2500, Next::KEYS_PASS);
        }
      } else if ((int32_t)(now - g.msg_until) >= 0) {
        Next next = g.msg_next;
        g.msg_until = 0;
        g.msg_next = Next::NONE;
        if (next == Next::CLOSE) closeScreen();
        else if (next == Next::KEYS_PASS) {
          // After a failed join keep what was typed so a typo can be fixed; after a
          // failed saved password, or a too-short one, start from what is in the box.
          char typed[wifistore::PASS_MAX + 1] = "";
          if (!g.tried_saved) {   // copy first: openKeys replaces the text area's own buffer
            strncpy(typed, lv_textarea_get_text(g.ta), sizeof(typed) - 1);
            typed[sizeof(typed) - 1] = '\0';
          }
          openKeys(Target::PASS, typed);
          memset(typed, 0, sizeof(typed));
          g.tried_saved = false;
        } else if (next == Next::LIST) showPage(Page::LIST);
      }
      break;
    case Page::OFFER:
      netWifiConnectedSsid(g.connected, sizeof(g.connected));
      updateSubtitle();
      break;
    case Page::KEYS:
      break;
  }
}
