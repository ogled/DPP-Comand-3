#include <Arduino.h>
#include <vector>
#include "cache.h"
#include "photo.h"
#include "radio.h"
#include "satproto.h"

namespace {

enum Phase { LISTENING, DUMPING };
Phase phase = LISTENING;
uint32_t listenUntil = 0;

std::vector<uint32_t> dumpIds;
size_t dumpNext = 0;
uint32_t lastDumped = 0; 

bool textOf(uint32_t id, String& out) {
  for (const auto& e : cache::entries())
    if (e.id == id) {
      out = e.text;
      return true;
    }
  return false;
}

void startDump() {
  dumpIds.clear();
  for (const auto& e : cache::entries()) dumpIds.push_back(e.id);
  dumpNext = 0;
  char head[48];
  snprintf(head, sizeof head, SAT_BEGIN "%u|%u|%u", dumpIds.front(), dumpIds.back(), (unsigned)dumpIds.size());
  radio::send(head);
  phase = DUMPING;
}

// One line per loop() pass, so incoming bytes are read in between.
void continueDump() {
  if (dumpNext < dumpIds.size()) {
    uint32_t id = dumpIds[dumpNext++];
    String text;
    if (textOf(id, text)) radio::send(String(SAT_PREFIX) + id + "|" + text);
    return;
  }
  uint32_t last = dumpIds.back();
  radio::send(String(SAT_END) + last);
  lastDumped = last;
  phase = LISTENING;
  listenUntil = millis() + SAT_LISTEN_MS;
}

void onReset(const String& line) {
  String arg = line.substring(strlen(SAT_RESET));
  arg.trim();
  uint32_t upTo = arg.isEmpty() ? lastDumped : static_cast<uint32_t>(arg.toInt());
  if (!upTo) return;
  size_t dropped = cache::resetUpTo(upTo);
  Serial.printf("RESET CACHE %u: dropped %u, %u left\n", upTo, (unsigned)dropped, (unsigned)cache::entries().size());
}

// Only messages addressed to someone are worth carrying: "MSG|from|to|text"
// with to other than "*".
bool addressed(const String& line) {
  if (!line.startsWith(MSG_PREFIX)) return false;
  int a = strlen(MSG_PREFIX), b = line.indexOf('|', a), c = b < 0 ? -1 : line.indexOf('|', b + 1);
  return b > a && c > b + 1 && line.substring(b + 1, c) != MSG_EVERYONE;
}

void onHeard(const String& line) {
  // A terminal is talking: keep the window open for the rest of them.
  if (phase == LISTENING) listenUntil = max(listenUntil, (uint32_t)(millis() + SAT_LISTEN_EXTEND_MS));
  if (line.startsWith(SAT_RESET)) return onReset(line);
  if (photo::handle(line)) return;
  if (!addressed(line)) return;
  // Leave room for "SAT|<id>|" so the dumped line still fits on air.
  if (cache::add(radio::fit(line, RADIO_FRAME_MAX - 16)))
    Serial.printf("stored #%u (%u lines, %u bytes): %s\n", cache::entries().back().id,
                  (unsigned)cache::entries().size(), (unsigned)cache::bytes(), line.c_str());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  radio::begin();
  cache::begin();
  listenUntil = millis() + SAT_LISTEN_MS;
  Serial.println("payload ready");
}

void loop() {
  radio::poll(onHeard);
  // A photo takes the radio from the dump; the dump starts over after it.
  bool ended = false;
  if (photo::step(ended)) {
    if (ended) {
      phase = LISTENING;
      listenUntil = millis() + SAT_LISTEN_MS;
    }
  } else if (phase == DUMPING) {
    continueDump();
  } else if (millis() >= listenUntil) {
    if (cache::entries().empty()) listenUntil = millis() + SAT_LISTEN_MS;  // nothing to dump: keep listening
    else startDump();
  }
  delay(2);
}
