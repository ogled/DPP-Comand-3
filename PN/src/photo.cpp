#include "photo.h"
#include <mbedtls/base64.h>
#include <vector>
#include "camera.h"
#include "radio.h"
#include "satproto.h"

namespace {

std::vector<uint8_t> jpeg;  // the last photo
uint32_t imgId = 0;
uint16_t width = 0, height = 0;
String forWhom;

bool wantPhoto = false;
String requestedBy;
std::vector<uint16_t> toSend;  // chunk numbers still to put on air
size_t sendNext = 0;
bool sending = false;

uint16_t chunks() { return (jpeg.size() + IMG_CHUNK - 1) / IMG_CHUNK; }

// "IMG|<img>|<seq>|<crc>|<base64>": under 58 bytes, one E32 packet.
String chunkLine(uint16_t seq) {
  size_t off = static_cast<size_t>(seq) * IMG_CHUNK;
  size_t len = min(static_cast<size_t>(IMG_CHUNK), jpeg.size() - off);
  unsigned char b64[IMG_CHUNK * 4 / 3 + 8];
  size_t outLen = 0;
  mbedtls_base64_encode(b64, sizeof b64, &outLen, jpeg.data() + off, len);
  char crc[3];
  snprintf(crc, sizeof crc, "%02x", imgCrc8(jpeg.data() + off, len));
  return String(IMG_PREFIX) + imgId + "|" + seq + "|" + crc + "|" +
         String(reinterpret_cast<char*>(b64)).substring(0, outLen);
}

void startSending(const std::vector<uint16_t>& seqs) {
  toSend = seqs;
  sendNext = 0;
  sending = true;
}

// "CMD|<from>|again|<img>|31-90,95,120-136"
void onAgain(const String& from, const String& rest) {
  int bar = rest.indexOf('|');
  if (bar < 0 || static_cast<uint32_t>(rest.substring(0, bar).toInt()) != imgId || jpeg.empty()) return;
  std::vector<uint16_t> seqs;
  String list = rest.substring(bar + 1);
  int start = 0;
  while (start < static_cast<int>(list.length()) && seqs.size() < chunks()) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    String item = list.substring(start, comma);
    int dash = item.indexOf('-');
    long a = item.toInt();
    long b = dash > 0 ? item.substring(dash + 1).toInt() : a;
    for (long seq = max(a, 0L); seq <= b && seq < chunks() && seqs.size() < chunks(); seq++) seqs.push_back(seq);
    start = comma + 1;
  }
  if (seqs.empty()) return;
  Serial.printf("photo %u: %u pieces again for #%s\n", imgId, (unsigned)seqs.size(), from.c_str());
  startSending(seqs);
}

}  // namespace

namespace photo {

bool handle(const String& line) {
  if (!line.startsWith(CMD_PREFIX)) return false;
  String rest = line.substring(strlen(CMD_PREFIX));
  int bar = rest.indexOf('|');
  if (bar <= 0) return true;
  String from = rest.substring(0, bar);
  String cmd = rest.substring(bar + 1);
  if (cmd == "cam") {
    // A repeat of the request this photo answers: the terminal just has
    // not heard it yet. Anything else is a new photo.
    if (!(from == forWhom && (sending || wantPhoto))) {
      wantPhoto = true;
      requestedBy = from;
      Serial.printf("photo requested by #%s\n", from.c_str());
    }
  } else if (cmd.startsWith("again|")) {
    onAgain(from, cmd.substring(6));
  }
  return true;
}

bool step(bool& ended) {
  ended = false;
  if (wantPhoto) {
    wantPhoto = false;
    std::vector<uint8_t> shot;
    if (!camera::capture(shot, width, height)) return false;
    jpeg.swap(shot);
    imgId = (imgId ? imgId : esp_random() % 1000) % IMG_ID_MAX + 1;  // new after a reboot too
    forWhom = requestedBy;
    radio::send(String(IMG_PREFIX) + imgId + "|BEGIN|" + forWhom + "|" + chunks() + "|" + jpeg.size() + "|" +
                width + "x" + height);
    std::vector<uint16_t> all(chunks());
    for (uint16_t i = 0; i < all.size(); i++) all[i] = i;
    startSending(all);
    return true;
  }
  if (!sending) return false;
  // One line per loop() pass, so incoming bytes are read in between.
  if (sendNext < toSend.size()) {
    radio::send(chunkLine(toSend[sendNext++]));
    return true;
  }
  radio::send(String(IMG_PREFIX) + imgId + "|END");
  sending = false;
  ended = true;
  return true;
}

}  // namespace photo
