// The terminal's side of the payload protocol (satproto.h):
//  - from a dump, shows only the lines addressed to this terminal, and
//    passes every line to the site when online;
//  - answers a complete dump with RESET CACHE, if the site is reachable;
//  - keeps every addressed line this terminal sent until it comes back in
//    a dump, sending it again in the payload's listening window.
#pragma once
#include <Arduino.h>

namespace satlink {
// Handles a line heard on air if it is payload protocol. Returns true if
// it was (the caller must not treat it as chat), false for an ordinary line.
bool handle(const String& line);
// Remember a MSG line this terminal put on air, until the payload has it.
// siteId: the site's message id when relaying for another terminal, else 0.
void track(const String& wire, uint32_t siteId);
// For loop(): answers the payload once the reply delay is up.
void poll();
}
