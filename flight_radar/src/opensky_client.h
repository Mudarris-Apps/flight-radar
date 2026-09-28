#pragma once
#include <Arduino.h>
class OpenSkyClient {
public:
  OpenSkyClient(const char *client_id, const char *client_secret);
  bool ensureToken();                       // fetch when missing or within 120 s of expiry; true if a token is held
  // GET states; returns HTTP code (200 ok) or negative HTTPClient error. On 200, *body is heap memory (free()) of *len bytes.
  int  fetchStates(const char *bbox_query, char **body, size_t *len, int *rate_remaining);
  void invalidateToken();
private:
  const char *id_, *secret_; String token_; uint32_t token_expiry_ms_ = 0;
};
