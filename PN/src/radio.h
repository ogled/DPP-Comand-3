// The E32 LoRa module in transparent mode (M0 = M1 = 0): whatever goes in
// on its UART is broadcast, whatever it hears comes out. One message is
// one line of text, "nick: text", ending in '\n'.
//
// Only loop() calls into this, so a line can never be interleaved with
// another one on the UART. Same module and pins as the terminals (espS3).
#pragma once
#include <Arduino.h>

#define RADIO_RX 1    // ESP32 RX  <- E32 TXD
#define RADIO_TX 2    // ESP32 TX  -> E32 RXD
#define RADIO_AUX 14  // E32 AUX: low while the module is busy
#define RADIO_LINE_MAX 240   // bytes in one message, as the terminals send
#define RADIO_FRAME_MAX 320  // bytes in one line on air: "SAT|id|MSG|from|to|" plus a message

namespace radio {
void begin();
// Reads what the module heard; calls onLine for every complete line.
void poll(void (*onLine)(const String& line));
// Puts one line on air (up to RADIO_FRAME_MAX bytes). Returns false for an empty line.
bool send(const String& line);
// One line, at most `max` bytes, cut on a character boundary.
String fit(const String& text, size_t max = RADIO_LINE_MAX);
}
