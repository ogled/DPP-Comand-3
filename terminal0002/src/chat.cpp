#include "chat.h"
#include <time.h>

namespace {
struct Line {
  uint32_t id;
  chat::Origin origin;
  uint32_t at;  // Unix time once the clock is set over Wi-Fi, else 0
  String from;
  String to;
  String text;
  uint32_t img;
};

const uint32_t KEEP = 60;
Line lines[KEEP];
uint32_t nextId = 1;

const char* originName(chat::Origin o) {
  switch (o) {
    case chat::HERE: return "here";
    case chat::SITE: return "site";
    case chat::SAT: return "sat";
    case chat::SYS: return "sys";
    default: return "air";
  }
}

uint32_t now() {
  time_t t = time(nullptr);
  return t > 1700000000 ? static_cast<uint32_t>(t) : 0;
}
}  // namespace

namespace chat {

void add(Origin origin, const String& from, const String& to, const String& text, uint32_t img) {
  lines[nextId % KEEP] = {nextId, origin, now(), from, to, text, img};
  nextId++;
}

bool contains(const String& text) {
  uint32_t first = nextId > KEEP ? nextId - KEEP : 1;
  for (uint32_t id = first; id < nextId; id++)
    if (lines[id % KEEP].text == text) return true;
  return false;
}

void since(uint32_t after, JsonArray out) {
  uint32_t first = nextId > KEEP ? nextId - KEEP : 1;
  if (after + 1 > first) first = after + 1;
  for (uint32_t id = first; id < nextId; id++) {
    const Line& l = lines[id % KEEP];
    JsonObject o = out.add<JsonObject>();
    o["id"] = l.id;
    o["o"] = originName(l.origin);
    o["at"] = l.at;
    o["f"] = l.from;
    o["to"] = l.to;
    o["t"] = l.text;
    if (l.img) o["img"] = l.img;
  }
}

}  // namespace chat
