#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
class OpenSkyClient {
public:
  OpenSkyClient(const char *client_id, const char *client_secret);
  bool ensureToken();                       // fetch when missing or within 120 s of expiry; true if a token is held
  // GET states; returns HTTP code (200 ok) or negative HTTPClient error. On 200, *body is heap memory (free()) of *len bytes.
  int  fetchStates(const char *bbox_query, char **body, size_t *len, int *rate_remaining);
  void invalidateToken();
  // HTTP code of the last token request: < 0 means no answer at all (network), > 0 a real reply.
  int  lastTokenCode() const { return last_token_code_; }
  void dropConnection();                    // forget the kept-alive TLS connection (e.g. after a Wi-Fi change)
private:
  int  fetchStatesOnce(const char *bbox_query, char **body, size_t *len, int *rate_remaining);
  const char *id_, *secret_; String token_; uint32_t token_expiry_ms_ = 0;
  // One TLS connection to opensky-network.org kept open between polls, so a
  // poll is a single request instead of a fresh handshake every time (the
  // handshake is what fails on a weak Wi-Fi link).
  WiFiClientSecure states_tls_;
  bool states_tls_ready_ = false;
  int  last_token_code_ = 0;
};
