#include "site.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "certs.h"
#include "config.h"

namespace {

struct Report {
  site::Via via;
  bool plain;     // no from/to: older firmware's line
  char from[24];
  char to[24];
  char text[RADIO_LINE_MAX + 4];
};

struct Ack {
  uint32_t id;
  bool stored;  // false: shown here; true: relayed and seen back in a dump
};

const uint32_t POLL_MS = 10000;      // how often to ask for messages with nothing to report
const uint32_t ACK_HURRY_MS = 300;   // something to confirm: do it soon
const uint32_t RETRY_MS = 3000;      // the site did not answer
const uint32_t BAD_KEY_MS = 15000;   // the key was refused: ask less often until it is changed
const uint32_t FRESH_MS = 3 * POLL_MS;
const int ACK_MAX = 16;

QueueHandle_t reports;   // loop -> task: lines said on the channel
QueueHandle_t outgoing;  // task -> loop: messages from the site
QueueHandle_t acks;      // loop -> task: confirmations

volatile uint32_t lastOkMs = 0;
volatile int lastCode = 0;

// Confirmations wait here until the site has taken them: a lost request
// must not make the site hand the same message out again.
uint32_t shownIds[ACK_MAX], storedIds[ACK_MAX];
int shownCount = 0, storedCount = 0;

const char* viaName(site::Via v) {
  return v == site::SAT ? "sat" : v == site::AIR ? "air" : "here";
}

void collectAcks() {
  Ack a;
  while (shownCount < ACK_MAX && storedCount < ACK_MAX && xQueueReceive(acks, &a, 0) == pdTRUE) {
    if (a.stored) storedIds[storedCount++] = a.id;
    else shownIds[shownCount++] = a.id;
  }
}

void addIds(JsonDocument& req, const char* key, const uint32_t* ids, int n) {
  if (!n) return;
  JsonArray a = req[key].to<JsonArray>();
  for (int i = 0; i < n; i++) a.add(ids[i]);
}

// One exchange with the site. Returns the HTTP status, or a negative
// HTTPClient error.
int exchange(WiFiClientSecure& client, const Config& cfg, const Report* line) {
  JsonDocument req;
  req["terminal"] = cfg.terminal;
  if (line) {
    req["text"] = line->text;
    req["via"] = viaName(line->via);
    if (!line->plain) {
      if (line->via != site::HERE) req["from"] = line->from;
      req["to"] = line->to;
    }
  }
  addIds(req, "ack", shownIds, shownCount);
  addIds(req, "stored", storedIds, storedCount);
  String body;
  serializeJson(req, body);

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(true);
  if (!http.begin(client, cfg.url)) return HTTPC_ERROR_CONNECTION_REFUSED;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + cfg.key);
  int code = http.POST(body);
  String resp = code == 200 ? http.getString() : String();
  http.end();

  if (code != 200) return code;
  shownCount = storedCount = 0;

  JsonDocument doc;
  if (deserializeJson(doc, resp)) return code;
  for (JsonObject o : doc["out"].as<JsonArray>()) {
    site::Outgoing m{};
    m.id = o["id"] | 0;
    m.to = o["to"] | 0;
    strlcpy(m.from, o["from"] | "", sizeof m.from);
    strlcpy(m.text, o["text"] | "", sizeof m.text);
    // If the queue is full the lease runs out and the site sends it again.
    if (m.id && m.to && m.text[0]) xQueueSend(outgoing, &m, 0);
  }
  return code;
}

void task(void*) {
  WiFiClientSecure client;
  client.setCACert(SITE_ROOT_CA);
  Report line;
  uint32_t waitMs = POLL_MS;

  for (;;) {
    if (uxQueueMessagesWaiting(acks)) waitMs = min(waitMs, ACK_HURRY_MS);
    bool have = xQueueReceive(reports, &line, pdMS_TO_TICKS(waitMs)) == pdTRUE;
    waitMs = POLL_MS;

    Config cfg = config::get();
    if (!cfg.siteLink || !cfg.key[0]) continue;  // nowhere to send it
    if (WiFi.status() != WL_CONNECTED) {
      if (have) xQueueSendToFront(reports, &line, 0);  // report it once Wi-Fi is back
      // Sleep, not waitMs: with the line back in the queue the next receive
      // returns at once, and the task would spin on core 0 until the
      // watchdog restarts the board.
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    collectAcks();
    int code = exchange(client, cfg, have ? &line : nullptr);
    lastCode = code;
    if (code == 200) {
      lastOkMs = millis();
      if (uxQueueMessagesWaiting(reports)) waitMs = 0;
    } else if (code == 401 || code == 403) {
      Serial.printf("site: key refused (%d)\n", code);
      vTaskDelay(pdMS_TO_TICKS(BAD_KEY_MS));  // lines waiting in the queue would not let waitMs wait
    } else {
      Serial.printf("site: %d\n", code);
      // Network or server trouble: keep the line. A 4xx would fail the same way twice.
      if (have && (code < 0 || code >= 500)) xQueueSendToFront(reports, &line, 0);
      client.stop();
      vTaskDelay(pdMS_TO_TICKS(RETRY_MS));  // the line is back in the queue: waitMs would not wait
    }
  }
}

bool linked() {
  Config cfg = config::get();
  return cfg.siteLink && cfg.key[0];
}

void queueReport(const Report& r) {
  if (linked()) xQueueSend(reports, &r, 0);
}

}  // namespace

namespace site {

void begin() {
  reports = xQueueCreate(20, sizeof(Report));
  outgoing = xQueueCreate(8, sizeof(Outgoing));
  acks = xQueueCreate(32, sizeof(Ack));
  // TLS needs the stack.
  xTaskCreatePinnedToCore(task, "site", 12288, nullptr, 1, nullptr, 0);
}

void report(Via via, const String& from, const String& to, const String& text) {
  Report r{};
  r.via = via;
  strlcpy(r.from, from.c_str(), sizeof r.from);
  strlcpy(r.to, to.c_str(), sizeof r.to);
  strlcpy(r.text, text.c_str(), sizeof r.text);
  queueReport(r);
}

void reportPlain(const String& line) {
  Report r{};
  r.via = AIR;
  r.plain = true;
  strlcpy(r.text, line.c_str(), sizeof r.text);
  queueReport(r);
}

bool nextOutgoing(Outgoing& out) {
  return xQueueReceive(outgoing, &out, 0) == pdTRUE;
}

void shown(uint32_t id) {
  Ack a{id, false};
  xQueueSend(acks, &a, 0);
}

void stored(uint32_t id) {
  Ack a{id, true};
  xQueueSend(acks, &a, 0);
}

State state() {
  if (!linked()) return OFF;
  if (WiFi.status() != WL_CONNECTED) return NO_WIFI;
  if (lastOkMs && millis() - lastOkMs < FRESH_MS) return ONLINE;
  if (lastCode == 401 || lastCode == 403) return BAD_KEY;
  return lastCode ? TROUBLE : NO_WIFI;
}

}  // namespace site
