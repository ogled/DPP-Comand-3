// The link to cosmos1562.space/glebotron. With Wi-Fi, the terminal does
// mission control's job: every MSG line it types, hears, or catches in a
// payload dump goes up to the site, and the site hands it messages for
// itself and for terminals that are offline (for the payload to carry).
//
//   POST {"terminal": 7, "from": 3, "to": "@anton", "text": "vasya: hi", "via": "sat",
//         "ack": [41], "stored": [44]}
//   <-   {"ok": true, "out": [{"id": 43, "from": "@anton", "to": 7, "text": "are you there?"}]}
//
// The HTTP work runs in its own task, so a slow request never stalls
// the radio or the page.
#pragma once
#include <Arduino.h>
#include "radio.h"

namespace site {

struct Outgoing {
  uint32_t id;
  char from[24];  // "@callsign" or "#7"
  uint16_t to;    // this terminal: show it; another: relay it through the payload
  char text[RADIO_LINE_MAX + 4];
};

enum State : uint8_t {
  OFF,      // switched off, or no server key
  NO_WIFI,  // not connected to a network
  ONLINE,   // the last exchange worked
  BAD_KEY,  // the site refused the key
  TROUBLE,  // Wi-Fi is up, the site is not answering
};

enum Via : uint8_t { HERE, AIR, SAT };

void begin();
// A MSG line: typed here, heard on air, or caught in a payload dump.
// from is ignored for HERE (it is this terminal).
void report(Via via, const String& from, const String& to, const String& text);
// A line without from/to, from older firmware on the channel.
void reportPlain(const String& line);
// For loop(): the next message from the site.
bool nextOutgoing(Outgoing& out);
// For loop(): a message for this terminal is on its page.
void shown(uint32_t id);
// For loop(): a message relayed for another terminal came back in a dump.
void stored(uint32_t id);
State state();

}  // namespace site
