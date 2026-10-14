// /cam: a photo from the payload's camera (satproto.h).
//
// The request goes out at once and again in every listening window until
// the payload starts answering. The photo arrives as IMG lines; pieces
// that did not arrive are asked for again after IMG END. Only a photo
// this terminal asked for is kept and shown. The last few are kept for
// the page.
#pragma once
#include <Arduino.h>

namespace camlink {
// A line heard on air: true if it was camera traffic (IMG or another
// terminal's CMD), which the caller must not treat as chat.
bool handle(const String& line);
// "/cam" typed on the page.
void request();
// Whether a request is still waiting for the payload.
bool wantsWindow();
// The payload is listening (after SAT|END): send the request again.
void inWindow();
// For loop(): asks for missing pieces once the reply delay is up.
void poll();
// A photo arriving: pieces so far, and of how many.
bool progress(uint16_t& got, uint16_t& total);
// A received photo, by id.
bool image(uint32_t id, const uint8_t*& data, size_t& len);
}
