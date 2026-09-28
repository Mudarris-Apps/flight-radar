// detail_card.cpp: a 300 x 250 panel at the bottom of the scope, shown by long press.
#include "detail_card.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "metadata_table.h"
#include "net_task.h"
#include "units.h"

namespace {

struct Card {
  UiState *st = nullptr;
  lv_obj_t *header = nullptr;
  lv_obj_t *body = nullptr;
};
Card g_card;   // one card per UI

void setTextIfChanged(lv_obj_t *label, const char *text) {
  const char *cur = lv_label_get_text(label);
  if (!cur || strcmp(cur, text) != 0) lv_label_set_text(label, text);
}

const char *orUnknown(const char *s) { return (s && s[0]) ? s : "Unknown"; }

const Aircraft *selectedAircraft(const UiState *st) {
  if (!st || !st->snap || !st->selected_icao24[0]) return nullptr;
  for (size_t i = 0; i < st->snap_n; ++i)
    if (strncmp(st->snap[i].last.icao24, st->selected_icao24, sizeof(st->selected_icao24)) == 0) return &st->snap[i];
  return nullptr;
}

}  // namespace

lv_obj_t *detailCardCreate(lv_obj_t *parent, UiState *st) {
  g_card.st = st;
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 300, 250);
  lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_radius(card, 18, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x0C1218), 0);
  lv_obj_set_style_bg_opa(card, 235, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x2F8F5A), 0);
  lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_user_data(card, &g_card);

  g_card.header = lv_label_create(card);
  lv_obj_set_width(g_card.header, lv_pct(100));
  lv_obj_align(g_card.header, LV_ALIGN_TOP_MID, 0, 0);
  lv_label_set_long_mode(g_card.header, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(g_card.header, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(g_card.header, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(g_card.header, lv_color_white(), 0);
  lv_label_set_text(g_card.header, "");

  // Fixed-width wrapping block centred under the header. The round glass clips the card's
  // lower corners: at the last body row the visible chord starts about 29 px inside the
  // card's left edge, so a 240 px block (20 px inset from the 278 px content box) keeps
  // every line start on screen. Lines are left-aligned within it; long ones wrap.
  g_card.body = lv_label_create(card);
  lv_obj_set_width(g_card.body, 240);
  lv_label_set_long_mode(g_card.body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(g_card.body, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(g_card.body, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_set_style_text_font(g_card.body, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(g_card.body, lv_color_hex(0xC8D2DC), 0);
  lv_label_set_text(g_card.body, "");
  return card;
}

void detailCardRefresh(lv_obj_t *card) {
  if (!card) return;
  Card *c = static_cast<Card *>(lv_obj_get_user_data(card));
  const Aircraft *a = c ? selectedAircraft(c->st) : nullptr;
  if (!a || !c->st->card_open) {
    if (!lv_obj_has_flag(card, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const AircraftReport &r = a->last;
  float alt_m = std::isnan(r.baro_alt_m) ? r.geo_alt_m : r.baro_alt_m;
  char alt_ft[16], alt_mm[16], vs[16], gs[16], trk[16], lat[16], lon[16];
  fmtOrUnknown(alt_ft, sizeof(alt_ft), mToFt(alt_m), "%.0f ft");
  fmtOrUnknown(alt_mm, sizeof(alt_mm), alt_m, "%.0f m");
  fmtOrUnknown(vs, sizeof(vs), mpsToFpm(r.vertical_rate_mps), "%.0f fpm");
  fmtOrUnknown(gs, sizeof(gs), mpsToKt(r.velocity_mps), "%.0f kt");
  fmtOrUnknown(trk, sizeof(trk), r.track_deg, "%.0f°");
  fmtOrUnknown(lat, sizeof(lat), r.lat, "%.4f");
  fmtOrUnknown(lon, sizeof(lon), r.lon, "%.4f");
  const MetadataRow *m = a->meta;
  uint32_t now = netNowEpoch();
  unsigned ago = now > r.last_contact ? (unsigned)(now - r.last_contact) : 0u;

  char body[512];
  snprintf(body, sizeof(body),
           "ICAO24  %s\nSquawk  %s\nAlt     %s (%s)\nV/S     %s\nGS      %s\nTrack   %s\nLat/Lon %s, %s\n"
           "Reg     %s\nType    %s\nModel   %s\nOperator %s\nGround  %s\nUpdated %us ago",
           r.icao24, orUnknown(r.squawk), alt_ft, alt_mm, vs, gs, trk, lat, lon,
           orUnknown(m ? m->reg : nullptr), orUnknown(m ? m->typecode : nullptr),
           orUnknown(m ? m->model : nullptr), orUnknown(m ? m->op : nullptr),
           r.on_ground ? "Yes" : "No", ago);
  setTextIfChanged(c->header, r.callsign[0] ? r.callsign : r.icao24);
  setTextIfChanged(c->body, body);
  if (lv_obj_has_flag(card, LV_OBJ_FLAG_HIDDEN)) lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
}
