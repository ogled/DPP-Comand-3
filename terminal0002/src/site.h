// The link to cosmos1562.space/glebotron - which this terminal does not
// have: it has no internet. The interface is the one espS3/ has, so the
// rest of the code is the same; here the site is always OFF, nothing goes
// up and nothing comes down. Messages for people on the site go up through
// the payload instead (main.cpp tracks them while the site is not ONLINE).
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
