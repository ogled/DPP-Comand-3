// The terminal's own log of the channel: what was typed here, what was
// heard on air, what came in from the site, and what the payload dumped
// for this terminal. The page polls it.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace chat {
enum Origin : uint8_t { HERE, AIR, SITE, SAT, SYS };  // SYS: a note from the terminal itself

// from: "7" or "@callsign" ("" if unknown); to: "*", "3", "@callsign", "#48213" ("" if unknown).
// img: a photo from the payload's camera (camlink), 0 for none.
void add(Origin origin, const String& from, const String& to, const String& text, uint32_t img = 0);
// Whether this exact text is already in the log.
bool contains(const String& text);
// Lines with id > after, oldest first.
void since(uint32_t after, JsonArray out);
}
