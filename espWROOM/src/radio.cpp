#include "radio.h"

namespace {
#if RADIO_UART_NUM == 2
HardwareSerial& port = Serial2;
#else
HardwareSerial& port = Serial1;
#endif
String rx;
uint32_t lastByte = 0;
const uint32_t LINE_IDLE_MS = 300;  // a line with no '\n' ends after this much silence

void waitIdle(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (digitalRead(RADIO_AUX) == LOW && millis() - start < timeoutMs) delay(1);
}

void finish(void (*onLine)(const String&)) {
  String line = radio::fit(rx, RADIO_FRAME_MAX);
  rx = "";
  if (!line.isEmpty()) {
    Serial.printf("radio RX (%u B): %s\n", (unsigned)line.length(), line.c_str());
    onLine(line);
  }
}
}  // namespace

namespace radio {

void begin() {
  pinMode(RADIO_AUX, INPUT_PULLUP);
  // Room for several lines: bytes keep arriving while send() waits for AUX.
  port.setRxBufferSize(2048);
  port.begin(9600, SERIAL_8N1, RADIO_RX, RADIO_TX);
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
  // A full line (~250 bytes) takes the module close to a second on air;
  // writing the next one before it is done would overrun its buffer.
  uint32_t t0 = millis();
  waitIdle(3000);
  Serial.printf("radio TX (%u B, AUX %s%s): %s\n", (unsigned)line.length(), digitalRead(RADIO_AUX) ? "high" : "LOW",
                millis() - t0 >= 3000 ? ", waited 3 s for it" : "", line.c_str());
  port.print(line);
  port.print('\n');
  port.flush();
  return true;
}

void poll(void (*onLine)(const String&)) {
  while (port.available()) {
    char c = port.read();
    lastByte = millis();
    if (c == '\n') finish(onLine);
    else if (c != '\r' && rx.length() < 2 * RADIO_FRAME_MAX) rx += c;
  }
  if (!rx.isEmpty() && millis() - lastByte > LINE_IDLE_MS) finish(onLine);
}

}  // namespace radio
