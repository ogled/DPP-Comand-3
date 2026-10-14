// The E32 LoRa module in transparent mode (M0 = M1 = 0): whatever goes in
// on its UART is broadcast, whatever it hears comes out. One message is
// one line of text, "nick: text", ending in '\n'.
//
// Only loop() calls into this, so a line can never be interleaved with
// another one on the UART.
#pragma once
#include <Arduino.h>

#if defined(RADIO_RX)
// pins set per board with -DRADIO_RX= -DRADIO_TX= -DRADIO_AUX= in platformio.ini
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
// ESP32-C3 SuperMini: clear of strapping pins 2/8/9 and of UART0 (20/21),
// which prints the boot log
#define RADIO_RX 3    // ESP32 RX  <- E32 TXD
#define RADIO_TX 4    // ESP32 TX  -> E32 RXD
#define RADIO_AUX 5   // E32 AUX: low while the module is busy
#else
// ESP32-S3, and the ESP32-C6 SuperMini wired the same way (1, 2 and 14 are
// clear of its strapping, USB and UART0 pins)
#define RADIO_RX 1    // ESP32 RX  <- E32 TXD
#define RADIO_TX 2   // ESP32 TX  -> E32 RXD
#define RADIO_AUX 14  // E32 AUX: low while the module is busy
#endif
#ifndef RADIO_UART_NUM
#define RADIO_UART_NUM 1  // which ESP32 UART talks to the E32 (1 = Serial1, 2 = Serial2)
#endif
#define RADIO_LINE_MAX 240   // bytes in one message; the site accepts the same
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
