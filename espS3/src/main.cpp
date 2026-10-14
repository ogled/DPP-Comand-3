// Glebotron terminal, ESP32-S3 + E32 LoRa module.
//
// A local LoRa chat that phones join over the terminal's own Wi-Fi
// access point. When the terminal is also connected to a network with
// internet, it links its channel to cosmos1562.space/glebotron and does
// mission control's job: people on the site can write to it, anyone at
// the terminal can write to them with "@callsign text", and messages for
// terminals that are offline go up to the payload to carry.
//
// Every message on air is a MSG line that says who it is from and who
// it is for (satproto.h).
//
//   radio  - the E32 module, one line of text per message
//   msg    - MSG lines: from, to, text
//   satlink- the payload's dumps: what is for us, RESET CACHE, resending
//   camlink- /cam: a photo from the payload's camera
//   chat   - the channel log the page shows
//   site   - the link to the site, in its own task
//   net    - access point, captive portal, Wi-Fi client
//   web    - the page and its JSON API
//   config - settings in flash
//
// Hold BOOT for 5 s to wipe settings and saved Wi-Fi (a forgotten PIN).
#include <Arduino.h>
#include "camlink.h"
#include "chat.h"
#include "config.h"
#include "msg.h"
#include "net.h"
#include "radio.h"
#include "satlink.h"
#include "satproto.h"
#include "site.h"
#include "web.h"

#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define BOOT_BUTTON 9
#else
#define BOOT_BUTTON 0
#endif
#define FACTORY_RESET_MS 5000

namespace {
uint32_t bootPressedAt = 0;

String me() { return String(config::get().terminal); }

// "/cam" and the like: for the payload, not for the chat.
void command(const String& cmd) {
  if (cmd == "/cam") {
    camlink::request();
    chat::add(chat::SYS, "", "", "Запрошен снимок со спутника. Он придёт, когда спутник будет рядом.");
  } else {
    chat::add(chat::SYS, "", "", "Неизвестная команда " + cmd + ". Есть: /cam");
  }
}

// Typed on the page. "@anton hi" or "#3 hi" is for someone; anything else
// is for everyone in range. On air, into the log, up to the site, and -
// if it is for someone - kept until the payload shows it has it.
void typedHere(const String& nick, const String& typed) {
  String trimmed = typed;
  trimmed.trim();
  if (trimmed.startsWith("/")) return command(trimmed);
  String to, body;
  msg::splitTarget(typed, to, body);
  String text = radio::fit(nick.isEmpty() ? body : nick + ": " + body);
  if (text.isEmpty()) return;
  String wire = msg::build(me(), to, text);
  if (!radio::send(wire)) return;
  chat::add(chat::HERE, me(), to, text);
  site::report(site::HERE, me(), to, text);
  // For a terminal, the payload is the way there. For a person on the site
  // it is only needed while this terminal cannot reach the site itself.
  bool forTerminal = to != MSG_EVERYONE && isdigit(static_cast<unsigned char>(to[0]));
  bool forPerson = to != MSG_EVERYONE && !forTerminal;
  if (forTerminal || (forPerson && site::state() != site::ONLINE)) satlink::track(wire, 0);
}

// Heard on air. Payload protocol goes to satlink; a MSG line from another
// terminal is shown (it is in range, so it is part of the conversation
// here) and passed to the site; a plain line is from older firmware.
void heardOnAir(const String& line) {
  if (satlink::handle(line)) return;
  if (camlink::handle(line)) return;
  msg::Msg m;
  if (msg::parse(line, m)) {
    chat::add(chat::AIR, m.from, m.to, m.text);
    site::report(site::AIR, m.from, m.to, m.text);
  } else {
    chat::add(chat::AIR, "", "", line);
    site::reportPlain(line);
  }
}

// From the site: a message for this terminal goes on its page; a message
// for another terminal goes on air for the payload, and is kept until it
// comes back in a dump.
void fromSite() {
  site::Outgoing m;
  while (site::nextOutgoing(m)) {
    String from = m.from[0] == '#' ? String(m.from + 1) : String(m.from);  // "#5" -> "5"
    if (String(m.to) == me()) {
      chat::add(chat::SITE, from, me(), m.text);
      site::shown(m.id);
    } else {
      String wire = msg::build(from, String(m.to), m.text);
      if (radio::send(wire)) satlink::track(wire, m.id);
    }
  }
}

void checkFactoryReset() {
  if (digitalRead(BOOT_BUTTON) != LOW) {
    bootPressedAt = 0;
    return;
  }
  if (!bootPressedAt) bootPressedAt = millis();
  else if (millis() - bootPressedAt > FACTORY_RESET_MS) {
    Serial.println("factory reset");
    config::factoryReset();
    ESP.restart();
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  randomSeed(esp_random());
  config::begin();
  radio::begin();
  net::begin();
  site::begin();
  web::begin(typedHere);
  Serial.printf("terminal #%u ready\n", config::get().terminal);
}

void loop() {
  web::poll();
  radio::poll(heardOnAir);
  fromSite();
  satlink::poll();
  camlink::poll();
  checkFactoryReset();
  delay(2);
}
