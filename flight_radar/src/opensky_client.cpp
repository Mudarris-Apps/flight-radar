#include "opensky_client.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "isrg_root_x1.h"

namespace {

const char *TOKEN_URL =
    "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token";
const char *STATES_URL = "https://opensky-network.org/api/states/all?";
const uint16_t HTTP_TIMEOUT_MS = 15000;
const size_t BODY_CHUNK = 8 * 1024;
const size_t BODY_CAP = 256 * 1024;

void configureTls(WiFiClientSecure &client) {
#if OPENSKY_TLS_INSECURE
  client.setInsecure();
#else
  client.setCACert(ISRG_ROOT_X1_PEM);
#endif
}

String urlEncode(const char *s) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  String out;
  for (const char *p = s; *p; ++p) {
    char c = *p;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += HEX_DIGITS[((unsigned char)c) >> 4];
      out += HEX_DIGITS[((unsigned char)c) & 0x0F];
    }
  }
  return out;
}

// Write-only Stream that accumulates the response body in PSRAM. Grows in
// BODY_CHUNK steps and refuses to exceed BODY_CAP (a short write makes
// HTTPClient::writeToStream fail with HTTPC_ERROR_STREAM_WRITE). Always keeps
// one spare byte for a NUL terminator.
class PsramSink : public Stream {
public:
  explicit PsramSink(int expected) {
    if (expected > 0 && (size_t)expected <= BODY_CAP) reserve((size_t)expected + 1);
  }
  ~PsramSink() { heap_caps_free(buf_); }

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *data, size_t n) override {
    if (overflow_) return 0;
    if (len_ + n > BODY_CAP) { overflow_ = true; return 0; }
    if (len_ + n + 1 > cap_) {
      size_t want = cap_ ? cap_ : BODY_CHUNK;
      while (want < len_ + n + 1) want += BODY_CHUNK;
      if (want > BODY_CAP + 1) want = BODY_CAP + 1;
      if (!reserve(want)) return 0;
    }
    memcpy(buf_ + len_, data, n);
    len_ += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

  bool overflow() const { return overflow_; }
  // Hands ownership of the NUL-terminated buffer to the caller.
  char *release(size_t *len) {
    if (!buf_ && !reserve(1)) return nullptr;
    buf_[len_] = '\0';
    *len = len_;
    char *out = buf_;
    buf_ = nullptr; len_ = cap_ = 0;
    return out;
  }

private:
  bool reserve(size_t want) {
    void *p = heap_caps_realloc(buf_, want, MALLOC_CAP_SPIRAM);
    if (!p) return false;
    buf_ = (char *)p;
    cap_ = want;
    return true;
  }
  char *buf_ = nullptr;
  size_t len_ = 0, cap_ = 0;
  bool overflow_ = false;
};

}  // namespace

OpenSkyClient::OpenSkyClient(const char *client_id, const char *client_secret)
    : id_(client_id), secret_(client_secret) {}

void OpenSkyClient::invalidateToken() {
  token_ = String();
  token_expiry_ms_ = 0;
}

bool OpenSkyClient::ensureToken() {
  // token_expiry_ms_ already has the 120 s margin subtracted.
  if (token_.length() > 0 && (int32_t)(token_expiry_ms_ - millis()) > 0) return true;
  invalidateToken();

  WiFiClientSecure client;
  configureTls(client);
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, TOKEN_URL)) {
    Serial.println("[net] token: http.begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String form = String("grant_type=client_credentials&client_id=") + urlEncode(id_) +
                "&client_secret=" + urlEncode(secret_);
  int code = http.POST(form);
  if (code != 200) {
    Serial.printf("[net] token: http=%d %s\n", code,
                  code < 0 ? HTTPClient::errorToString(code).c_str() : "");
    http.end();
    return false;
  }
  String resp = http.getString();
  http.end();

  JsonDocument filter;
  filter["access_token"] = true;
  filter["expires_in"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, resp, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("[net] token: json error %s\n", err.c_str());
    return false;
  }
  const char *tok = doc["access_token"] | (const char *)nullptr;
  uint32_t expires_in = doc["expires_in"] | 0u;
  if (!tok || !*tok || expires_in <= 120) {
    Serial.printf("[net] token: missing access_token or expires_in=%u\n", (unsigned)expires_in);
    return false;
  }
  token_ = tok;
  token_expiry_ms_ = millis() + (expires_in - 120) * 1000u;
  Serial.printf("[net] token: ok, expires_in=%us\n", (unsigned)expires_in);
  return true;
}

int OpenSkyClient::fetchStates(const char *bbox_query, char **body, size_t *len, int *rate_remaining) {
  *body = nullptr;
  *len = 0;
  *rate_remaining = -1;

  WiFiClientSecure client;
  configureTls(client);
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(HTTP_TIMEOUT_MS);
  String url = String(STATES_URL) + bbox_query + "&extended=1";
  if (!http.begin(client, url)) return HTTPC_ERROR_CONNECTION_REFUSED;
  http.addHeader("Authorization", "Bearer " + token_);
  const char *hdrs[] = {"X-Rate-Limit-Remaining"};
  http.collectHeaders(hdrs, 1);
  int code = http.GET();

  if (http.hasHeader("X-Rate-Limit-Remaining")) {
    String v = http.header("X-Rate-Limit-Remaining");
    if (v.length() > 0) *rate_remaining = v.toInt();
  }

  if (code != 200) {
    http.end();
    return code;
  }

  int expected = http.getSize();
  if (expected > (int)BODY_CAP) {
    Serial.printf("[net] states: body too large (%d bytes)\n", expected);
    http.end();
    return HTTPC_ERROR_TOO_LESS_RAM;
  }
  PsramSink sink(expected);
  int written = http.writeToStream(&sink);
  http.end();
  if (written < 0 || sink.overflow()) {
    Serial.printf("[net] states: body read failed (%d%s)\n", written,
                  sink.overflow() ? ", over 256 KB cap" : "");
    return written < 0 ? written : HTTPC_ERROR_TOO_LESS_RAM;
  }
  *body = sink.release(len);
  if (!*body) return HTTPC_ERROR_TOO_LESS_RAM;
  return 200;
}
