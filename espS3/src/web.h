// The page phones see when they join the terminal's access point, its
// small JSON API, and the captive portal that opens it.
//
//   GET  /                     the chat page
//   GET  /api/state            terminal number, Wi-Fi, link to the site
//   GET  /api/lines?after=N    the channel log
//   GET  /api/image?id=N       a photo from the payload's camera
//   POST /api/send             n=<name>&t=<text>
//   POST /api/admin/info       k=<pin>                     settings
//   POST /api/admin/save       k=<pin>&...                 change settings
//   GET  /api/wifi/scan                                    networks around
//   POST /api/wifi/connect     k=<pin>&s=<ssid>&p=<pass>
#pragma once
#include <Arduino.h>

namespace web {
// onSend is called for every message typed on the page.
void begin(void (*onSend)(const String& nick, const String& text));
void poll();
}
