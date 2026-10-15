#include "site.h"

// No internet on this terminal: see site.h.
namespace site {

void begin() {}
void report(Via, const String&, const String&, const String&) {}
void reportPlain(const String&) {}
bool nextOutgoing(Outgoing&) { return false; }
void shown(uint32_t) {}
void stored(uint32_t) {}
State state() { return OFF; }

}  // namespace site
