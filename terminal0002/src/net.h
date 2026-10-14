// Wi-Fi: only the terminal's own access point, with a captive portal that
// opens the chat page. This terminal never joins a network: it has no
// internet, and phones on it get none either.
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

namespace net {
extern const IPAddress AP_IP;

void begin();
void restartAp();
}
