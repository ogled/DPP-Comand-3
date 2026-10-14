// Wi-Fi: the terminal's own access point (always on, with a captive
// portal that opens the chat page), plus a connection to a network with
// internet when one is set up.
//
// The ESP32 has one radio, so once it joins a network the access point
// moves to that network's channel; phones on the access point drop for a
// moment and reconnect on their own.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>

namespace net {
extern const IPAddress AP_IP;

void begin();
void restartAp();
void connect(const String& ssid, const String& pass);
bool connected();
String ssid();
String ip();
// Fills out with the networks around and returns true, or returns false
// while a scan is still running (call again).
bool scan(JsonArray out);
}
