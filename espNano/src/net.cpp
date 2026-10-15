#include "net.h"
#include <WiFi.h>
#include "config.h"

namespace net {

const IPAddress AP_IP(192, 168, 4, 1);

void restartAp() {
  Config c = config::get();
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(config::apName(c).c_str(), c.apPass[0] ? c.apPass : nullptr);
  Serial.printf("access point: %s\n", config::apName(c).c_str());
}

void begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  restartAp();
}

}  // namespace net
