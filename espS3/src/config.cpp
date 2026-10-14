#include "config.h"
#include <Preferences.h>

// The key names match the earlier firmware, so a terminal keeps its
// number, Wi-Fi and PIN across the update.
namespace {
Preferences cfgPrefs;   // "cfg"
Preferences wifiPrefs;  // "wifi"
Config current;
SemaphoreHandle_t lock;

void copyPref(Preferences& p, const char* key, const char* fallback, char* dst, size_t size) {
  strlcpy(dst, p.getString(key, fallback).c_str(), size);
}

void load() {
  Config c{};
  c.terminal = cfgPrefs.getUShort("id", DEFAULT_TERMINAL);
  c.siteLink = cfgPrefs.getBool("api", true);
  copyPref(cfgPrefs, "url", DEFAULT_SITE_URL, c.url, sizeof c.url);
  copyPref(cfgPrefs, "tok", "", c.key, sizeof c.key);
  copyPref(cfgPrefs, "apn", "", c.apName, sizeof c.apName);
  copyPref(cfgPrefs, "app", "", c.apPass, sizeof c.apPass);
  copyPref(cfgPrefs, "pin", DEFAULT_PIN, c.pin, sizeof c.pin);
  copyPref(wifiPrefs, "ssid", "", c.wifiSsid, sizeof c.wifiSsid);
  copyPref(wifiPrefs, "pass", "", c.wifiPass, sizeof c.wifiPass);
  if (c.terminal < 1 || c.terminal > 999) c.terminal = DEFAULT_TERMINAL;

  xSemaphoreTake(lock, portMAX_DELAY);
  current = c;
  xSemaphoreGive(lock);
}
}  // namespace

namespace config {

void begin() {
  lock = xSemaphoreCreateMutex();
  cfgPrefs.begin("cfg", false);
  wifiPrefs.begin("wifi", false);
  load();
}

Config get() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Config c = current;
  xSemaphoreGive(lock);
  return c;
}

void set(const Config& c) {
  cfgPrefs.putUShort("id", c.terminal);
  cfgPrefs.putBool("api", c.siteLink);
  cfgPrefs.putString("url", c.url);
  cfgPrefs.putString("tok", c.key);
  cfgPrefs.putString("apn", c.apName);
  cfgPrefs.putString("app", c.apPass);
  cfgPrefs.putString("pin", c.pin);
  load();
}

void setWifi(const char* ssid, const char* pass) {
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  load();
}

void factoryReset() {
  cfgPrefs.clear();
  wifiPrefs.clear();
}

String apName(const Config& c) {
  if (c.apName[0]) return c.apName;
  char name[20];
  snprintf(name, sizeof name, "Glebotron-%03u", c.terminal);
  return name;
}

}  // namespace config
