// wifi_store.cpp: NVS namespace "wifi" holds n (count) and s<i>/p<i> (SSID/password), index 0 newest.
#include "wifi_store.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cstring>
#include "../secrets.h"

namespace wifistore {
namespace {

const char *const NVS_NS = "wifi";

void copyStr(char *dst, size_t n, const char *src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = '\0';
}

bool homeConfigured() {
  return WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "your-ssid") != 0;
}

int loadSaved(Cred *out, int max) {
  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) return 0;   // read-write: creates the namespace on first boot
  int n = prefs.getInt("n", 0);
  if (n < 0) n = 0;
  if (n > MAX_SAVED) n = MAX_SAVED;
  if (n > max) n = max;
  int got = 0;
  for (int i = 0; i < n; ++i) {
    char ks[4], kp[4];
    snprintf(ks, sizeof(ks), "s%d", i);
    snprintf(kp, sizeof(kp), "p%d", i);
    String s = prefs.getString(ks, "");
    if (s.length() == 0) continue;
    copyStr(out[got].ssid, sizeof(out[got].ssid), s.c_str());
    copyStr(out[got].pass, sizeof(out[got].pass), prefs.getString(kp, "").c_str());
    ++got;
  }
  prefs.end();
  return got;
}

}  // namespace

int known(Cred *out, int max) {
  int n = loadSaved(out, max);
  if (n < max && homeConfigured()) {
    for (int i = 0; i < n; ++i)
      if (strcmp(out[i].ssid, WIFI_SSID) == 0) return n;
    copyStr(out[n].ssid, sizeof(out[n].ssid), WIFI_SSID);
    copyStr(out[n].pass, sizeof(out[n].pass), WIFI_PASS);
    ++n;
  }
  return n;
}

bool find(const char *ssid, char *pass_out, size_t pass_n) {
  Cred all[MAX_SAVED + 1];
  int n = known(all, MAX_SAVED + 1);
  for (int i = 0; i < n; ++i) {
    if (strcmp(all[i].ssid, ssid) == 0) {
      copyStr(pass_out, pass_n, all[i].pass);
      return true;
    }
  }
  return false;
}

void save(const char *ssid, const char *pass) {
  Cred list[MAX_SAVED];
  int n = loadSaved(list, MAX_SAVED);
  Cred out[MAX_SAVED];
  int m = 0;
  copyStr(out[m].ssid, sizeof(out[m].ssid), ssid);
  copyStr(out[m].pass, sizeof(out[m].pass), pass);
  ++m;
  for (int i = 0; i < n && m < MAX_SAVED; ++i)
    if (strcmp(list[i].ssid, ssid) != 0) out[m++] = list[i];

  Preferences prefs;
  if (!prefs.begin(NVS_NS, false)) {
    Serial.println("[wifi] nvs open failed");
    return;
  }
  for (int i = 0; i < m; ++i) {
    char ks[4], kp[4];
    snprintf(ks, sizeof(ks), "s%d", i);
    snprintf(kp, sizeof(kp), "p%d", i);
    prefs.putString(ks, out[i].ssid);
    prefs.putString(kp, out[i].pass);
  }
  prefs.putInt("n", m);
  prefs.end();
  Serial.printf("[wifi] saved \"%s\" (%d saved)\n", ssid, m);   // never log the password
}

}  // namespace wifistore
