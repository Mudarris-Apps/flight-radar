// detail_card.cpp: the selected aircraft's details, shown by long press.
//
//   [logo] QFA455                [B738]
//          Qantas
//          Boeing 737-838 · VH-VXA
//   ─────────────────────────────────────
//   ALTITUDE     SPEED       HEADING
//   36,000 ft    452 kt      213°
//   CLIMB        SQUAWK      UPDATED
//   ▲ 1,200 fpm  3301        4s ago
//              ICAO 7c6d7b
//
// Sized and placed so every line stays inside the round glass.
#include "detail_card.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "airline_logos.h"
#include "metadata_table.h"
#include "net_task.h"
#include "units.h"

namespace {

const lv_coord_t CARD_W = 300, CARD_H = 212;
const lv_coord_t CARD_BOTTOM_GAP = 44;   // keeps the bottom corners inside the circle
const lv_coord_t PAD = 12;
const int TILE_COLS = 3, TILE_ROWS = 2;

const uint32_t COL_BG = 0x0B1117, COL_BORDER = 0x24543A, COL_ACCENT = 0x4ADE80;
const uint32_t COL_TEXT = 0xF1F5F9, COL_SUB = 0x94A3B8, COL_DIM = 0x5B6B7C, COL_CHIP = 0x163323;

struct Card {
  UiState *st = nullptr;
  lv_obj_t *logo = nullptr;
  lv_obj_t *callsign = nullptr;
  lv_obj_t *chip = nullptr, *chip_text = nullptr;
  lv_obj_t *airline = nullptr;
  lv_obj_t *model = nullptr;
  lv_obj_t *tile_val[TILE_COLS * TILE_ROWS] = {};
  lv_obj_t *footer = nullptr;
  lv_img_dsc_t logo_dsc = {};
  int logo_idx = -2;
};
Card g_card;   // one card per UI

void setTextIfChanged(lv_obj_t *label, const char *text) {
  const char *cur = lv_label_get_text(label);
  if (!cur || strcmp(cur, text) != 0) lv_label_set_text(label, text);
}

void setHidden(lv_obj_t *o, bool hidden) {
  if (hidden != lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
    if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, "");
  return l;
}

// "36000" -> "36,000"; works for negatives.
void withCommas(char *out, size_t n, long v) {
  char digits[16];
  snprintf(digits, sizeof(digits), "%ld", v < 0 ? -v : v);
  size_t len = strlen(digits), o = 0;
  if (v < 0 && o + 1 < n) out[o++] = '-';
  for (size_t i = 0; i < len && o + 1 < n; ++i) {
    out[o++] = digits[i];
    size_t left = len - i - 1;
    if (left > 0 && left % 3 == 0 && o + 1 < n) out[o++] = ',';
  }
  out[o] = '\0';
}

const char *dash(const char *s) { return (s && s[0]) ? s : "-"; }

const Aircraft *selectedAircraft(const UiState *st) {
  if (!st || !st->snap || !st->selected_icao24[0]) return nullptr;
  for (size_t i = 0; i < st->snap_n; ++i)
    if (strncmp(st->snap[i].last.icao24, st->selected_icao24, sizeof(st->selected_icao24)) == 0) return &st->snap[i];
  return nullptr;
}

void updateLogo(Card *c, const char *callsign) {
  int idx = airlineLogoIndex(callsign);
  if (idx == c->logo_idx) return;
  c->logo_idx = idx;
  if (idx < 0) {
    setHidden(c->logo, true);
    lv_obj_align(c->callsign, LV_ALIGN_TOP_LEFT, 0, 0);
    return;
  }
  c->logo_dsc.header.always_zero = 0;
  c->logo_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  c->logo_dsc.header.w = AIRLINE_LOGO_W;
  c->logo_dsc.header.h = AIRLINE_LOGO_H;
  c->logo_dsc.data_size = AIRLINE_LOGO_BYTES;
  c->logo_dsc.data = AIRLINE_LOGOS[idx].data;
  lv_img_set_src(c->logo, &c->logo_dsc);
  lv_img_cache_invalidate_src(&c->logo_dsc);
  setHidden(c->logo, false);
  lv_obj_align(c->callsign, LV_ALIGN_TOP_LEFT, AIRLINE_LOGO_W + 8, 0);
}

}  // namespace

lv_obj_t *detailCardCreate(lv_obj_t *parent, UiState *st) {
  Card &c = g_card;
  c.st = st;
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, CARD_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -CARD_BOTTOM_GAP);
  lv_obj_set_style_radius(card, 26, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(card, 240, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, PAD, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_user_data(card, &c);
  const lv_coord_t inner_w = CARD_W - 2 * PAD;

  // Header: logo + callsign on the left, aircraft type chip on the right.
  c.logo = lv_img_create(card);
  lv_obj_align(c.logo, LV_ALIGN_TOP_LEFT, 0, 1);
  lv_obj_add_flag(c.logo, LV_OBJ_FLAG_HIDDEN);

  c.callsign = makeLabel(card, &lv_font_montserrat_22, COL_TEXT);
  lv_obj_align(c.callsign, LV_ALIGN_TOP_LEFT, 0, 0);

  c.chip = lv_obj_create(card);
  lv_obj_remove_style_all(c.chip);
  lv_obj_set_size(c.chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(c.chip, lv_color_hex(COL_CHIP), 0);
  lv_obj_set_style_bg_opa(c.chip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c.chip, 8, 0);
  lv_obj_set_style_pad_hor(c.chip, 8, 0);
  lv_obj_set_style_pad_ver(c.chip, 3, 0);
  lv_obj_align(c.chip, LV_ALIGN_TOP_RIGHT, 0, 2);
  c.chip_text = makeLabel(c.chip, &lv_font_montserrat_14, COL_ACCENT);

  c.airline = makeLabel(card, &lv_font_montserrat_16, COL_ACCENT);
  lv_obj_set_width(c.airline, inner_w);
  lv_label_set_long_mode(c.airline, LV_LABEL_LONG_DOT);
  lv_obj_align(c.airline, LV_ALIGN_TOP_LEFT, 0, 28);

  c.model = makeLabel(card, &lv_font_montserrat_12, COL_SUB);
  lv_obj_set_width(c.model, inner_w);
  lv_label_set_long_mode(c.model, LV_LABEL_LONG_DOT);
  lv_obj_align(c.model, LV_ALIGN_TOP_LEFT, 0, 49);

  lv_obj_t *rule = lv_obj_create(card);
  lv_obj_remove_style_all(rule);
  lv_obj_set_size(rule, inner_w, 1);
  lv_obj_set_style_bg_color(rule, lv_color_hex(COL_BORDER), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_align(rule, LV_ALIGN_TOP_LEFT, 0, 70);

  // 3 x 2 grid of caption/value tiles.
  static const char *CAPTIONS[TILE_COLS * TILE_ROWS] = {"ALTITUDE", "SPEED", "HEADING", "CLIMB", "SQUAWK", "UPDATED"};
  const lv_coord_t tile_w = inner_w / TILE_COLS;
  for (int i = 0; i < TILE_COLS * TILE_ROWS; ++i) {
    lv_coord_t x = (i % TILE_COLS) * tile_w, y = 80 + (i / TILE_COLS) * 44;
    lv_obj_t *cap = makeLabel(card, &lv_font_montserrat_10, COL_DIM);
    lv_label_set_text(cap, CAPTIONS[i]);
    lv_obj_set_style_text_letter_space(cap, 1, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, x, y);
    c.tile_val[i] = makeLabel(card, &lv_font_montserrat_16, COL_TEXT);
    lv_obj_set_width(c.tile_val[i], tile_w - 4);
    lv_label_set_long_mode(c.tile_val[i], LV_LABEL_LONG_CLIP);
    lv_obj_align(c.tile_val[i], LV_ALIGN_TOP_LEFT, x, y + 13);
  }

  c.footer = makeLabel(card, &lv_font_montserrat_10, COL_DIM);
  lv_obj_set_width(c.footer, inner_w);
  lv_obj_set_style_text_align(c.footer, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(c.footer, LV_ALIGN_BOTTOM_MID, 0, 0);
  return card;
}

void detailCardRefresh(lv_obj_t *card) {
  if (!card) return;
  Card *c = static_cast<Card *>(lv_obj_get_user_data(card));
  const Aircraft *a = c ? selectedAircraft(c->st) : nullptr;
  if (!a || !c->st->card_open) {
    setHidden(card, true);
    return;
  }
  const AircraftReport &r = a->last;
  const MetadataRow *m = a->has_meta ? &a->meta : nullptr;
  char buf[64], num[16];

  // Header
  setTextIfChanged(c->callsign, r.callsign[0] ? r.callsign : (m && m->reg[0] ? m->reg : r.icao24));
  updateLogo(c, r.callsign);
  const char *type = m ? m->typecode : "";
  setTextIfChanged(c->chip_text, type[0] ? type : "");
  setHidden(c->chip, !type[0]);

  const char *airline = airlineNameForCallsign(r.callsign);
  if (!airline && m && m->op[0]) airline = m->op;
  setTextIfChanged(c->airline, airline ? airline : (r.callsign[0] ? "Unknown airline" : "Private"));

  if (m && (m->model[0] || m->reg[0]))
    snprintf(buf, sizeof(buf), "%s%s%s", m->model, (m->model[0] && m->reg[0]) ? "  " LV_SYMBOL_BULLET "  " : "", m->reg);
  else
    snprintf(buf, sizeof(buf), "Aircraft details unknown");
  setTextIfChanged(c->model, buf);

  // Tiles
  if (r.on_ground) {
    snprintf(buf, sizeof(buf), "On ground");
  } else {
    float alt_m = std::isnan(r.baro_alt_m) ? r.geo_alt_m : r.baro_alt_m;
    if (std::isnan(alt_m)) snprintf(buf, sizeof(buf), "-");
    else { withCommas(num, sizeof(num), lroundf(mToFt(alt_m))); snprintf(buf, sizeof(buf), "%s ft", num); }
  }
  setTextIfChanged(c->tile_val[0], buf);

  if (std::isnan(r.velocity_mps)) snprintf(buf, sizeof(buf), "-");
  else snprintf(buf, sizeof(buf), "%ld kt", lroundf(mpsToKt(r.velocity_mps)));
  setTextIfChanged(c->tile_val[1], buf);

  if (std::isnan(r.track_deg)) snprintf(buf, sizeof(buf), "-");
  else snprintf(buf, sizeof(buf), "%03ld°", lroundf(r.track_deg) % 360);
  setTextIfChanged(c->tile_val[2], buf);

  if (std::isnan(r.vertical_rate_mps)) {
    snprintf(buf, sizeof(buf), "-");
  } else {
    long fpm = lroundf(mpsToFpm(r.vertical_rate_mps) / 50.f) * 50;   // to the nearest 50 fpm
    if (fpm == 0) snprintf(buf, sizeof(buf), "Level");
    else {
      withCommas(num, sizeof(num), fpm < 0 ? -fpm : fpm);
      snprintf(buf, sizeof(buf), "%s %s", fpm > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, num);
    }
  }
  setTextIfChanged(c->tile_val[3], buf);

  setTextIfChanged(c->tile_val[4], dash(r.squawk));

  uint32_t now = netNowEpoch();
  unsigned ago = now > r.last_contact ? (unsigned)(now - r.last_contact) : 0u;
  snprintf(buf, sizeof(buf), "%us ago", ago);
  setTextIfChanged(c->tile_val[5], buf);

  snprintf(buf, sizeof(buf), "ICAO %s", r.icao24);
  setTextIfChanged(c->footer, buf);

  setHidden(card, false);
}
