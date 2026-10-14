#include "web.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "camlink.h"
#include "chat.h"
#include "config.h"
#include "net.h"
#include "page.h"
#include "radio.h"
#include "site.h"

namespace {
WebServer server(80);
DNSServer dns;
void (*sendLine)(const String& nick, const String& text) = nullptr;
uint32_t apRestartAt = 0;

void sendJson(int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

void fail(int code, const char* message) {
  server.send(code, "text/plain; charset=utf-8", message);
}

bool pinOk() {
  Config c = config::get();
  if (!c.pin[0] || server.arg("k") == c.pin) return true;
  fail(403, "pin");
  return false;
}

const char* siteState() {
  switch (site::state()) {
    case site::ONLINE: return "online";
    case site::BAD_KEY: return "key";
    case site::TROUBLE: return "trouble";
    case site::NO_WIFI: return "nowifi";
    default: return "off";
  }
}

void handleState() {
  Config c = config::get();
  JsonDocument doc;
  doc["terminal"] = c.terminal;
  doc["wifi"] = net::connected();
  doc["site"] = siteState();
  doc["pin"] = c.pin[0] != 0;
  uint16_t got, total;
  if (camlink::progress(got, total)) {
    doc["photo"]["got"] = got;
    doc["photo"]["total"] = total;
  }
  sendJson(200, doc);
}

// /api/image?id=N: a photo from the payload's camera.
void handleImage() {
  const uint8_t* data;
  size_t len;
  if (!camlink::image(server.arg("id").toInt(), data, len)) return fail(404, "Снимка уже нет на терминале");
  server.sendHeader("Cache-Control", "max-age=86400");
  server.send_P(200, "image/jpeg", reinterpret_cast<const char*>(data), len);
}

void handleLines() {
  JsonDocument doc;
  chat::since(server.arg("after").toInt(), doc.to<JsonArray>());
  sendJson(200, doc);
}

// n: the name the person goes by here (may be empty); t: what they typed.
void handleSend() {
  String nick = server.arg("n"), text = radio::fit(server.arg("t"));
  nick.replace("|", "");
  nick.replace(":", "");
  nick.trim();
  if (nick.length() > 24) nick = nick.substring(0, 24);
  if (text.isEmpty()) return fail(400, "empty");
  sendLine(nick, text);
  server.send(204);
}

void handleAdminInfo() {
  if (!pinOk()) return;
  Config c = config::get();
  JsonDocument doc;
  doc["terminal"] = c.terminal;
  doc["site"] = c.siteLink;
  doc["url"] = c.url;
  doc["key"] = c.key[0] != 0;  // whether one is set; the key itself never leaves the terminal
  doc["apName"] = c.apName;
  Config unnamed{};
  unnamed.terminal = c.terminal;
  doc["apDefault"] = config::apName(unnamed);
  doc["apOpen"] = c.apPass[0] == 0;
  doc["pin"] = c.pin[0] != 0;
  JsonObject w = doc["wifi"].to<JsonObject>();
  w["ssid"] = net::ssid();
  w["connected"] = net::connected();
  w["ip"] = net::ip();
  doc["siteState"] = siteState();
  sendJson(200, doc);
}

// Empty password, key and PIN fields keep what is set.
void handleAdminSave() {
  if (!pinOk()) return;
  Config c = config::get();
  Config before = c;

  long terminal = server.arg("terminal").toInt();
  String url = server.arg("url"), key = server.arg("key"), apName = server.arg("apName");
  String apPass = server.arg("apPass"), pin = server.arg("pin");
  url.trim();
  key.trim();
  apName.trim();

  if (terminal < 1 || terminal > 999) return fail(400, "Номер терминала: от 1 до 999");
  if (!url.startsWith("https://") || url.length() >= sizeof c.url) return fail(400, "Адрес сервера: https://…");
  if (key.length() >= sizeof c.key) return fail(400, "Ключ слишком длинный");
  if (apName.length() >= sizeof c.apName) return fail(400, "Имя точки доступа: до 32 символов");
  if (!apPass.isEmpty() && (apPass.length() < 8 || apPass.length() >= sizeof c.apPass))
    return fail(400, "Пароль точки доступа: от 8 до 64 символов");
  if (pin.length() >= sizeof c.pin) return fail(400, "PIN: до 16 символов");

  c.terminal = terminal;
  c.siteLink = server.arg("site") == "1";
  strlcpy(c.url, url.c_str(), sizeof c.url);
  if (!key.isEmpty()) strlcpy(c.key, key.c_str(), sizeof c.key);
  strlcpy(c.apName, apName.c_str(), sizeof c.apName);
  if (server.arg("apOpen") == "1") c.apPass[0] = 0;
  else if (!apPass.isEmpty()) strlcpy(c.apPass, apPass.c_str(), sizeof c.apPass);
  if (server.arg("apOpen") != "1" && !c.apPass[0]) return fail(400, "Пароль точки доступа: от 8 до 64 символов");
  if (server.arg("noPin") == "1") c.pin[0] = 0;
  else if (!pin.isEmpty()) strlcpy(c.pin, pin.c_str(), sizeof c.pin);

  config::set(c);
  bool apChanged = config::apName(c) != config::apName(before) || strcmp(c.apPass, before.apPass) != 0;
  server.send(200, "text/plain", apChanged ? "ap" : "ok");
  // The reply goes out first: restarting the access point drops the phone.
  if (apChanged) apRestartAt = millis() + 1000;
}

void handleScan() {
  JsonDocument doc;
  if (!net::scan(doc.to<JsonArray>())) {
    server.send(202, "text/plain", "scanning");
    return;
  }
  sendJson(200, doc);
}

void handleConnect() {
  if (!pinOk()) return;
  String ssid = server.arg("s"), pass = server.arg("p");
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 64) return fail(400, "bad");
  server.send(204);
  net::connect(ssid, pass);
}

// Any other address, including the phone's "is there internet?" check,
// lands on the chat page: that is what makes it open by itself.
void handleOther() {
  server.sendHeader("Location", String("http://") + net::AP_IP.toString() + "/", true);
  server.send(302, "text/plain", "");
}
}  // namespace

namespace web {

void begin(void (*onSend)(const String& nick, const String& text)) {
  sendLine = onSend;
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", net::AP_IP);

  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html; charset=utf-8", PAGE);
  });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/lines", HTTP_GET, handleLines);
  server.on("/api/image", HTTP_GET, handleImage);
  server.on("/api/send", HTTP_POST, handleSend);
  server.on("/api/admin/info", HTTP_POST, handleAdminInfo);
  server.on("/api/admin/save", HTTP_POST, handleAdminSave);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi/connect", HTTP_POST, handleConnect);
  server.onNotFound(handleOther);
  server.begin();
}

void poll() {
  dns.processNextRequest();
  server.handleClient();
  if (apRestartAt && millis() > apRestartAt) {
    apRestartAt = 0;
    net::restartAp();
  }
}

}  // namespace web
