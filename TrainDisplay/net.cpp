#include "net.h"

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include "app.h"
#include "secrets.h"
#include "settings.h"
#include "web_ui.h"

bool portalActive = false;
char apSsid[32] = "";

static WebServer server(80);
static DNSServer dnsServer;
static bool serverStarted = false, extrasStarted = false, everConnected = false;
static uint32_t portalStartMs = 0;
static uint32_t restartAtMs = 0;  // a restart scheduled after a reply has been sent

const uint32_t PORTAL_AFTER_MS = 60000;     // no Wi-Fi 60 s after boot -> setup hotspot
const uint32_t PORTAL_IDLE_MS = 300000;     // hotspot unused for 5 min -> restart, retry Wi-Fi
const uint32_t ROLLBACK_HEALTHY_S = 60;     // new firmware kept after a minute on the network
const uint32_t ROLLBACK_GIVE_UP_S = 600;    // ...or rolled back if it never gets there

bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

static void scheduleRestart(uint32_t delayMs) { restartAtMs = millis() + delayMs; }

// ---- Admin login ----------------------------------------------------------------
// The web page sends "X-TDD: 1" and its own Authorization header from an
// in-page login form. For those requests a failure is a plain 401 without
// WWW-Authenticate, so browsers don't show their own login pop-up (which
// captive-portal windows can't show at all). Anything changing settings must
// carry X-TDD, which a form on another website can't add. Five wrong passwords
// in 10 minutes lock that address out for 60 s, doubling up to 15 minutes.

struct LoginTrack {
  uint32_t ip = 0, firstFailS = 0, lockedUntilS = 0, lastS = 0;
  uint8_t fails = 0, lockouts = 0;
};
static LoginTrack logins[8];
static uint32_t lockWaitS = 0;

static uint32_t nowS() { return millis() / 1000; }

static LoginTrack *findLogin(uint32_t ip, bool create) {
  LoginTrack *oldest = &logins[0];
  for (auto &t : logins) {
    if (t.ip == ip) return &t;
    if (t.lastS < oldest->lastS) oldest = &t;
  }
  if (!create) return nullptr;
  *oldest = LoginTrack();
  oldest->ip = ip;
  return oldest;
}

enum AuthResult { AUTH_OK, AUTH_DENIED, AUTH_LOCKED };

static AuthResult checkAdmin() {
  uint32_t ip = (uint32_t)server.client().remoteIP();
  uint32_t now = nowS();
  LoginTrack *t = findLogin(ip, false);
  if (t && now < t->lockedUntilS) {
    lockWaitS = t->lockedUntilS - now;
    return AUTH_LOCKED;
  }
  Settings s = getSettings();
  if (server.authenticate("admin", s.adminPass)) {
    if (t) *t = LoginTrack();
    return AUTH_OK;
  }
  if (!server.hasHeader("Authorization")) return AUTH_DENIED;  // no attempt made
  t = findLogin(ip, true);
  t->lastS = now;
  if (t->fails == 0 || now - t->firstFailS >= 600) {
    t->fails = 0;
    t->firstFailS = now;
  }
  if (++t->fails < 5) return AUTH_DENIED;
  lockWaitS = min<uint32_t>(60u << min<uint8_t>(t->lockouts, 4), 900);
  t->lockedUntilS = now + lockWaitS;
  t->fails = 0;
  if (t->lockouts < 255) t->lockouts++;
  Serial.printf("Admin login locked for %lu s: %s\n", (unsigned long)lockWaitS, IPAddress(ip).toString().c_str());
  return AUTH_LOCKED;
}

static void sendAuthFailure(AuthResult r) {
  if (r == AUTH_LOCKED) {
    server.sendHeader("Retry-After", String(lockWaitS));
    server.send(429, "text/plain", "Too many wrong passwords. Try again in " + String(lockWaitS) + " s.");
  } else if (server.hasHeader("X-TDD")) {
    server.send(401, "text/plain", "Admin login required");
  } else {
    server.requestAuthentication(BASIC_AUTH, "Train Display", "Admin login required");
  }
}

static bool requireAdmin() {
  AuthResult r = checkAdmin();
  if (r == AUTH_OK) return true;
  sendAuthFailure(r);
  return false;
}

static bool requireAdminWrite() {
  if (!requireAdmin()) return false;
  if (server.header("X-TDD") != "1") {
    server.send(403, "text/plain", "Missing X-TDD header");
    return false;
  }
  return true;
}

static void sendJson(int code, const JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

// ---- Firmware state ---------------------------------------------------------------

// Overrides the core's weak default, so new firmware isn't accepted
// unconditionally at start-up: serviceRollback() decides.
extern "C" bool verifyRollbackLater() { return true; }

static const char *otaStateText() {
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(esp_ota_get_running_partition(), &st);
  switch (st) {
    case ESP_OTA_IMG_VALID: return "verified";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending health check";
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    default: return "flashed over USB";
  }
}

static void serviceRollback() {
  static bool done = false;
  if (done) return;
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) != ESP_OK ||
      st != ESP_OTA_IMG_PENDING_VERIFY) {
    done = true;
    return;
  }
  uint32_t up = nowS();
  if (up >= ROLLBACK_HEALTHY_S && (wifiUp() || portalActive)) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("Firmware " FW_VERSION " passed its health check");
    done = true;
  } else if (up >= ROLLBACK_GIVE_UP_S) {
    Serial.println("Firmware " FW_VERSION " never reached the network: rolling back");
    drawMessage("Update failed", "Rolling back");
    delay(500);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

// ---- Handlers ------------------------------------------------------------------------

static void handleStatus() {
  JsonDocument doc;
  doc["fw"] = FW_VERSION;
  doc["built"] = __DATE__ " " __TIME__;
  doc["ota"] = otaStateText();
  doc["rolled_back"] = esp_ota_get_last_invalid_partition() != nullptr;
  doc["mode"] = portalActive ? "setup" : "wifi";
  doc["ip"] = portalActive ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  doc["ssid"] = portalActive ? apSsid : WiFi.SSID();
  doc["rssi"] = wifiUp() ? WiFi.RSSI() : 0;
  doc["uptime"] = nowS();
  doc["heap"] = ESP.getFreeHeap();
  doc["psram"] = ESP.getFreePsram();
  doc["fps"] = roundf(displayFps * 10) / 10;
  doc["screens"] = getSettings().dualScreen ? 2 : 1;

  time_t t = time(nullptr);
  if (t > 1700000000) {
    char buf[24];
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &lt);
    doc["time"] = buf;
  }

  JsonObject f = doc["fetch"].to<JsonObject>();
  f["ok"] = fetchStatus.lastOk;
  f["ever_ok"] = fetchStatus.everOk;
  if (fetchStatus.everOk) f["age"] = (millis() - fetchStatus.lastOkMs) / 1000;
  f["error"] = fetchStatus.lastError;
  f["bytes"] = fetchStatus.replyBytes;
  f["ms"] = fetchStatus.tookMs;

  xSemaphoreTake(boardLock, portMAX_DELAY);
  const Board &b = *boards[frontBoard];
  doc["station"] = b.stationName;
  JsonArray deps = doc["departures"].to<JsonArray>();
  for (int i = 0; i < b.count && i < 5; i++) {
    JsonObject d = deps.add<JsonObject>();
    d["std"] = b.deps[i].std;
    d["etd"] = b.deps[i].etd;
    d["platform"] = b.deps[i].platform;
    d["destination"] = b.deps[i].destination;
  }
  xSemaphoreGive(boardLock);
  sendJson(200, doc);
}

static void handleGetSettings() {
  if (!requireAdmin()) return;
  Settings s = getSettings();
  JsonDocument doc;
  doc["wifiSsid"] = s.wifiSsid;
  doc["wifiPassSet"] = s.wifiPass[0] != 0;
  doc["apiKeySet"] = s.apiKey[0] != 0;
  doc["station"] = s.station;
  doc["destinations"] = s.destinations;
  doc["platform"] = s.platform;
  doc["numericPlatformsOnly"] = s.numericPlatformsOnly;
  doc["refreshTime"] = s.refreshTime;
  doc["timeOffset"] = s.timeOffset;
  doc["operatingHours"] = s.operatingHours;
  doc["outOfHoursName"] = s.outOfHoursName;
  doc["screenBlankHours"] = s.screenBlankHours;
  doc["individualTimes"] = s.individualTimes;
  doc["showNumbers"] = s.showNumbers;
  doc["firstBold"] = s.firstBold;
  doc["rotation"] = s.rotation;
  doc["contrast"] = s.contrast;
  doc["dualScreen"] = s.dualScreen;
  doc["platform2"] = s.platform2;
  doc["debug"] = s.debug;
  sendJson(200, doc);
}

static void copyField(JsonDocument &in, const char *key, char *dst, size_t size, bool upper = false) {
  if (!in[key].is<const char *>()) return;
  String v = in[key].as<const char *>();
  v.trim();
  if (upper) v.toUpperCase();
  v.replace(" ", upper ? "" : " ");
  strlcpy(dst, v.c_str(), size);
}

static void handlePostSettings() {
  if (!requireAdminWrite()) return;
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  Settings old = getSettings();
  Settings s = old;
  copyField(in, "wifiSsid", s.wifiSsid, sizeof s.wifiSsid);
  // Secrets are write-only: an empty value keeps the stored one
  if (in["wifiPass"].is<const char *>() && strlen(in["wifiPass"]))
    strlcpy(s.wifiPass, in["wifiPass"], sizeof s.wifiPass);
  if (in["wifiOpen"] == true) s.wifiPass[0] = 0;
  if (in["apiKey"].is<const char *>() && strlen(in["apiKey"])) copyField(in, "apiKey", s.apiKey, sizeof s.apiKey);
  if (in["adminPass"].is<const char *>() && strlen(in["adminPass"]))
    strlcpy(s.adminPass, in["adminPass"], sizeof s.adminPass);
  copyField(in, "station", s.station, sizeof s.station, true);
  copyField(in, "destinations", s.destinations, sizeof s.destinations, true);
  copyField(in, "platform", s.platform, sizeof s.platform, true);
  copyField(in, "platform2", s.platform2, sizeof s.platform2, true);
  if (in["dualScreen"].is<bool>()) s.dualScreen = in["dualScreen"];
  if (in["debug"].is<int>()) s.debug = constrain(in["debug"].as<int>(), 0, 65535);
  copyField(in, "operatingHours", s.operatingHours, sizeof s.operatingHours);
  copyField(in, "outOfHoursName", s.outOfHoursName, sizeof s.outOfHoursName);
  copyField(in, "screenBlankHours", s.screenBlankHours, sizeof s.screenBlankHours);
  if (in["numericPlatformsOnly"].is<bool>()) s.numericPlatformsOnly = in["numericPlatformsOnly"];
  if (in["individualTimes"].is<bool>()) s.individualTimes = in["individualTimes"];
  if (in["showNumbers"].is<bool>()) s.showNumbers = in["showNumbers"];
  if (in["firstBold"].is<bool>()) s.firstBold = in["firstBold"];
  if (in["refreshTime"].is<int>()) s.refreshTime = constrain(in["refreshTime"].as<int>(), 0, 65535);
  if (in["timeOffset"].is<int>()) s.timeOffset = constrain(in["timeOffset"].as<int>(), -32768, 32767);
  if (in["rotation"].is<int>()) s.rotation = constrain(in["rotation"].as<int>(), 0, 255);
  if (in["contrast"].is<int>()) s.contrast = constrain(in["contrast"].as<int>(), 0, 255);

  String err = applySettings(s);
  if (err.length()) {
    server.send(400, "text/plain", err);
    return;
  }
  bool wifiChanged = strcmp(old.wifiSsid, s.wifiSsid) || strcmp(old.wifiPass, s.wifiPass);
  // In setup mode, saving always restarts to try the network
  bool restart = wifiChanged || portalActive;
  JsonDocument out;
  out["ok"] = true;
  out["restart"] = restart;
  sendJson(200, out);
  Serial.println("Settings saved");
  if (restart) {
    drawMessage("Settings saved", "Restarting");
    scheduleRestart(1000);
  } else {
    requestRefresh();
  }
}

static void handleRefresh() {
  if (!requireAdminWrite()) return;
  requestRefresh();
  server.send(200, "text/plain", "OK");
}

static void handleDebug() {
  if (!requireAdminWrite()) return;
  showDebugFor(30000);
  server.send(200, "text/plain", "OK");
}

static void handleRestart() {
  if (!requireAdminWrite()) return;
  server.send(200, "text/plain", "OK");
  drawMessage("Restarting", "");
  scheduleRestart(500);
}

static void handleReset() {
  if (!requireAdminWrite()) return;
  resetSettings();
  server.send(200, "text/plain", "OK");
  drawMessage("Settings reset", "Restarting");
  scheduleRestart(1000);
}

// Browser firmware upload: streams the .bin into the spare OTA partition
static String fwError;
static AuthResult fwAuth = AUTH_DENIED;

static void handleFirmwareUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    fwError = "";
    fwAuth = checkAdmin();
    if (fwAuth != AUTH_OK) {
      fwError = "auth";
      return;
    }
    if (server.header("X-TDD") != "1") {
      fwError = "Missing X-TDD header";
      return;
    }
    if (!up.filename.endsWith(".bin")) {
      fwError = "Choose the firmware .bin file";
      return;
    }
    Serial.printf("Firmware upload started: %s\n", up.filename.c_str());
    drawUpdateProgress(0);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) fwError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (fwError.length()) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      fwError = Update.errorString();
      return;
    }
    size_t total = server.clientContentLength();
    if (total) drawUpdateProgress((int)min<uint64_t>(100, (uint64_t)up.totalSize * 100 / total));
  } else if (up.status == UPLOAD_FILE_END) {
    if (fwError.length()) {
      Update.abort();
      return;
    }
    if (!Update.end(true)) fwError = Update.errorString();  // also checks the image
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    fwError = "Upload interrupted";
  }
}

static void handleFirmwareDone() {
  if (fwError == "auth") {
    sendAuthFailure(fwAuth);
    return;
  }
  if (fwError.length()) {
    Serial.printf("Firmware upload failed: %s\n", fwError.c_str());
    drawMessage("Update failed", fwError.c_str());
    server.send(400, "text/plain", fwError);
    return;
  }
  Serial.println("Firmware uploaded, restarting");
  server.send(200, "text/plain", "OK");
  drawMessage("Update received", "Restarting");
  scheduleRestart(500);
}

static void handleNotFound() {
  // In setup mode, send every unknown URL to the settings page so phones and
  // laptops open their "sign in to network" window
  if (portalActive) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Not found");
}

static void startServer() {
  if (serverStarted) return;
  serverStarted = true;
  const char *headers[] = {"X-TDD"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", HTML_INDEX); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/restart", HTTP_POST, handleRestart);
  server.on("/api/debug", HTTP_POST, handleDebug);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.on("/api/firmware", HTTP_POST, handleFirmwareDone, handleFirmwareUpload);
  server.onNotFound(handleNotFound);
  server.begin();
}

// mDNS and Arduino IDE updates, once the network is first up
static void startExtras() {
  if (extrasStarted) return;
  extrasStarted = true;
  Settings s = getSettings();
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(s.adminPass);
  ArduinoOTA.onStart([]() {
    Serial.println("OTA update started");
    drawUpdateProgress(0);
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    if (total) drawUpdateProgress(done * 100 / total);
  });
  ArduinoOTA.onEnd([]() { drawMessage("Update received", "Restarting"); });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("OTA update failed (error %u)\n", (unsigned)e);
    drawMessage("Update failed", "");
  });
  ArduinoOTA.begin();  // also starts mDNS as train-display.local
  MDNS.addService("http", "tcp", 80);
}

static void startPortal() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof apSsid, "TrainDisplay-Setup-%02X%02X", mac[4], mac[5]);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, SETUP_AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());
  portalActive = true;
  portalStartMs = millis();
  startServer();
  Serial.printf("Setup hotspot \"%s\" at %s\n", apSsid, WiFi.softAPIP().toString().c_str());
}

void startNetwork() {
  Settings s = getSettings();
  if (!s.wifiSsid[0]) {
    startPortal();  // nothing saved yet: go straight to setup
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(s.wifiSsid, s.wifiPass);
}

void serviceNetwork() {
  if (restartAtMs && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
  serviceRollback();

  if (portalActive) {
    dnsServer.processNextRequest();
    server.handleClient();
    if (WiFi.softAPgetStationNum() > 0) portalStartMs = millis();  // in use: keep it open
    Settings s = getSettings();
    if (s.wifiSsid[0] && millis() - portalStartMs > PORTAL_IDLE_MS) ESP.restart();
    return;
  }

  static bool wasUp = false;
  bool up = wifiUp();
  if (up != wasUp) {
    wasUp = up;
    if (up) {
      Serial.printf("Wi-Fi connected: %s (%s.local)\n", WiFi.localIP().toString().c_str(), HOSTNAME);
      everConnected = true;
      startServer();
      startExtras();
    } else {
      Serial.println("Wi-Fi disconnected");
    }
  }

  if (!everConnected && millis() > PORTAL_AFTER_MS) {
    startPortal();
    return;
  }

  if (serverStarted) server.handleClient();
  if (extrasStarted) ArduinoOTA.handle();
}
