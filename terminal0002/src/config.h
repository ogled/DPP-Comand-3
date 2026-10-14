// Settings, kept in flash (Preferences). Every task reads a copy through
// config::get(), so nothing holds a pointer into a struct that the
// settings page might be rewriting.
#pragma once
#include <Arduino.h>

#ifndef DEFAULT_TERMINAL
#define DEFAULT_TERMINAL 1  // per-board override: -DDEFAULT_TERMINAL=n in platformio.ini
#endif
#define DEFAULT_SITE_URL "https://cosmos1562.space/glebotron/api/terminal"
#define DEFAULT_PIN "TEST1234"

struct Config {
  uint16_t terminal;  // 1-999: this terminal's number in Glebotron
  bool siteLink;      // talk to the site whenever there is Wi-Fi
  char url[128];
  char key[65];       // server key from cosmos1562.space/glebotron, bound to `terminal`
  char apName[33];    // empty: Glebotron-NNN
  char apPass[65];    // empty: open access point
  char pin[17];       // empty: settings open to anyone on the access point
  char wifiSsid[33];
  char wifiPass[65];
};

namespace config {
void begin();
Config get();
void set(const Config& c);
void setWifi(const char* ssid, const char* pass);
void factoryReset();
String apName(const Config& c);
}
