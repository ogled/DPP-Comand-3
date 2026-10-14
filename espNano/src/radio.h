// The radio is the Nano's (nano/): it keeps the E32 and talks to this board
// over its hardware Serial (D0/D1, 9600). One message is one line, ending
// in '\n'. Lines from the Nano are what it heard on air, or its own, which
// start "NANO|" (nano/src/main.cpp):
//   NANO|READY       it (re)started
//   NANO|OK          the line we gave it is with the E32; it takes the next one
//   NANO|TX|<line>   a line it put on air itself (its sensor report)
//   NANO|<note>      anything else
// It holds one line at a time, so send() queues lines and poll() hands them
// over one by one, each after the Nano's OK.
//
// Only loop() calls into this.
#pragma once
#include <Arduino.h>

#define RADIO_RX 17  // ESP32 RX <- Nano D1 (TX), through a 10k/20k divider: the Nano is 5 V
#define RADIO_TX 18  // ESP32 TX -> Nano D0 (RX)
#define RADIO_BAUD 9600
#define RADIO_LINE_MAX 240   // bytes in one message; the site accepts the same
#define RADIO_FRAME_MAX 320  // bytes in one line on air: "SAT|id|MSG|from|to|" plus a message; the Nano holds as much

namespace radio {
void begin();
// Reads what the Nano passed on. onLine: every line heard on air, and the
// lines the Nano put on air itself. onNote: the Nano's notes ("BME280 ok").
void poll(void (*onLine)(const String& line), void (*onNote)(const String& note));
// Queues one line for the air (up to RADIO_FRAME_MAX bytes). Returns false
// for an empty line or a full queue.
bool send(const String& line);
// One line, at most `max` bytes, cut on a character boundary.
String fit(const String& text, size_t max = RADIO_LINE_MAX);
}
