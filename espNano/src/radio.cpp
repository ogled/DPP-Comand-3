#include "radio.h"
#include <deque>

namespace {
HardwareSerial& port = Serial1;
String rx;
uint32_t lastByte = 0;
const uint32_t LINE_IDLE_MS = 300;  // a line with no '\n' ends after this much silence
// No OK in this long: the Nano lost the line or restarted; go on with the next.
// It waits up to 1 s for AUX, and a full line takes ~0.35 s each way at 9600.
const uint32_t OK_TIMEOUT_MS = 3000;
const size_t QUEUE_MAX = 24;

std::deque<String> queue;  // lines for the air; front() is with the Nano while busy
bool busy = false;
uint32_t handedAt = 0;

void handOver() {
  if (busy || queue.empty()) return;
  const String& line = queue.front();
  Serial.printf("radio TX (%u B): %s\n", (unsigned)line.length(), line.c_str());
  port.print(line);
  port.print('\n');
  busy = true;
  handedAt = millis();
}

void done() {
  if (!busy) return;
  busy = false;
  queue.pop_front();
}

void finish(void (*onLine)(const String&), void (*onNote)(const String&)) {
  String line = radio::fit(rx, RADIO_FRAME_MAX);
  rx = "";
  if (line.isEmpty()) return;
  if (!line.startsWith("NANO|")) {
    Serial.printf("radio RX (%u B): %s\n", (unsigned)line.length(), line.c_str());
    onLine(line);
    return;
  }
  String rest = line.substring(5);
  if (rest == "OK") {
    done();
  } else if (rest == "READY") {
    // Whatever it was holding is gone: send it again.
    Serial.println("Nano restarted");
    busy = false;
  } else if (rest.startsWith("TX|")) {
    onLine(rest.substring(3));
  } else {
    onNote(rest);
  }
}
}  // namespace

namespace radio {

void begin() {
  port.setRxBufferSize(2048);
  port.begin(RADIO_BAUD, SERIAL_8N1, RADIO_RX, RADIO_TX);
}

String fit(const String& text, size_t max) {
  String s = text;
  s.replace("\r", " ");
  s.replace("\n", " ");
  s.trim();
  if (s.length() > max) {
    size_t cut = max;
    // step back over UTF-8 continuation bytes so a letter is not split in two
    while (cut > 0 && (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80) cut--;
    s = s.substring(0, cut);
    s.trim();
  }
  return s;
}

bool send(const String& text) {
  String line = fit(text, RADIO_FRAME_MAX);
  if (line.isEmpty()) return false;
  if (queue.size() >= QUEUE_MAX) {
    Serial.printf("radio TX dropped, queue full: %s\n", line.c_str());
    return false;
  }
  queue.push_back(line);
  handOver();
  return true;
}

void poll(void (*onLine)(const String&), void (*onNote)(const String&)) {
  while (port.available()) {
    char c = port.read();
    lastByte = millis();
    if (c == '\n') finish(onLine, onNote);
    // Control bytes are noise (the Nano or the E32 powering up, a garbled
    // packet), never text: one in the log would make the page's JSON invalid.
    else if (static_cast<uint8_t>(c) >= 0x20 && rx.length() < 2 * RADIO_FRAME_MAX) rx += c;
  }
  if (!rx.isEmpty() && millis() - lastByte > LINE_IDLE_MS) finish(onLine, onNote);
  if (busy && millis() - handedAt > OK_TIMEOUT_MS) {
    Serial.println("radio TX: no OK from the Nano, going on");
    done();
  }
  handOver();
}

}  // namespace radio
