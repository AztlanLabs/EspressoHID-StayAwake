#include "event_log.h"

#include <time.h>

namespace {
static constexpr uint8_t LOG_MAX = 40;
static String items[LOG_MAX];
static uint8_t head = 0;
static uint8_t count_ = 0;
}

void eventLogAdd(const String& msg) {
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm t;
    localtime_r(&now, &t);
    char buf[24];
    // YYYY-MM-DD HH:MM:SS
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    items[head] = String(buf) + " " + msg;
  } else {
    items[head] = String(millis()) + "ms " + msg;
  }
  head = (uint8_t)((head + 1) % LOG_MAX);
  if (count_ < LOG_MAX) count_++;
}

void eventLogToJson(String& out) {
  out = "[";
  for (uint8_t i = 0; i < count_; i++) {
    const uint8_t idx = (uint8_t)((head + LOG_MAX - count_ + i) % LOG_MAX);
    if (i) out += ',';
    out += '"';
    // Basic JSON string escaping for quotes/backslashes/newlines
    const String& s = items[idx];
    for (size_t k = 0; k < s.length(); k++) {
      const char c = s[k];
      if (c == '\\') out += "\\\\";
      else if (c == '"') out += "\\\"";
      else if (c == '\n') out += "\\n";
      else if (c == '\r') out += "\\r";
      else out += c;
    }
    out += '"';
  }
  out += "]";
}

void eventLogClear() {
  for (uint8_t i = 0; i < LOG_MAX; i++) items[i] = "";
  head = 0;
  count_ = 0;
}
