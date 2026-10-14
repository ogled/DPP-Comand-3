// /cam: a photo for the terminal that asked, sent as IMG lines
// (satproto.h). The last photo is kept so missing pieces can be sent
// again when a terminal asks.
#pragma once
#include <Arduino.h>

namespace photo {
// A line heard on air: true if it was a command for the camera.
bool handle(const String& line);
// For loop(): takes the photo, or puts the next line of it on air.
// Returns true while it has the radio; false when there is nothing to do.
// `ended` is set when an IMG END has just gone out (time to listen).
bool step(bool& ended);
}
