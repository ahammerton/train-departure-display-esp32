#include "rail.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <string>
#include <vector>

#include "config.h"
#include "root_ca.h"

static const char *API_BASE =
    "https://api1.raildata.org.uk/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepBoardWithDetails/";

// Keep the parsed JSON in PSRAM rather than internal RAM
struct SpiRamAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
static SpiRamAllocator spiRam;

size_t lastReplyBytes = 0;

// Settings for the fetch in progress
static Settings cfg;

// ---- Text helpers, as in trains.py ------------------------------------------

using Strings = std::vector<std::string>;

static std::string removeBrackets(const char *name) {
  std::string s = name ? name : "";
  size_t p = s.find(" (");
  return p == std::string::npos ? s : s.substr(0, p);
}

static bool isTime(const char *v) {
  if (!v) return false;
  for (const char *p = v; p[0] && p[1] && p[2] && p[3] && p[4]; p++)
    if (isdigit(p[0]) && isdigit(p[1]) && p[2] == ':' && isdigit(p[3]) && isdigit(p[4])) return true;
  return false;
}

// "A, B and C"
static std::string joinWithCommas(const Strings &items) {
  std::string s;
  for (size_t i = 0; i < items.size(); i++) {
    if (i) s += ", ";
    s += items[i];
  }
  size_t last = s.rfind(',');
  if (last != std::string::npos) s.replace(last, 1, " and");
  return s;
}

// Join the non-empty items with single spaces
static std::string joinWithSpaces(const Strings &items) {
  std::string s;
  for (const auto &i : items) {
    if (i.empty()) continue;
    if (!s.empty()) s += " ";
    s += i;
  }
  return s;
}

static std::string serviceMessage(const char *op) {
  std::string o = op ? op : "";
  bool an = o == "Elizabeth Line" || o == "Avanti West Coast";
  return joinWithSpaces({an ? "An" : "A", o, "Service"});
}

static std::string carriagesMessage(int carriages) {
  if (carriages <= 0) return "";
  return "formed of " + std::to_string(carriages) + " coaches.";
}

static std::string locationName(JsonObjectConst point) {
  std::string name = removeBrackets(point["locationName"] | "");
  if (!cfg.individualTimes) return name;
  // As per the API docs, it's "at" if there isn't an "et"
  const char *expected = point["et"].is<const char *>() ? point["et"].as<const char *>() : (point["at"] | "");
  const char *t = isTime(expected) ? expected : (point["st"] | "");
  return name + " (" + t + ")";
}

static Strings callingPointNames(JsonArrayConst points) {
  Strings names;
  for (JsonObjectConst p : points) names.push_back(locationName(p));
  return names;
}

// Port of the calling_at_list logic in ProcessDepartures
static std::string buildCallingAt(JsonObjectConst svc, const std::string &destination) {
  std::string service = serviceMessage(svc["operator"] | "");
  std::string coaches = carriagesMessage(svc["length"] | 0);
  JsonArrayConst lists = svc["subsequentCallingPoints"];

  if (lists.isNull() || lists.size() == 0)
    return joinWithSpaces({destination, "only.", service, coaches});

  if (lists.size() > 1) {
    // The train divides: one list of calling points per portion
    std::string joined;
    for (size_t i = 0; i < lists.size(); i++) {
      Strings names = callingPointNames(lists[i]["callingPoint"]);
      if (i) joined += " with a portion going to ";
      joined += names.size() == 1 ? names[0] : joinWithCommas(names);
    }
    return joinWithSpaces({joined, "  --  ", service, coaches});
  }

  Strings names = callingPointNames(lists[0]["callingPoint"]);
  if (names.size() == 1) return joinWithSpaces({names[0], "only.", "  --  ", service, coaches});
  return joinWithSpaces({joinWithCommas(names) + ".", " --  ", service, coaches});
}

// Minutes past midnight, with times before 02:00 counted as the next day,
// so a 00:10 departure sorts after a 23:50 one (ArrivalOrder in trains.py)
static int sortMinutes(const char *std) {
  int h = atoi(std), m = atoi(std + 3);
  if (h < 2) h += 24;
  return h * 60 + m;
}

static void copyStr(char *dst, size_t size, const std::string &src) {
  strlcpy(dst, src.c_str(), size);
}

// ---- Fetching -----------------------------------------------------------------

static bool fetchOne(const char *filterCrs, JsonDocument &doc, String &err) {
  String url = String(API_BASE) + cfg.station + "?numRows=10&timeWindow=120&timeOffset=" + String(cfg.timeOffset);
  if (filterCrs && *filterCrs) url += String("&filterType=to&filterCrs=") + filterCrs;

  NetworkClientSecure client;
  client.setCACert(RAILDATA_ROOT_CA);
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    err = "HTTP begin failed";
    return false;
  }
  http.addHeader("x-apikey", cfg.apiKey);
  int code = http.GET();
  if (code != 200) {
    err = "HTTP " + String(code) + (code < 0 ? " " + HTTPClient::errorToString(code) : "");
    http.end();
    return false;
  }

  // Keep only the fields the display uses
  JsonDocument filter;
  filter["locationName"] = true;
  for (const char *kind : {"trainServices", "busServices"}) {
    JsonObject f = filter[kind][0].to<JsonObject>();
    for (const char *k : {"std", "etd", "platform", "operator", "length"}) f[k] = true;
    f["destination"][0]["locationName"] = true;
    JsonObject cp = f["subsequentCallingPoints"][0]["callingPoint"][0].to<JsonObject>();
    for (const char *k : {"locationName", "st", "et", "at"}) cp[k] = true;
  }

  // Read the whole reply first (it is 15-35 KB), then parse it. getString()
  // copes with chunked transfer encoding; parsing straight from the TLS
  // stream ended early.
  int expected = http.getSize();
  String body = http.getString();
  http.end();
  if (expected > 0 && (int)body.length() != expected) {
    err = "short read: " + String(body.length()) + " of " + String(expected) + " bytes";
    return false;
  }
  DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (e) {
    err = String("JSON: ") + e.c_str() + " (" + String(body.length()) + " bytes)";
    return false;
  }
  lastReplyBytes = body.length();
  return true;
}

struct Pending {
  int sortKey;
  std::string key;  // std + destination, to drop duplicates across filters
  Departure *dep;
};

bool fetchBoard(const Settings &settings, Board &out, String &err) {
  cfg = settings;
  if (!cfg.apiKey[0]) {
    err = "no API key set";
    return false;
  }
  // Destination filters: one request each, or one unfiltered request
  Strings filters;
  {
    std::string list = cfg.destinations, item;
    for (char c : list + ",") {
      if (c == ',') {
        if (!item.empty()) filters.push_back(item);
        item.clear();
      } else if (isalnum((unsigned char)c)) {
        item += (char)toupper(c);
      }
    }
    if (filters.empty()) filters.push_back("");
  }

  static Departure scratch[MAX_DEPARTURES];
  std::vector<Pending> found;
  out.count = 0;
  out.stationName[0] = 0;

  for (const auto &crs : filters) {
    JsonDocument doc(&spiRam);
    if (!fetchOne(crs.c_str(), doc, err)) return false;
    strlcpy(out.stationName, doc["locationName"] | cfg.station, sizeof out.stationName);

    for (const char *kind : {"trainServices", "busServices"}) {
      for (JsonObjectConst svc : doc[kind].as<JsonArrayConst>()) {
        if ((int)found.size() >= MAX_DEPARTURES) break;

        std::string dest;
        for (JsonObjectConst d : svc["destination"].as<JsonArrayConst>()) {
          if (!dest.empty()) dest += " & ";
          dest += removeBrackets(d["locationName"] | "");
        }
        std::string key = std::string(svc["std"] | "") + dest;
        bool seen = false;
        for (const auto &f : found) seen |= f.key == key;
        if (seen) continue;

        Departure &d = scratch[found.size()];
        strlcpy(d.std, svc["std"] | "", sizeof d.std);
        strlcpy(d.etd, svc["etd"] | "", sizeof d.etd);
        strlcpy(d.platform, svc["platform"] | "", sizeof d.platform);
        copyStr(d.destination, sizeof d.destination, dest);
        copyStr(d.callingAt, sizeof d.callingAt, buildCallingAt(svc, dest));
        found.push_back({sortMinutes(d.std), key, &d});
      }
    }
  }

  std::stable_sort(found.begin(), found.end(),
                   [](const Pending &a, const Pending &b) { return a.sortKey < b.sortKey; });
  for (const auto &f : found) out.deps[out.count++] = *f.dep;
  return true;
}
