// wifi_store.h: Wi-Fi networks joined from the on-device keyboard, kept in NVS.
// The home network from secrets.h is always known as well; a saved entry with the
// same SSID wins, so a password changed on the device overrides the compiled one.
#pragma once
#include <stddef.h>

namespace wifistore {

const int MAX_SAVED = 5;
const size_t SSID_MAX = 32;   // 802.11 limit, excluding the terminator
const size_t PASS_MAX = 63;   // WPA2 passphrase limit, excluding the terminator

struct Cred {
  char ssid[SSID_MAX + 1];
  char pass[PASS_MAX + 1];
};

// Every known network: saved entries (most recently joined first), then the
// secrets.h home network unless it is unset or already saved. Returns the count.
int  known(Cred *out, int max);
// Looks up `ssid` among the known networks; copies its password into `pass_out`.
bool find(const char *ssid, char *pass_out, size_t pass_n);
// Saves (or refreshes) a network as the most recently joined; drops the oldest past MAX_SAVED.
void save(const char *ssid, const char *pass);

}  // namespace wifistore
