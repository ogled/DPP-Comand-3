// MSG lines (satproto.h): who a message is from and who it is for.
#pragma once
#include <Arduino.h>

namespace msg {

struct Msg {
  String from;  // "7", or "@callsign"
  String to;    // "*", "3", "@callsign" or "#48213"
  String text;
};

// "MSG|from|to|text" -> true and m filled; anything else -> false.
bool parse(const String& line, Msg& m);
String build(const String& from, const String& to, const String& text);

// What a person typed, split into who it is for and the message:
//   "@anton hi" -> "@anton", "hi"      "#3 hi" -> "3", "hi"
//   "#48213 hi" -> "#48213", "hi"      "hi"    -> "*", "hi"
void splitTarget(const String& typed, String& to, String& text);

}  // namespace msg
