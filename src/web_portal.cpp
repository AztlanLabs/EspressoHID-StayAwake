#include "web_portal.h"

#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <time.h>

#include "runtime_config.h"
#include "actions.h"
#include "control.h"
#include "event_log.h"
#include "state.h"
#include "profiles.h"
#include "device_status.h"

namespace {

static WebServer server(80);
static DNSServer dns;
static bool apMode = false;
static String apSsid;

static bool staWanted = false;
static uint8_t staAttempts = 0;
static unsigned long nextStaAttemptAt = 0;
static bool wifiSuppressedThisBoot = false;

static const char* mdnsHost = "fakekeyboard";

static void mdnsStop() {
  MDNS.end();
}

static void mdnsStart() {
  mdnsStop();
  if (!MDNS.begin(mdnsHost)) {
    eventLogAdd("mDNS begin failed");
    return;
  }
  MDNS.addService("http", "tcp", 80);
}

static String portalPageHtml();

static String jsonBool(bool v) { return v ? "true" : "false"; }

static String jsonStr(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  out += '"';
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  out += '"';
  return out;
}

static String wifiModeString() {
  if (apMode) return "AP";
  if (WiFi.status() == WL_CONNECTED) return "STA";
  if (staWanted) return "STA_CONNECTING";
  if (wifiSuppressedThisBoot) return "OFF";
  return "DISCONNECTED";
}

static String ipToString(const IPAddress& ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

static String buildActionsJson() {
  String out = "[";
  const int n = actionsCount();
  for (int i = 0; i < n; i++) {
    if (i) out += ',';
    out += "{\"id\":";
    out += String(i);
    out += ",\"name\":\"";
    out += actionName(i);
    out += "\",\"enabled\":";
    out += jsonBool(actionEnabled(i));
    out += ",\"wA\":";
    out += String(actionWeightActive(i));
    out += ",\"wM\":";
    out += String(actionWeightMeeting(i));
    out += '}';
  }
  out += "]";
  return out;
}

static void sendJson(const String& body, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

static void sendPortalHtml() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", portalPageHtml());
}

static void sendPortalForProbe() {
  // Many OS captive-portal checks expect any non-success response.
  // Returning the portal HTML with HTTP 200 is generally enough to trigger the UI.
  sendPortalHtml();
}

static void handleApiStatus() {
  const unsigned long now = millis();
  const unsigned long nextIn = (nextActionTime > now) ? (nextActionTime - now) : 0;
  const unsigned long sleepRemaining = (isSleeping && sleepUntil > now) ? (sleepUntil - now) : 0;

  String body = "{";
  body += "\"active\":" + jsonBool(isActive) + ",";
  body += "\"sleeping\":" + jsonBool(isSleeping) + ",";
  body += "\"sleepRemainingMs\":" + String(sleepRemaining) + ",";
  body += "\"profileId\":" + String((int)currentProfile) + ",";
  body += "\"profile\":\"" + String(profileName()) + "\",";
  body += "\"actionCount\":" + String(actionCount) + ",";
  body += "\"nextInMs\":" + String(nextIn) + ",";
  body += "\"fwVersion\":\"" + String(FIRMWARE_VERSION) + "\",";
  body += "\"mdns\":\"" + String(mdnsHost) + "\",";
  body += "\"wifi\":{\"mode\":\"" + wifiModeString() + "\",";
  body += "\"ssid\":\"" + String(apMode ? apSsid : WiFi.SSID()) + "\",";
  body += "\"ip\":\"" + String(apMode ? ipToString(WiFi.softAPIP()) : ipToString(WiFi.localIP())) + "\"}";
  body += "}";

  sendJson(body);
}

static void handleApiLogs() {
  String out;
  eventLogToJson(out);
  sendJson(out);
}

static String buildProfilesJson() {
  String out = "[";
  for (uint8_t p = 0; p < PROFILE_COUNT; p++) {
    if (p) out += ',';
    out += "{\"id\":";
    out += String(p);
    out += ",\"name\":";
    out += jsonStr(profileName((Profile)p));
    out += ",\"minMs\":";
    out += String(runtimeConfigProfileIntervalMinMs((Profile)p));
    out += ",\"maxMs\":";
    out += String(runtimeConfigProfileIntervalMaxMs((Profile)p));
    out += '}';
  }
  out += "]";
  return out;
}

static void handleApiConfigGet() {
  const RuntimeConfig& cfg = runtimeConfigGet();

  String body = "{";
  body += "\"provisioned\":" + jsonBool(cfg.provisioned) + ",";
  body += "\"wifiConfigured\":" + jsonBool(runtimeConfigHasWifiCredentials()) + ",";
  body += "\"profiles\":" + buildProfilesJson() + ",";
  body += "\"actionMask\":" + String(runtimeConfigActionEnabledMask()) + ",";
  body += "\"customText\":" + jsonStr(String(runtimeConfigCustomText())) + ",";
  body += "\"actions\":" + buildActionsJson();
  body += "}";

  sendJson(body);
}

static uint32_t parseU32(const String& s, uint32_t fallback) {
  if (!s.length()) return fallback;
  char* endp = nullptr;
  const uint32_t v = (uint32_t)strtoul(s.c_str(), &endp, 10);
  if (endp == s.c_str()) return fallback;
  return v;
}

static uint32_t parseSecondsToMs(const String& s, uint32_t fallbackMs) {
  if (!s.length()) return fallbackMs;
  char* endp = nullptr;
  double v = strtod(s.c_str(), &endp);
  if (endp == s.c_str()) return fallbackMs;
  if (v < 0) v = 0;
  const double ms = v * 1000.0;
  if (ms > 4294967295.0) return 0xFFFFFFFFu;
  return (uint32_t)(ms + 0.5);
}

static void handleApiConfigPost() {
  // Expect application/x-www-form-urlencoded
  for (uint8_t p = 0; p < PROFILE_COUNT; p++) {
    const String kMin = String("p") + String(p) + "MinMs";
    const String kMax = String("p") + String(p) + "MaxMs";
    const String kMinS = String("p") + String(p) + "MinS";
    const String kMaxS = String("p") + String(p) + "MaxS";
    if (!server.hasArg(kMin) && !server.hasArg(kMax) && !server.hasArg(kMinS) && !server.hasArg(kMaxS)) continue;

    const uint32_t curMin = runtimeConfigProfileIntervalMinMs((Profile)p);
    const uint32_t curMax = runtimeConfigProfileIntervalMaxMs((Profile)p);
    const uint32_t minMs = server.hasArg(kMinS) ? parseSecondsToMs(server.arg(kMinS), curMin)
                                                : parseU32(server.arg(kMin), curMin);
    const uint32_t maxMs = server.hasArg(kMaxS) ? parseSecondsToMs(server.arg(kMaxS), curMax)
                                                : parseU32(server.arg(kMax), curMax);
    runtimeConfigSetProfileIntervalsMs((Profile)p, minMs, maxMs);
  }

  if (server.hasArg("actionMask")) {
    const uint32_t mask = parseU32(server.arg("actionMask"), runtimeConfigActionEnabledMask());
    runtimeConfigSetActionEnabledMask(mask);
  }

  // Optional runtime weight overrides
  {
    bool any = false;
    bool seeded = false;
    const int n = actionsCount();
    for (int i = 0; i < n && i < 32; i++) {
      const String kA = String("wA") + String(i);
      const String kM = String("wM") + String(i);
      if (!server.hasArg(kA) && !server.hasArg(kM)) continue;

      if (!seeded && !runtimeConfigActionWeightsConfigured()) {
        // Initialize the whole table from compile-time defaults so unset entries
        // don’t implicitly become 0 when weightsConfigured flips true.
        for (int j = 0; j < n && j < 32; j++) {
          runtimeConfigSetActionWeight((uint8_t)j, actionWeightActive(j), actionWeightMeeting(j));
        }
        seeded = true;
      }

      uint8_t curA = actionWeightActive(i);
      uint8_t curM = actionWeightMeeting(i);
      if (server.hasArg(kA)) {
        uint32_t v = parseU32(server.arg(kA), curA);
        if (v > 255) v = 255;
        curA = (uint8_t)v;
      }
      if (server.hasArg(kM)) {
        uint32_t v = parseU32(server.arg(kM), curM);
        if (v > 255) v = 255;
        curM = (uint8_t)v;
      }
      runtimeConfigSetActionWeight((uint8_t)i, curA, curM);
      any = true;
    }
    if (any) {
      eventLogAdd("Weights updated via web");
    }
  }

  if (server.hasArg("customText")) {
    runtimeConfigSetCustomText(server.arg("customText").c_str());
  }

  eventLogAdd("Config updated via web");

  sendJson("{\"ok\":true}");
}

static void handleApiHistory() {
  String hist;
  actionHistoryToJson(hist);
  sendJson(hist);
}

static void handleApiTrigger() {
  const int id = server.hasArg("id") ? server.arg("id").toInt() : -1;
  const bool ok = performActionByIndex(id, ACTION_SRC_MANUAL);
  if (ok) {
    actionCount++;
    eventLogAdd(String("Manual action: ") + actionName(id));
    sendJson("{\"ok\":true}");
  } else {
    sendJson("{\"ok\":false}", 400);
  }
}

static void handleApiControl() {
  // application/x-www-form-urlencoded
  if (server.hasArg("active")) {
    const bool v = (server.arg("active") == "1" || server.arg("active") == "true");
    controlSetActive(v);
    eventLogAdd(String("Set active=") + (v ? "true" : "false"));
  }

  if (server.hasArg("profile")) {
    const int p = server.arg("profile").toInt();
    if (p >= 0 && p < (int)PROFILE_COUNT) {
      controlSetProfile((Profile)p);
      eventLogAdd(String("Set profile=") + profileName((Profile)p));
    }
  }

  if (server.hasArg("sleepS")) {
    const uint32_t d = parseSecondsToMs(server.arg("sleepS"), 60000);
    controlSleepNow(d, "Sleep (web)");
    eventLogAdd(String("Sleep now ") + String(d / 1000) + "s");
  } else if (server.hasArg("sleepMs")) {
    const uint32_t d = parseU32(server.arg("sleepMs"), 60000);
    controlSleepNow(d, "Sleep (web)");
    eventLogAdd(String("Sleep now ") + String(d / 1000) + "s");
  }

  if (server.hasArg("wake")) {
    controlWakeNow();
    eventLogAdd("Wake now");
  }

  if (server.hasArg("clearHistory")) {
    actionHistoryClear();
    eventLogAdd("Action history cleared");
  }

  if (server.hasArg("factoryReset")) {
    eventLogAdd("Factory reset requested");
    runtimeConfigFactoryReset();
    sendJson("{\"ok\":true,\"restarting\":true}");
    delay(300);
    ESP.restart();
    return;
  }

  if (server.hasArg("reboot")) {
    eventLogAdd("Reboot requested");
    sendJson("{\"ok\":true,\"restarting\":true}");
    delay(300);
    ESP.restart();
    return;
  }

  sendJson("{\"ok\":true}");
}

static void handleOtaUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    Update.end(true);
  }
}

static void handleApiOta() {
  const bool ok = !Update.hasError();
  eventLogAdd(ok ? "OTA success" : "OTA failed");
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false}", ok ? 200 : 500);
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

static String portalPageHtml() {
  const uint32_t mask = runtimeConfigActionEnabledMask();

  String html;
  html.reserve(4096);
  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Fake Keyboard Setup</title>";
  html += "<style>";
  html += ":root{--bg:#0b1220;--card:#101b30;--text:#e8eefc;--muted:#9cb0d6;--acc:#4fd1c5;--danger:#ff6b6b;--br:14px}";
  html += "*{box-sizing:border-box}body{margin:0;background:linear-gradient(180deg,#070b14, var(--bg));color:var(--text);font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}";
  html += ".wrap{max-width:720px;margin:0 auto;padding:18px}h2{margin:8px 0 2px}p{margin:0 0 12px;color:var(--muted)}";
  html += ".card{background:rgba(16,27,48,.92);border:1px solid rgba(255,255,255,.06);border-radius:var(--br);padding:14px;margin:12px 0;backdrop-filter: blur(6px);transform: translateY(0);animation: pop .35s ease both}";
  html += "@keyframes pop{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}";
  html += "label{display:block;margin:.7rem 0 .2rem;color:var(--muted);font-size:14px}";
  html += "input,select{width:100%;padding:.6rem .7rem;border-radius:12px;border:1px solid rgba(255,255,255,.08);background:#0b1326;color:var(--text);outline:none}";
  html += "input:focus{border-color:rgba(79,209,197,.8);box-shadow:0 0 0 3px rgba(79,209,197,.18)}";
  html += ".row{display:flex;gap:10px}.row>div{flex:1}";
  html += "button{cursor:pointer;border:0;border-radius:12px;padding:.7rem 1rem;background:linear-gradient(135deg,#2dd4bf,#60a5fa);color:#06101f;font-weight:700;transition:transform .08s ease, filter .15s ease}";
  html += "button:active{transform:scale(.98)}button:hover{filter:brightness(1.05)}";
  html += ".chip{display:inline-flex;align-items:center;gap:8px;padding:8px 10px;border-radius:999px;background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.08);color:var(--muted);font-size:13px}";
  html += ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}";
  html += "@media(max-width:560px){.grid{grid-template-columns:1fr}.row{flex-direction:column}}";
  html += "hr{border:0;border-top:1px solid rgba(255,255,255,.08);margin:14px 0}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='wrap'>";
  html += "<h2>Fake Keyboard — Setup</h2>";
  html += "<p>Connect this device to your Wi‑Fi. Your phone should show a \"Sign in to Wi‑Fi\" prompt.</p>";
  html += "<div class='chip'>AP SSID: <b>" + apSsid + "</b> &nbsp; IP: <b>" + ipToString(WiFi.softAPIP()) + "</b></div>";

  html += "<div class='card'>";
  html += "<form method='POST' action='/provision'>";

  html += "<label>Wi-Fi SSID</label><input name='ssid' maxlength='32' required>";
  html += "<label>Wi-Fi Password</label><input name='pass' maxlength='64' type='password'>";

  html += "<label style='margin-top:12px'><input type='checkbox' name='wifiDisable'> Disable Wi‑Fi features (no AP/portal/web UI until factory reset)</label>";

  html += "<hr>";
  html += "<div><b>Intervals</b><div style='color:var(--muted);font-size:13px;margin-top:4px'>These ranges are per-profile (ms).</div></div>";
  html += "<div class='grid' style='margin-top:10px'>";
  for (uint8_t p = 0; p < PROFILE_COUNT; p++) {
    html += "<div>";
    html += "<div style='font-weight:700;margin:6px 0 4px'>" + String(profileName((Profile)p)) + "</div>";
    html += "<div class='row'>";
    html += "<div><label style='margin-top:0'>Min (s)</label><input name='p" + String(p) + "MinS' type='number' min='0' step='0.1' value='" + String(runtimeConfigProfileIntervalMinMs((Profile)p) / 1000.0, 2) + "'></div>";
    html += "<div><label style='margin-top:0'>Max (s)</label><input name='p" + String(p) + "MaxS' type='number' min='0' step='0.1' value='" + String(runtimeConfigProfileIntervalMaxMs((Profile)p) / 1000.0, 2) + "'></div>";
    html += "</div>";
    html += "</div>";
  }
  html += "</div>";

  html += "<hr>";
  html += "<div><b>Enabled actions</b></div>";
  for (int i = 0; i < actionsCount(); i++) {
    const bool en = (i < 32) ? ((mask & (1u << (uint32_t)i)) != 0) : true;
    html += "<label style='margin:.45rem 0'><input type='checkbox' name='act" + String(i) + "'" + (en ? " checked" : "") + "> " + String(actionName(i)) + "</label>";
  }

  html += "<hr>";
  html += "<button type='submit'>Save & Restart</button>";
  html += "<div style='margin-top:10px' class='chip'>Want to use the device UI without saving Wi‑Fi? <a href='/ui' style='color:var(--text)'>Open device UI</a></div>";
  html += "</form>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<div style='display:flex;align-items:center;justify-content:space-between;gap:10px'>";
  html += "<div><b>OTA Update</b><div style='color:var(--muted);font-size:13px;margin-top:4px'>Works even before joining your Wi‑Fi.</div></div>";
  html += "</div>";
  html += "<form method='POST' action='/api/ota' enctype='multipart/form-data' style='margin-top:10px'>";
  html += "<input type='file' name='firmware' required>";
  html += "<div style='margin-top:10px'><button type='submit'>Upload Firmware</button></div>";
  html += "</form>";
  html += "</div>";

  html += "</div></body></html>";
  return html;
}

static void handleProvisionPost() {
  const String ssid = server.arg("ssid");
  const String pass = server.arg("pass");

  const bool wifiDisable = server.hasArg("wifiDisable");

  for (uint8_t p = 0; p < PROFILE_COUNT; p++) {
    const String kMin = String("p") + String(p) + "MinMs";
    const String kMax = String("p") + String(p) + "MaxMs";
    const String kMinS = String("p") + String(p) + "MinS";
    const String kMaxS = String("p") + String(p) + "MaxS";
    const uint32_t curMin = runtimeConfigProfileIntervalMinMs((Profile)p);
    const uint32_t curMax = runtimeConfigProfileIntervalMaxMs((Profile)p);
    const uint32_t minMs = server.hasArg(kMinS) ? parseSecondsToMs(server.arg(kMinS), curMin)
                                                : parseU32(server.arg(kMin), curMin);
    const uint32_t maxMs = server.hasArg(kMaxS) ? parseSecondsToMs(server.arg(kMaxS), curMax)
                                                : parseU32(server.arg(kMax), curMax);
    runtimeConfigSetProfileIntervalsMs((Profile)p, minMs, maxMs);
  }

  uint32_t mask = 0;
  for (int i = 0; i < actionsCount() && i < 32; i++) {
    if (server.hasArg("act" + String(i))) mask |= (1u << (uint32_t)i);
  }

  if (wifiDisable) {
    runtimeConfigSetWifiDisabled(true);
    runtimeConfigSetWifiCredentials("", "");
    eventLogAdd("Wi-Fi disabled via setup");
  } else {
    runtimeConfigSetWifiDisabled(false);
    runtimeConfigSetWifiCredentials(ssid.c_str(), pass.c_str());
  }
  runtimeConfigSetActionEnabledMask(mask);
  runtimeConfigSetProvisioned(true);

  if (!wifiDisable) {
    eventLogAdd(String("Provisioned Wi-Fi SSID=") + ssid);
  }

  String body;
  body.reserve(1400);
  body += "<!doctype html><html><head><meta charset='utf-8'>";
  body += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  body += "<title>Saved</title>";
  body += "<style>body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#0b1326;color:#e8eefc;margin:0;padding:18px}a{color:#60a5fa}</style>";
  body += "</head><body>";
  body += "<h3>Saved. Restarting…</h3>";
  if (wifiDisable) {
    body += "<p>Wi‑Fi features were disabled. The device UI will be unavailable until factory reset.</p>";
  } else {
    body += "<p>The device will reboot and join your Wi‑Fi network. Your phone will likely disconnect from this setup AP.</p>";
    body += "<ol><li>Reconnect your phone to your Wi‑Fi: <b>" + ssid + "</b></li>";
    body += "<li>Open: <a href='http://" + String(mdnsHost) + ".local/'>http://" + String(mdnsHost) + ".local/</a></li></ol>";
    body += "<p class='muted'>If that doesn’t resolve, check your router/DHCP list for the device IP.</p>";
    body += "<script>setTimeout(()=>{try{location.href='http://" + String(mdnsHost) + ".local/';}catch(e){}},6000);</script>";
  }
  body += "</body></html>";
  server.send(200, "text/html", body);
  delay(800);
  ESP.restart();
}

static String companionAppHtml() {
  String html;
  html.reserve(28000);
  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Fake Keyboard</title>";
  html += "<style>";
  html += ":root{--bg:#060a12;--card:#0b1326;--card2:#0f1b33;--text:#e8eefc;--muted:#9cb0d6;--acc:#4fd1c5;--acc2:#60a5fa;--danger:#ff6b6b;--br:16px}";
  html += "*{box-sizing:border-box}body{margin:0;background:radial-gradient(1200px 600px at 20% -20%,rgba(96,165,250,.20),transparent 60%),radial-gradient(900px 520px at 90% 10%,rgba(79,209,197,.18),transparent 55%),linear-gradient(180deg,#05070d,var(--bg));color:var(--text);font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}";
  html += ".wrap{max-width:820px;margin:0 auto;padding:16px}h2{margin:8px 0 2px}p{margin:0 0 12px;color:var(--muted)}";
  html += ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}@media(max-width:760px){.grid{grid-template-columns:1fr}}";
  html += ".card{background:rgba(15,27,51,.92);border:1px solid rgba(255,255,255,.06);border-radius:var(--br);padding:14px;backdrop-filter: blur(6px);animation: in .35s ease both}";
  html += "@keyframes in{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}";
  html += ".title{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:8px}";
  html += ".badge{display:inline-flex;align-items:center;gap:8px;padding:7px 10px;border-radius:999px;background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.08);color:var(--muted);font-size:13px}";
  html += ".muted{color:var(--muted);font-size:13px}";
  html += "label{display:block;margin:.6rem 0 .2rem;color:var(--muted);font-size:13px}";
  html += "input,select{width:100%;padding:.62rem .7rem;border-radius:12px;border:1px solid rgba(255,255,255,.08);background:#070f22;color:var(--text);outline:none}";
  html += "input:focus,select:focus{border-color:rgba(79,209,197,.8);box-shadow:0 0 0 3px rgba(79,209,197,.18)}";
  html += ".row{display:flex;gap:10px}.row>div{flex:1}@media(max-width:560px){.row{flex-direction:column}}";
  html += "button{cursor:pointer;border:0;border-radius:12px;padding:.62rem .95rem;background:linear-gradient(135deg,var(--acc),var(--acc2));color:#04111f;font-weight:800;transition:transform .08s ease, filter .15s ease;will-change:transform}";
  html += "button:hover{filter:brightness(1.05)}button:active{transform:scale(.98)}button.secondary{background:rgba(255,255,255,.08);color:var(--text);border:1px solid rgba(255,255,255,.10)}";
  html += "button.danger{background:linear-gradient(135deg,#ff6b6b,#f59e0b);color:#1b0a0a}";
  html += "button:disabled{opacity:.5;cursor:not-allowed}"
          ".pulse{display:inline-block;width:8px;height:8px;border-radius:999px;background:var(--acc);box-shadow:0 0 0 0 rgba(79,209,197,.6);animation:pulse 1.4s infinite}";
  html += "@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(79,209,197,.55)}70%{box-shadow:0 0 0 10px rgba(79,209,197,0)}100%{box-shadow:0 0 0 0 rgba(79,209,197,0)}}";
  html += "table{width:100%;border-collapse:collapse}td,th{padding:7px;border-bottom:1px solid rgba(255,255,255,.06);text-align:left;font-size:13px}";
  html += "pre{white-space:pre-wrap;margin:0;font-size:12px;color:var(--muted)}";
  html += "details.card{padding:0}details.card>summary.title{cursor:pointer;list-style:none;padding:14px;margin:0}details.card>summary.title::-webkit-details-marker{display:none}details.card[open]>summary.title{border-bottom:1px solid rgba(255,255,255,.06)}details.card .cardBody{padding:14px}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='wrap'><h2>Fake Keyboard</h2><p>Phone companion UI</p><div id='app'></div>";

  // Vue template is hidden so bindings are never shown as raw text when CDN is blocked
  html += "<template id='vue-tmpl'>";
  html += "<div class='grid'>";
  html += "<div class='card'><div class='title'><b>Status</b><span class='badge'><span class='pulse'></span>{{status.wifi.mode}} • {{status.wifi.ip}}</span></div>";
  html += "<div class='muted'>SSID: {{status.wifi.ssid}}</div>";
  html += "<div class='muted'>FW: {{status.fwVersion}}<span v-if='status.mdns'> — mDNS: {{status.mdns}}.local</span></div>";
  html += "<div style='margin-top:8px'>State: <b>{{status.active?'ACTIVE':'IDLE'}}</b> — Profile: <b>{{status.profile}}</b> — Sleeping: <b>{{status.sleeping}}</b>";
  html += "<span v-if='status.sleeping' class='muted'> (ends in {{Math.round(status.sleepRemainingMs/1000)}}s)</span></div>";
  html += "<div style='margin-top:6px'>Actions: <b>{{status.actionCount}}</b> — Next in: <b>{{Math.round(status.nextInMs/1000)}}</b>s</div>";
  html += "</div>";

  html += "<div class='card'><div class='title'><b>Controls</b><span class='muted'>Live</span></div>";
  html += "<div class='row'>";
  html += "<div><button @click='setActive(!status.active)'>{{status.active?'Stop':'Start'}}</button></div>";
  html += "<div><button class='secondary' @click='wakeNow' :disabled='!status.sleeping'>Wake</button></div>";
  html += "</div>";
  html += "<label style='margin-top:10px' for='profileSelect'>Profile</label>";
  html += "<select id='profileSelect' v-model.number='selectedProfile' @change='setProfile(selectedProfile)'>";
  html += "<option v-for='p in cfg.profiles' :key='p.id' :value='p.id'>{{p.name}}</option>";
  html += "</select>";
  html += "<div class='row' style='margin-top:10px'>";
  html += "<div><label style='margin-top:0' for='sleepSeconds'>Sleep (s)</label><input id='sleepSeconds' name='sleepSeconds' type='number' min='0' step='0.1' :value='msToS(sleepMs)' @input='sleepMs=sToMs($event.target.value)'></div>";
  html += "<div style='display:flex;align-items:flex-end'><button class='secondary' @click='sleepNow'>Sleep</button></div>";
  html += "</div>";
  html += "<div class='row' style='margin-top:10px'>";
  html += "<div><button class='secondary' @click='reboot'>Reboot</button></div>";
  html += "<div><button class='danger' @click='factoryReset'>Factory reset</button></div>";
  html += "</div>";
  html += "</div>";

  html += "<div class='card'><div class='title'><b>Settings</b><span class='muted'>Intervals + actions</span></div>";
  html += "<label for='customText'>Custom text (TypeText action)</label><input id='customText' name='customText' v-model='cfg.customText' maxlength='128' placeholder='(optional)'>";
  html += "<div v-for='p in cfg.profiles' :key='p.id' style='margin:10px 0;padding:10px;border:1px solid rgba(255,255,255,.06);border-radius:14px;background:rgba(255,255,255,.03)'>";
  html += "<div style='font-weight:800'>{{p.name}}</div>";
  html += "<div class='row' style='margin-top:6px'>";
  html += "<div><label style='margin-top:0' :for='\"p_\"+p.id+\"_min\"'>Min (s)</label><input :id='\"p_\"+p.id+\"_min\"' :name='\"p\"+p.id+\"MinS\"' type='number' min='0' step='0.1' :value='msToS(p.minMs)' @input='p.minMs=sToMs($event.target.value)'></div>";
  html += "<div><label style='margin-top:0' :for='\"p_\"+p.id+\"_max\"'>Max (s)</label><input :id='\"p_\"+p.id+\"_max\"' :name='\"p\"+p.id+\"MaxS\"' type='number' min='0' step='0.1' :value='msToS(p.maxMs)' @input='p.maxMs=sToMs($event.target.value)'></div>";
  html += "</div></div>";
  html += "<div style='margin:10px 0'>";
  html += "<div class='muted' style='margin:6px 0 4px'>Weights (Active / Meeting)</div>";
  html += "<div v-for='a in cfg.actions' :key='a.id' style='display:flex;align-items:center;gap:10px;margin:6px 0'>";
  html += "<input type='checkbox' :id='\"act_\"+a.id' :name='\"act_\"+a.id' v-model='a.enabled'>";
  html += "<div style='flex:1'>{{a.name}}</div>";
  html += "<input type='number' min='0' max='255' step='1' style='width:86px' aria-label='Active weight' v-model.number='a.wA'>";
  html += "<input type='number' min='0' max='255' step='1' style='width:86px' aria-label='Meeting weight' v-model.number='a.wM'>";
  html += "</div></div>";
  html += "<button @click='saveConfig'>Save</button> <span class='muted' v-if='saveMsg'>{{saveMsg}}</span>";
  html += "</div>";

  html += "<details class='card'><summary class='title'><b>Run action</b><span class='muted'>Manual</span></summary><div class='cardBody'>";
  html += "<div v-for='a in cfg.actions' :key='a.id' style='display:flex;align-items:center;gap:10px;margin:8px 0'>";
  html += "<button @click='trigger(a)' :disabled='!a.enabled'>Run</button><div style='flex:1'>{{a.name}}</div>";
  html += "<div class='muted'>{{a.enabled?'':'disabled'}}</div>";
  html += "</div></div></details>";

  html += "<details class='card'><summary class='title'><b>History</b><span class='muted'>Recent</span></summary><div class='cardBody'>";
  html += "<div style='display:flex;justify-content:flex-end;margin-bottom:8px'><button class='secondary' @click='clearHistory'>Clear</button></div>";
  html += "<table><thead><tr><th>ms</th><th>Action</th><th>src</th></tr></thead>";
  html += "<tbody><tr v-for='h in history' :key='h.ms + h.name'>";
  html += "<td>{{h.ms}}</td><td>{{h.name}}</td><td>{{h.src==1?'manual':'auto'}}</td>";
  html += "</tr></tbody></table>";
  html += "</div></details>";

  html += "<details class='card'><summary class='title'><b>Logs</b><span class='muted'>In‑RAM</span></summary><div class='cardBody'>";
  html += "<pre v-for='(l,idx) in logs' :key='idx'>{{l}}</pre>";
  html += "</div></details>";

  html += "<details class='card'><summary class='title'><b>OTA Update</b><span class='muted'>Upload .bin</span></summary><div class='cardBody'>";
  html += "<div><input type='file' @change='fileChanged'></div>";
  html += "<div style='margin-top:10px'><button @click='uploadOta' :disabled='!otaFile'>Upload</button> <span class='muted' v-if='otaMsg'>{{otaMsg}}</span></div>";
  html += "</div></details>";

  html += "</div>";
  html += "</template>";

  // Vanilla-JS fallback template (used when Vue CDN is blocked)
  html += "<template id='fb-tmpl'>";
  html += "<div class='grid'>";
  html += "<div class='card'><div class='title'><b>Status</b><span class='badge'><span class='pulse'></span><span id='fb_wifi'></span></span></div>";
  html += "<div class='muted' id='fb_ssid'></div><div style='margin-top:8px' id='fb_state'></div><div style='margin-top:6px' id='fb_next'></div></div>";

  html += "<div class='card'><div class='title'><b>Controls</b><span class='muted'>Live</span></div>";
  html += "<div class='row'><div><button id='fb_start'></button></div><div><button class='secondary' id='fb_wake'>Wake</button></div></div>";
  html += "<label style='margin-top:10px'>Profile</label><select id='fb_profile'></select>";
  html += "<div class='row' style='margin-top:10px'><div><label style='margin-top:0'>Sleep (s)</label><input type='number' id='fb_sleeps' min='0' step='0.1' value='60'></div>";
  html += "<div style='display:flex;align-items:flex-end'><button class='secondary' id='fb_sleep'>Sleep</button></div></div>";
  html += "<div class='row' style='margin-top:10px'><div><button class='secondary' id='fb_reboot'>Reboot</button></div><div><button class='danger' id='fb_reset'>Factory reset</button></div></div></div>";

  html += "<div class='card'><div class='title'><b>Settings</b><span class='muted'>Intervals + actions</span></div>";
  html += "<label>Custom text (TypeText action)</label><input id='fb_custom' maxlength='128' placeholder='(optional)'>";
  html += "<div id='fb_profiles'></div><div id='fb_actions' style='margin:10px 0'></div>";
  html += "<button id='fb_save'>Save</button> <span class='muted' id='fb_savemsg'></span></div>";

  html += "<details class='card'><summary class='title'><b>Run action</b><span class='muted'>Manual</span></summary><div class='cardBody'><div id='fb_run'></div></div></details>";
  html += "<details class='card'><summary class='title'><b>History</b><span class='muted'>Recent</span></summary><div class='cardBody'>";
  html += "<div style='display:flex;justify-content:flex-end;margin-bottom:8px'><button class='secondary' id='fb_clearhist'>Clear</button></div>";
  html += "<table><thead><tr><th>ms</th><th>Action</th><th>src</th></tr></thead><tbody id='fb_hist'></tbody></table></div></details>";
  html += "<details class='card'><summary class='title'><b>Logs</b><span class='muted'>In‑RAM</span></summary><div class='cardBody'><div id='fb_logs'></div></div></details>";
  html += "<details class='card'><summary class='title'><b>OTA Update</b><span class='muted'>Upload .bin</span></summary><div class='cardBody'><div><input type='file' id='fb_ota'></div>";
  html += "<div style='margin-top:10px'><button id='fb_upload'>Upload</button> <span class='muted' id='fb_otamsg'></span></div></div></details>";
  html += "</div>";
  html += "</template>";

  // Dynamic Vue loader + no-internet fallback
  html += "<script>(function(){";
  html += "const root=document.getElementById('app');";
  html += "const vueTmplEl=document.getElementById('vue-tmpl');";
  html += "const fbTmplEl=document.getElementById('fb-tmpl');";
  html += "function loadVue(timeoutMs){return new Promise((res,rej)=>{if(window.Vue) return res();";
  html += "const s=document.createElement('script'); s.src='https://unpkg.com/vue@3/dist/vue.global.prod.js'; s.async=true;";
  html += "let done=false; const t=setTimeout(()=>{if(done) return; done=true; rej(new Error('timeout'));}, timeoutMs||1400);";
  html += "s.onload=()=>{if(done) return; done=true; clearTimeout(t); window.Vue?res():rej(new Error('noVue'));};";
  html += "s.onerror=()=>{if(done) return; done=true; clearTimeout(t); rej(new Error('loadFail'));};";
  html += "document.head.appendChild(s);});}";

  html += "function startVue(){const {createApp}=Vue;";
  html += "createApp({template: vueTmplEl.innerHTML,";
  html += "data(){return{status:{wifi:{}},cfg:{profiles:[],actions:[],actionMask:0,customText:''},history:[],logs:[],saveMsg:'',otaFile:null,otaMsg:'',sleepMs:60000,selectedProfile:0,errMsg:'',_timer:null,_refreshMs:2000,_refreshing:false,_failCount:0}},";
  html += "methods:{";
  html += "msToS(ms){const s=(Number(ms||0)/1000); return String(Math.round(s*100)/100);},";
  html += "sToMs(v){const s=parseFloat(v); if(!isFinite(s)) return 0; return Math.max(0, Math.round(s*1000));},";
  html += "async getJson(u){const r=await fetch(u,{cache:'no-store'}); if(!r.ok) throw new Error(u+' '+r.status); return r.json();},";
  html += "async loadConfig(){try{this.cfg=await this.getJson('/api/config');}catch(e){}},";
  html += "async refresh(){if(this._refreshing) return; this._refreshing=true;";
  html += "try{const rs=await Promise.all([this.getJson('/api/status'),this.getJson('/api/history'),this.getJson('/api/logs')]);";
  html += "this.status=rs[0]; this.history=rs[1]; this.logs=rs[2];";
  html += "if(!document.activeElement || document.activeElement.id !== 'profileSelect'){this.selectedProfile=(typeof this.status.profileId==='number')?this.status.profileId:0;} this.errMsg=''; this._failCount=0;}";
  html += "catch(e){this._failCount=(this._failCount||0)+1; this.errMsg=(e&&e.message)?e.message:String(e);}";
  html += "finally{this._refreshing=false; this.adjustTimer();}},";
  html += "adjustTimer(){let target=(this.status&&this.status.wifi&&this.status.wifi.mode==='STA')?500:2000; if(this._failCount>0) target=5000;";
  html += "if(target!==this._refreshMs){this._refreshMs=target; if(this._timer) clearInterval(this._timer); this._timer=setInterval(()=>this.refresh(), this._refreshMs);} },";
  html += "actionMask(){let m=0; for(const a of this.cfg.actions){if(a.enabled) m|=(1<<a.id);} return (m>>>0);},";
  html += "async saveConfig(){this.saveMsg='Saving...'; const p=new URLSearchParams();";
  html += "for(const prof of this.cfg.profiles){p.set('p'+prof.id+'MinMs',prof.minMs); p.set('p'+prof.id+'MaxMs',prof.maxMs);}";
  html += "p.set('actionMask',this.actionMask()); p.set('customText', this.cfg.customText||'');";
  html += "for(const a of this.cfg.actions){p.set('wA'+a.id, a.wA); p.set('wM'+a.id, a.wM);} ";
  html += "const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});";
  html += "this.saveMsg=r.ok?'Saved':'Error'; setTimeout(()=>this.saveMsg='',1500); await this.loadConfig();},";
  html += "async trigger(a){const p=new URLSearchParams(); p.set('id',a.id); await fetch('/api/trigger',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p}); await this.refresh();},";
  html += "async setActive(v){const p=new URLSearchParams(); p.set('active',v?'1':'0'); await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p}); await this.refresh();},";
  html += "async setProfile(id){const p=new URLSearchParams(); p.set('profile',id); await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p}); await this.refresh();},";
  html += "async sleepNow(){const p=new URLSearchParams(); p.set('sleepMs',this.sleepMs); await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p}); await this.refresh();},";
  html += "async wakeNow(){const p=new URLSearchParams(); p.set('wake','1'); await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p}); await this.refresh();},";
  html += "async clearHistory(){const p=new URLSearchParams(); p.set('clearHistory','1'); await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p}); await this.refresh();},";
  html += "async reboot(){this.otaMsg='Rebooting...'; const p=new URLSearchParams(); p.set('reboot','1'); await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});},";
  html += "async factoryReset(){this.otaMsg='Factory reset...'; const p=new URLSearchParams(); p.set('factoryReset','1'); await fetch('/api/control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});},";
  html += "fileChanged(e){this.otaFile=e.target.files[0]||null;},";
  html += "async uploadOta(){this.otaMsg='Uploading...'; const fd=new FormData(); fd.append('firmware',this.otaFile);";
  html += "const r=await fetch('/api/ota',{method:'POST',body:fd}); this.otaMsg=r.ok?'Uploaded (restarting)':'Upload failed';},";
  html += "},mounted(){this.loadConfig().then(()=>this.refresh()).then(()=>{this.adjustTimer(); this._timer=setInterval(()=>this.refresh(), this._refreshMs);});}";
  html += "}).mount(root);}";

  html += "function startFallback(){";
  html += "root.innerHTML = fbTmplEl ? fbTmplEl.innerHTML : '<div class=\\\"card\\\">Offline UI failed to load</div>';";

  html += "let cfg=null,status=null,history=[],logs=[]; let refreshMs=2000; let timer=null;";
  html += "const q=(id)=>document.getElementById(id);";
  html += "async function get(u){const r=await fetch(u,{cache:'no-store'}); if(!r.ok) throw new Error(u+' '+r.status); return r.json();}";
  html += "async function post(u,obj){const p=new URLSearchParams(); for(const k in obj)p.set(k,obj[k]); return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});}";
  html += "function maskFromCfg(){let m=0; (cfg.actions||[]).forEach(a=>{const cb=q('fb_act_'+a.id); if(cb && cb.checked) m|=(1<<a.id);}); return (m>>>0);}";
    html += "function buildCfg(){";
    html += "const profWrap=q('fb_profiles'); const actWrap=q('fb_actions'); const runWrap=q('fb_run');";
    html += "profWrap.innerHTML=''; actWrap.innerHTML=''; runWrap.innerHTML='';";
    html += "(cfg.profiles||[]).forEach(p=>{";
    html += "const box=document.createElement('div'); box.style.cssText='margin:10px 0;padding:10px;border:1px solid rgba(255,255,255,.06);border-radius:14px;background:rgba(255,255,255,.03)';";
    html += "const title=document.createElement('div'); title.style.fontWeight='800'; title.textContent=p.name;";
    html += "const row=document.createElement('div'); row.className='row'; row.style.marginTop='6px';";
    html += "const col1=document.createElement('div'); const l1=document.createElement('label'); l1.style.marginTop='0'; l1.textContent='Min (s)';";
    html += "const i1=document.createElement('input'); i1.type='number'; i1.min='0'; i1.step='0.1'; i1.id='fb_pmin_'+p.id; i1.value=Math.round(((p.minMs||0)/1000)*100)/100;";
    html += "col1.appendChild(l1); col1.appendChild(i1);";
    html += "const col2=document.createElement('div'); const l2=document.createElement('label'); l2.style.marginTop='0'; l2.textContent='Max (s)';";
    html += "const i2=document.createElement('input'); i2.type='number'; i2.min='0'; i2.step='0.1'; i2.id='fb_pmax_'+p.id; i2.value=Math.round(((p.maxMs||0)/1000)*100)/100;";
    html += "col2.appendChild(l2); col2.appendChild(i2);";
    html += "row.appendChild(col1); row.appendChild(col2);";
    html += "box.appendChild(title); box.appendChild(row); profWrap.appendChild(box);";
    html += "});";
    html += "(cfg.actions||[]).forEach(a=>{";
    html += "const wr=document.createElement('div'); wr.style.cssText='display:flex;align-items:center;gap:10px;margin:6px 0';";
    html += "const cb=document.createElement('input'); cb.type='checkbox'; cb.id='fb_act_'+a.id; cb.checked=!!a.enabled;";
    html += "const nm=document.createElement('div'); nm.style.flex='1'; nm.textContent=a.name;";
    html += "const wA=document.createElement('input'); wA.type='number'; wA.min='0'; wA.max='255'; wA.step='1'; wA.style.width='86px'; wA.id='fb_wA_'+a.id; wA.value=(a.wA==null?0:a.wA);";
    html += "const wM=document.createElement('input'); wM.type='number'; wM.min='0'; wM.max='255'; wM.step='1'; wM.style.width='86px'; wM.id='fb_wM_'+a.id; wM.value=(a.wM==null?0:a.wM);";
    html += "wr.appendChild(cb); wr.appendChild(nm); wr.appendChild(wA); wr.appendChild(wM); actWrap.appendChild(wr);";
    html += "const row=document.createElement('div'); row.style.cssText='display:flex;align-items:center;gap:10px;margin:8px 0';";
    html += "const btn=document.createElement('button'); btn.textContent='Run'; btn.disabled=!a.enabled; btn.dataset.id=a.id;";
    html += "const name=document.createElement('div'); name.style.flex='1'; name.textContent=a.name;";
    html += "const st=document.createElement('div'); st.className='muted'; st.textContent=a.enabled?'':'disabled';";
    html += "btn.addEventListener('click', async()=>{await post('/api/trigger',{id:btn.dataset.id}); await refresh();});";
    html += "row.appendChild(btn); row.appendChild(name); row.appendChild(st); runWrap.appendChild(row);";
    html += "});";
    html += "const sel=q('fb_profile'); sel.innerHTML=''; (cfg.profiles||[]).forEach(p=>{const o=document.createElement('option'); o.value=p.id; o.textContent=p.name; sel.appendChild(o);});";
    html += "q('fb_custom').value = (cfg.customText||'');";
    html += "}";

  html += "function render(){";
  html += "q('fb_wifi').textContent=(status.wifi?status.wifi.mode:'?')+' • '+(status.wifi?status.wifi.ip:'?');";
  html += "q('fb_ssid').textContent='SSID: '+(status.wifi?status.wifi.ssid:'')+' | FW: '+(status.fwVersion||'?')+(status.mdns?(' | mDNS: '+status.mdns+'.local'):'');";
  html += "let sleepTxt='Sleeping: '+status.sleeping; if(status.sleeping){sleepTxt+=' (ends in '+Math.round((status.sleepRemainingMs||0)/1000)+'s)';}";
  html += "q('fb_state').innerHTML='State: <b>'+(status.active?'ACTIVE':'IDLE')+'</b> — Profile: <b>'+status.profile+'</b> — '+sleepTxt;";
  html += "q('fb_next').innerHTML='Actions: <b>'+status.actionCount+'</b> — Next in: <b>'+Math.round((status.nextInMs||0)/1000)+'</b>s';";
  html += "q('fb_start').textContent = status.active ? 'Stop' : 'Start';";
  html += "q('fb_wake').disabled = !status.sleeping;";
  html += "if(!document.activeElement || document.activeElement.id !== 'fb_profile'){q('fb_profile').value = (typeof status.profileId==='number')?status.profileId:0;}";
  html += "q('fb_hist').innerHTML = (history||[]).map(h=>'<tr><td>'+h.ms+'</td><td>'+h.name+'</td><td>'+(h.src==1?'manual':'auto')+'</td></tr>').join('');";
  html += "q('fb_logs').innerHTML = (logs||[]).map(l=>'<pre>'+String(l).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))+'</pre>').join('');";
  html += "const target=(status.wifi&&status.wifi.mode==='STA')?500:2000; if(target!==refreshMs){refreshMs=target; if(timer) clearInterval(timer); timer=setInterval(refresh, refreshMs);}";
  html += "}";

  html += "let refreshing=false;";
  html += "async function loadConfig(){try{cfg=await get('/api/config'); buildCfg();}catch(e){}}";
  html += "async function refresh(){if(refreshing) return; refreshing=true;";
  html += "try{status=await get('/api/status'); history=await get('/api/history'); logs=await get('/api/logs'); render();}";
  html += "catch(e){q('fb_state').innerHTML='State: <b>ERROR</b> — '+String((e&&e.message)?e.message:e); if(refreshMs<5000){refreshMs=5000; if(timer) clearInterval(timer); timer=setInterval(refresh, refreshMs);} }";
  html += "finally{refreshing=false;}}";

  html += "q('fb_start').addEventListener('click',async()=>{await post('/api/control',{active: status.active?'0':'1'}); await refresh();});";
  html += "q('fb_profile').addEventListener('change',async(e)=>{await post('/api/control',{profile:e.target.value}); await refresh();});";
  html += "q('fb_sleep').addEventListener('click',async()=>{const s=parseFloat(q('fb_sleeps').value||'60'); const ms=Math.max(0, Math.round((isFinite(s)?s:60)*1000)); await post('/api/control',{sleepMs:ms}); await refresh();});";
  html += "q('fb_wake').addEventListener('click',async()=>{await post('/api/control',{wake:'1'}); await refresh();});";
  html += "q('fb_clearhist').addEventListener('click',async()=>{await post('/api/control',{clearHistory:'1'}); await refresh();});";
  html += "q('fb_reboot').addEventListener('click',async()=>{q('fb_otamsg').textContent='Rebooting...'; await post('/api/control',{reboot:'1'});});";
  html += "q('fb_reset').addEventListener('click',async()=>{q('fb_otamsg').textContent='Factory reset...'; await post('/api/control',{factoryReset:'1'});});";
  html += "q('fb_save').addEventListener('click',async()=>{q('fb_savemsg').textContent='Saving...'; const p=new URLSearchParams();";
  html += "(cfg.profiles||[]).forEach(pr=>{p.set('p'+pr.id+'MinS', q('fb_pmin_'+pr.id).value); p.set('p'+pr.id+'MaxS', q('fb_pmax_'+pr.id).value);});";
  html += "p.set('actionMask', maskFromCfg()); p.set('customText', q('fb_custom').value||'');";
  html += "(cfg.actions||[]).forEach(a=>{const wa=q('fb_wA_'+a.id); const wm=q('fb_wM_'+a.id); if(wa) p.set('wA'+a.id, wa.value); if(wm) p.set('wM'+a.id, wm.value);});";
  html += "const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});";
  html += "q('fb_savemsg').textContent=r.ok?'Saved':'Error'; setTimeout(()=>q('fb_savemsg').textContent='',1500); await loadConfig();});";
  html += "q('fb_upload').addEventListener('click',async()=>{const f=q('fb_ota').files[0]; if(!f){q('fb_otamsg').textContent='Select a .bin first'; return;} q('fb_otamsg').textContent='Uploading...';";
  html += "const fd=new FormData(); fd.append('firmware', f); const r=await fetch('/api/ota',{method:'POST',body:fd}); q('fb_otamsg').textContent=r.ok?'Uploaded (restarting)':'Upload failed';});";
  html += "loadConfig().then(()=>refresh()).then(()=>{ timer=setInterval(refresh, refreshMs); });";
  html += "}";

  html += "loadVue(1400).then(startVue).catch(startFallback);";
  html += "})();</script>";

  html += "</div></body></html>";
  return html;
}

static void setupRoutes() {
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/logs", HTTP_GET, handleApiLogs);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigPost);
  server.on("/api/history", HTTP_GET, handleApiHistory);
  server.on("/api/trigger", HTTP_POST, handleApiTrigger);
  server.on("/api/control", HTTP_POST, handleApiControl);

  server.on(
      "/api/ota", HTTP_POST, handleApiOta, handleOtaUpload);

  // Captive portal probes
  server.on("/generate_204", HTTP_GET, sendPortalForProbe);          // Android
  server.on("/gen_204", HTTP_GET, sendPortalForProbe);               // Android variants
  server.on("/redirect", HTTP_GET, sendPortalForProbe);              // Android variants
  server.on("/hotspot-detect.html", HTTP_GET, sendPortalForProbe);   // Apple
  server.on("/library/test/success.html", HTTP_GET, sendPortalForProbe); // Apple variants
  server.on("/canonical.html", HTTP_GET, sendPortalForProbe);
  server.on("/ncsi.txt", HTTP_GET, sendPortalForProbe);              // Windows
  server.on("/connecttest.txt", HTTP_GET, sendPortalForProbe);       // Windows
  server.on("/fwlink", HTTP_GET, sendPortalForProbe);                // Windows
  server.on("/success.txt", HTTP_GET, sendPortalForProbe);
  server.on("/wpad.dat", HTTP_GET, sendPortalForProbe);              // Some stacks request this
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204, "text/plain", ""); });

  if (apMode) {
    server.on("/", HTTP_GET, sendPortalHtml);
    server.on("/ui", HTTP_GET, []() { server.send(200, "text/html", companionAppHtml()); });
    server.on("/provision", HTTP_POST, handleProvisionPost);
    server.onNotFound([]() {
      // Serve the portal for any unknown path so OS captive-portal probes work.
      sendPortalHtml();
    });
  } else {
    server.on("/", HTTP_GET, []() { server.send(200, "text/html", companionAppHtml()); });
    server.on("/ui", HTTP_GET, []() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
    server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  }
}

static void startApPortal() {
  apMode = true;
  deviceStatusSet(DEV_NEEDS_SETUP);

  mdnsStop();

  const uint64_t mac = ESP.getEfuseMac();
  const uint16_t suffix = (uint16_t)(mac & 0xFFFFu);
  apSsid = String("KBSetup-") + String(suffix, HEX);
  apSsid.toUpperCase();

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  // Make the AP IP/gateway explicit; some phones behave better when this is stable.
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress apGw(192, 168, 4, 1);
  const IPAddress apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, apGw, apMask);
  WiFi.softAP(apSsid.c_str());
  delay(500);

  dns.start(53, "*", WiFi.softAPIP());

  server.stop();
  setupRoutes();
  server.begin();

  DEBUG_PRINT("[WIFI] AP captive portal SSID=");
  DEBUG_PRINT(apSsid);
  DEBUG_PRINT(" IP=");
  DEBUG_PRINTLN(ipToString(WiFi.softAPIP()));

  eventLogAdd(String("AP portal started SSID=") + apSsid + " IP=" + ipToString(WiFi.softAPIP()));
}

static bool startStaIfPossible() {
  const RuntimeConfig& cfg = runtimeConfigGet();
  if (!runtimeConfigHasWifiCredentials()) return false;

  staWanted = true;
  wifiSuppressedThisBoot = false;
  deviceStatusSet(DEV_WIFI_CONNECTING);

  apMode = false;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(mdnsHost);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);  // Force DHCP to send our hostname
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
  staAttempts = 1;
  nextStaAttemptAt = millis() + 60000UL;

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(200);
  }

  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("[WIFI] STA connect failed");
    eventLogAdd("STA connect failed");
    return false;
  }

  deviceStatusSet(DEV_OK);

  mdnsStart();

  // Best-effort NTP time sync for real timestamps in logs
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  dns.stop();

  server.stop();
  setupRoutes();
  server.begin();

  DEBUG_PRINT("[WIFI] STA connected SSID=");
  DEBUG_PRINT(WiFi.SSID());
  DEBUG_PRINT(" IP=");
  DEBUG_PRINTLN(ipToString(WiFi.localIP()));

  eventLogAdd(String("STA connected SSID=") + WiFi.SSID() + " IP=" + ipToString(WiFi.localIP()));
  return true;
}

static void staRetryLoop() {
  if (!staWanted) return;
  if (wifiSuppressedThisBoot) return;
  if (WiFi.status() == WL_CONNECTED) return;

  const unsigned long now = millis();
  if (now < nextStaAttemptAt) return;

  if (staAttempts >= 10) {
    // Give up for this boot
    eventLogAdd("STA failed 10 attempts; Wi-Fi OFF until next boot");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    wifiSuppressedThisBoot = true;
    staWanted = false;
    deviceStatusSet(DEV_WIFI_OFF);
    server.stop();
    dns.stop();
    return;
  }

  staAttempts++;
  nextStaAttemptAt = now + 60000UL;

  eventLogAdd(String("STA retry #") + String(staAttempts));
  deviceStatusSet(DEV_WIFI_CONNECTING);
  const RuntimeConfig& cfg = runtimeConfigGet();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(mdnsHost);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);  // Force DHCP to send our hostname
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);

  // Short wait to allow fast connects without blocking too long
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Bring up the UI now
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    mdnsStart();
    server.stop();
    setupRoutes();
    server.begin();
    deviceStatusSet(DEV_OK);
    eventLogAdd(String("STA connected SSID=") + WiFi.SSID() + " IP=" + ipToString(WiFi.localIP()));
  }
}

}  // namespace

void webPortalSetup() {
  if (!runtimeConfigBegin()) {
    DEBUG_PRINTLN("[CFG] runtimeConfigBegin failed");
    return;
  }

  if (runtimeConfigWifiDisabled()) {
    eventLogAdd("Wi-Fi disabled (skipping web portal)");
    deviceStatusSet(DEV_WIFI_DISABLED);
    return;
  }

  // If we have saved Wi-Fi creds, prefer STA-only behavior.
  // If STA isn't available, retry once per minute up to 10 attempts,
  // then turn Wi-Fi off for the rest of this boot.
  if (runtimeConfigHasWifiCredentials()) {
    deviceStatusSet(DEV_WIFI_CONNECTING);
    if (startStaIfPossible()) return;
    // Start retry loop; do NOT start AP portal.
    eventLogAdd("STA not connected; will retry every 60s (max 10)");
    return;
  }

  // First boot / no credentials: start AP captive portal
  deviceStatusSet(DEV_NEEDS_SETUP);
  startApPortal();
}

void webPortalLoop() {
  if (apMode) {
    dns.processNextRequest();
  }
  server.handleClient();

  staRetryLoop();
}
