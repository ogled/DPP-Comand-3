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
  Config c = config::get();
  char host[20];
  snprintf(host, sizeof host, "glebotron-%03u", c.terminal);
  WiFi.setHostname(host);
  WiFi.persistent(false);  // credentials live in our own settings
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  // The SuperMini's antenna is badly matched: at full power it often fails to join a network
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  restartAp();
  if (c.wifiSsid[0]) WiFi.begin(c.wifiSsid, c.wifiPass);
  // Clock for message times, once there is internet.
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
}

void connect(const String& ssid, const String& pass) {
  config::setWifi(ssid.c_str(), pass.c_str());
  WiFi.disconnect();
  WiFi.begin(ssid.c_str(), pass.c_str());
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

String ssid() { return config::get().wifiSsid; }

String ip() { return connected() ? WiFi.localIP().toString() : String(); }

bool scan(JsonArray out) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanNetworks(true);
    return false;
  }
  if (n == WIFI_SCAN_RUNNING) return false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i).isEmpty()) continue;
    JsonObject o = out.add<JsonObject>();
    o["s"] = WiFi.SSID(i);
    o["r"] = WiFi.RSSI(i);
    o["e"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  return true;
}

}  // namespace net
