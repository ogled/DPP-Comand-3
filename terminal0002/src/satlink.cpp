#include "satlink.h"
#include <vector>
#include "camlink.h"
#include "chat.h"
#include "config.h"
#include "msg.h"
#include "radio.h"
#include "satproto.h"
#include "site.h"

namespace {

// The dump being received: which ids it promised, which arrived.
struct Dump {
  bool active = false;
  uint32_t first = 0, last = 0, count = 0;
  std::vector<uint32_t> got;
} dump;

// Ids already handled, so a line repeated in the next dump counts once.
const size_t SEEN_KEEP = 256;
uint32_t seen[SEEN_KEEP];
size_t seenNext = 0;

// Lines this terminal put on air that the payload has not shown it has.
struct Pending {
  String wire;
  uint32_t siteId;
  uint8_t tries;
  uint32_t since;
};
std::vector<Pending> pending;
const size_t PENDING_MAX = 16;
const uint8_t MAX_TRIES = 30;
const uint32_t MAX_AGE_MS = 48UL * 3600 * 1000;

uint32_t replyAt = 0;
bool resetDue = false;
uint32_t resetUpTo = 0;

bool wasSeen(uint32_t id) {
  for (uint32_t s : seen)
    if (s == id) return true;
  return false;
}

void markSeen(uint32_t id) { seen[seenNext++ % SEEN_KEEP] = id; }

// The payload has this line: stop sending it, and tell the site if it was a relay.
void confirm(const String& wire) {
  for (size_t i = 0; i < pending.size(); i++) {
    if (pending[i].wire != wire) continue;
    if (pending[i].siteId) site::stored(pending[i].siteId);
    pending.erase(pending.begin() + i);
    return;
  }
}

// "SAT|BEGIN|first|last|count"
void onBegin(const String& rest) {
  int a = rest.indexOf('|'), b = rest.indexOf('|', a + 1);
  if (a < 0 || b < 0) return;
  dump.active = true;
  dump.first = rest.substring(0, a).toInt();
  dump.last = rest.substring(a + 1, b).toInt();
  dump.count = rest.substring(b + 1).toInt();
  dump.got.clear();
}

// "SAT|END|last": the payload is listening now.
void onEnd(const String& rest) {
  uint32_t last = rest.toInt();
  bool complete = dump.active && last == dump.last && dump.got.size() == dump.count;
  dump.active = false;
  // Only a terminal linked to the site lets the payload forget: an offline
  // one shows what is for it and leaves the rest for someone who can pass it on.
  resetDue = complete && site::state() == site::ONLINE;
  resetUpTo = last;
  if (resetDue || !pending.empty() || camlink::wantsWindow())
    // Several terminals may hear the same dump: do not all answer at once.
    replyAt = millis() + random(SAT_REPLY_MIN_MS, SAT_REPLY_MAX_MS);
}

// "SAT|<id>|MSG|from|to|text"
void onItem(uint32_t id, const String& wire) {
  if (dump.active && id >= dump.first && id <= dump.last) {
    bool dup = false;
    for (uint32_t g : dump.got) dup |= g == id;
    if (!dup) dump.got.push_back(id);
  }
  confirm(wire);
  if (wasSeen(id)) return;
  markSeen(id);

  msg::Msg m;
  if (!msg::parse(wire, m)) return;
  // Everything goes to the site (it files each line once, however many
  // terminals catch it); only what is for this terminal goes on its page.
  site::report(site::SAT, m.from, m.to, m.text);
  if (m.to == String(config::get().terminal) && !chat::contains(m.text))
    chat::add(chat::SAT, m.from, m.to, m.text);
}

void expire() {
  uint32_t now = millis();
  for (size_t i = 0; i < pending.size();) {
    if (pending[i].tries >= MAX_TRIES || now - pending[i].since > MAX_AGE_MS) pending.erase(pending.begin() + i);
    else i++;
  }
}

}  // namespace

namespace satlink {

bool handle(const String& line) {
  if (line.startsWith(SAT_RESET)) return true;  // another terminal answering the payload
  if (!line.startsWith(SAT_PREFIX)) return false;

  if (line.startsWith(SAT_BEGIN)) onBegin(line.substring(strlen(SAT_BEGIN)));
  else if (line.startsWith(SAT_END)) onEnd(line.substring(strlen(SAT_END)));
  else {
    String rest = line.substring(strlen(SAT_PREFIX));
    int bar = rest.indexOf('|');
    uint32_t id = bar > 0 ? rest.substring(0, bar).toInt() : 0;
    if (id) onItem(id, rest.substring(bar + 1));
  }
  return true;
}

void track(const String& wire, uint32_t siteId) {
  for (const auto& p : pending)
    if (p.wire == wire) return;
  if (pending.size() >= PENDING_MAX) pending.erase(pending.begin());
  pending.push_back({wire, siteId, 0, millis()});
}

void poll() {
  if (!replyAt || millis() < replyAt) return;
  replyAt = 0;
  if (resetDue) {
    radio::send(String(SAT_RESET) + " " + resetUpTo);
    Serial.printf("caught a dump: RESET CACHE %u\n", resetUpTo);
    resetDue = false;
  }
  expire();
  int sent = 0;
  for (auto& p : pending) {
    if (sent >= SAT_RESEND) break;
    radio::send(p.wire);
    p.tries++;
    sent++;
  }
  if (sent) Serial.printf("sent %d line(s) again, %u waiting for the payload\n", sent, (unsigned)pending.size());
  camlink::inWindow();
}

}  // namespace satlink
