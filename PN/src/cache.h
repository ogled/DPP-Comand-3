// Every line heard from the terminals, kept until a terminal confirms it
// caught a dump. Held in RAM and mirrored to flash (LittleFS), so a
// reset in orbit loses nothing.
//
// Ids grow forever, across resets and reboots: terminals use them to
// tell a line they already have from a new one.
#pragma once
#include <Arduino.h>
#include <vector>

#define CACHE_MAX_LINES 300
#define CACHE_MAX_BYTES (48 * 1024)

namespace cache {

struct Entry {
  uint32_t id;
  String text;
};

void begin();
// Stores a line. Returns false for a duplicate of a line already held.
bool add(const String& text);
// Drops every line with id <= last. Returns how many went.
size_t resetUpTo(uint32_t last);
const std::vector<Entry>& entries();
size_t bytes();

}  // namespace cache
