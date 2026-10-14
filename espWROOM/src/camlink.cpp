#include "camlink.h"
#include <mbedtls/base64.h>
#include <deque>
#include <vector>
#include "chat.h"
#include "config.h"
#include "radio.h"
#include "satproto.h"

namespace {

const uint32_t REQUEST_FOR_MS = 30UL * 60 * 1000;  // stop asking after half an hour
const uint8_t MAX_IDLE_ASKS = 12;     // requests for missing pieces that bring nothing new, before giving up
const uint32_t STALL_MS = 10000;     // no photo line for this long: ask again
const size_t KEEP_PHOTOS = 3;

String me() { return String(config::get().terminal); }

bool requesting = false;
uint32_t requestedAt = 0;

// The photo arriving.
struct Incoming {
  bool active = false;
  uint32_t id = 0;
  uint16_t total = 0;
  uint16_t got = 0;
  size_t bytes = 0;
  String size;
  uint8_t idleAsks = 0;
  uint16_t gotAtLastAsk = 0;
  std::vector<uint8_t> jpeg;  // filled in place as pieces arrive
  std::vector<bool> have;
} rx;
uint32_t againAt = 0;
uint32_t lastImgMs = 0;   // last photo line heard
uint32_t lastAskMs = 0;   // last request for missing pieces

bool missing() { return rx.active && rx.got < rx.total; }

// Missing pieces as "31-90,95,120-136", as many as fit in one radio packet.
void askAgain() {
  rx.idleAsks = rx.got > rx.gotAtLastAsk ? 0 : rx.idleAsks + 1;
  rx.gotAtLastAsk = rx.got;
  if (rx.idleAsks > MAX_IDLE_ASKS) {
    rx.active = false;
    chat::add(chat::SYS, "", "", "Снимок пришёл не целиком. Отправьте /cam ещё раз, когда спутник будет рядом.");
    return;
  }
  String head = String(CMD_PREFIX) + me() + "|again|" + rx.id + "|";
  String list;
  for (uint16_t i = 0; i < rx.total;) {
    if (rx.have[i]) {
      i++;
      continue;
    }
    uint16_t j = i;
    while (j + 1 < rx.total && !rx.have[j + 1]) j++;
    String item = j > i ? String(i) + "-" + j : String(i);
    if (head.length() + list.length() + (list.isEmpty() ? 0 : 1) + item.length() > LINE_ONE_PACKET) break;
    if (!list.isEmpty()) list += ',';
    list += item;
    i = j + 1;
  }
  if (list.isEmpty()) return;
  radio::send(head + list);
  lastAskMs = millis();
  Serial.printf("photo %u: %u of %u, asked again: %s\n", rx.id, rx.got, rx.total, list.c_str());
}

struct Photo {
  uint32_t id;
  std::vector<uint8_t> jpeg;
};
std::deque<Photo> photos;  // newest last

// Every piece was checked on arrival, so the photo is whole.
void finish() {
  rx.active = false;
  photos.push_back({rx.id, std::move(rx.jpeg)});
  if (photos.size() > KEEP_PHOTOS) photos.pop_front();
  chat::add(chat::SAT, "", me(), "Снимок со спутника, " + rx.size, rx.id);
  Serial.printf("photo %u: %u bytes\n", rx.id, (unsigned)rx.bytes);
}

// "IMG|<img>|BEGIN|<from>|<chunks>|<bytes>|<w>x<h>"
void onBegin(uint32_t id, const String& rest) {
  int a = rest.indexOf('|'), b = rest.indexOf('|', a + 1), c = rest.indexOf('|', b + 1);
  if (a < 0 || b < 0 || c < 0) return;
  if (rest.substring(0, a) != me()) return;  // someone else's photo
  if (rx.id == id && (rx.active || !requesting)) return;  // already have it, or arriving
  rx = Incoming();
  rx.active = true;
  rx.id = id;
  rx.total = rest.substring(a + 1, b).toInt();
  rx.bytes = rest.substring(b + 1, c).toInt();
  rx.size = rest.substring(c + 1);
  requesting = false;
  againAt = 0;
  if (!rx.total || rx.bytes > IMG_MAX_BYTES || rx.total != (rx.bytes + IMG_CHUNK - 1) / IMG_CHUNK) {
    rx.active = false;
    return;
  }
  rx.jpeg.assign(rx.bytes, 0);
  rx.have.assign(rx.total, false);
  lastImgMs = millis();
}

// "<crc>|<base64>" for piece seq. A piece that does not decode to the
// right length with the right CRC is dropped: it stays missing and is
// asked for again.
void onPiece(uint32_t id, uint16_t seq, const String& rest) {
  if (!rx.active || id != rx.id || seq >= rx.total || rx.have[seq]) return;
  int bar = rest.indexOf('|');
  if (bar != 2) return;
  uint8_t crc = strtoul(rest.substring(0, 2).c_str(), nullptr, 16);
  String b64 = rest.substring(3);
  size_t want = seq + 1 < rx.total ? IMG_CHUNK : rx.bytes - static_cast<size_t>(seq) * IMG_CHUNK;
  unsigned char buf[IMG_CHUNK + 4];
  size_t n = 0;
  if (mbedtls_base64_decode(buf, sizeof buf, &n, reinterpret_cast<const unsigned char*>(b64.c_str()),
                            b64.length()) != 0 ||
      n != want || imgCrc8(buf, n) != crc) {
    Serial.printf("photo %u: piece %u damaged, will ask again\n", id, seq);
    return;
  }
  memcpy(rx.jpeg.data() + static_cast<size_t>(seq) * IMG_CHUNK, buf, n);
  rx.have[seq] = true;
  if (++rx.got == rx.total) finish();
}

// "IMG|<img>|END": the payload is listening; ask for what is missing.
void onEnd(uint32_t id) {
  if (!missing() || id != rx.id) return;
  againAt = millis() + random(SAT_REPLY_MIN_MS, SAT_REPLY_MAX_MS);
}

}  // namespace

namespace camlink {

bool handle(const String& line) {
  if (line.startsWith(CMD_PREFIX)) return true;  // another terminal talking to the payload
  if (!line.startsWith(IMG_PREFIX)) return false;
  String rest = line.substring(strlen(IMG_PREFIX));
  int bar = rest.indexOf('|');
  if (bar <= 0) return true;
  uint32_t id = rest.substring(0, bar).toInt();
  if (id == rx.id) lastImgMs = millis();
  String tail = rest.substring(bar + 1);
  if (tail.startsWith("BEGIN|")) onBegin(id, tail.substring(6));
  else if (tail == "END") onEnd(id);
  else {
    int bar2 = tail.indexOf('|');
    if (bar2 > 0) onPiece(id, tail.substring(0, bar2).toInt(), tail.substring(bar2 + 1));
  }
  return true;
}

void request() {
  requesting = true;
  requestedAt = millis();
  radio::send(String(CMD_PREFIX) + me() + "|cam");
}

bool wantsWindow() {
  if (requesting && millis() - requestedAt > REQUEST_FOR_MS) {
    requesting = false;
    chat::add(chat::SYS, "", "", "Спутник не ответил на /cam за полчаса.");
  }
  return requesting || missing();
}

void inWindow() {
  if (requesting && wantsWindow()) radio::send(String(CMD_PREFIX) + me() + "|cam");
  else if (missing()) askAgain();
}

void poll() {
  if (againAt && millis() >= againAt) {
    againAt = 0;
    if (missing()) askAgain();
    return;
  }
  // Photo lines stopped and no END came (lost, or the payload went back to
  // its dump): ask again rather than wait forever.
  uint32_t now = millis();
  if (missing() && !againAt && now - lastImgMs > STALL_MS && now - lastAskMs > STALL_MS) askAgain();
}

bool progress(uint16_t& got, uint16_t& total) {
  if (!rx.active) return false;
  got = rx.got;
  total = rx.total;
  return true;
}

bool image(uint32_t id, const uint8_t*& data, size_t& len) {
  for (const Photo& p : photos)
    if (p.id == id) {
      data = p.jpeg.data();
      len = p.jpeg.size();
      return true;
    }
  return false;
}

}  // namespace camlink
