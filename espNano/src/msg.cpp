#include "msg.h"
#include "satproto.h"

namespace msg {

bool parse(const String& line, Msg& m) {
  if (!line.startsWith(MSG_PREFIX)) return false;
  int a = strlen(MSG_PREFIX);
  int b = line.indexOf('|', a);
  int c = b < 0 ? -1 : line.indexOf('|', b + 1);
  if (b <= a || c <= b + 1) return false;
  m.from = line.substring(a, b);
  m.to = line.substring(b + 1, c);
  m.text = line.substring(c + 1);
  return !m.text.isEmpty();
}

String build(const String& from, const String& to, const String& text) {
  return String(MSG_PREFIX) + from + "|" + to + "|" + text;
}

namespace {
bool isCallsign(const String& s) {
  if (s.length() < 3 || s.length() > 20) return false;
  for (char ch : s)
    if (!isalnum(static_cast<unsigned char>(ch)) && ch != '_') return false;
  return true;
}

bool isNumber(const String& s) {
  if (s.isEmpty() || s.length() > 5) return false;
  for (char ch : s)
    if (!isdigit(static_cast<unsigned char>(ch))) return false;
  return true;
}
}  // namespace

void splitTarget(const String& typed, String& to, String& text) {
  String s = typed;
  s.trim();
  to = MSG_EVERYONE;
  text = s;
  int space = s.indexOf(' ');
  if (space < 2 || (s[0] != '@' && s[0] != '#')) return;
  String key = s.substring(1, space);
  String rest = s.substring(space + 1);
  rest.trim();
  if (rest.isEmpty()) return;
  if (s[0] == '@' && isCallsign(key)) {
    to = "@" + key;
  } else if (s[0] == '#' && isNumber(key)) {
    long n = key.toInt();
    if (n >= 1 && n <= 999) to = String(n);                // a terminal
    else if (n >= 1000 && n <= 65000) to = "#" + key;      // a person on the site
    else return;
  } else {
    return;
  }
  text = rest;
}

}  // namespace msg
