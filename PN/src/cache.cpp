#include "cache.h"
#include <LittleFS.h>
#include <Preferences.h>

namespace {
const char* FILE_PATH = "/cache.txt";  // one line per entry: "<id>\t<text>"
std::vector<cache::Entry> lines;
size_t totalBytes = 0;
uint32_t nextId = 1;
Preferences prefs;
bool fsOk = false;

void rewrite() {
  if (!fsOk) return;
  File f = LittleFS.open(FILE_PATH, "w");
  if (!f) return;
  for (const auto& e : lines) f.printf("%u\t%s\n", e.id, e.text.c_str());
  f.close();
}

void append(const cache::Entry& e) {
  if (!fsOk) return;
  File f = LittleFS.open(FILE_PATH, "a");
  if (!f) return;
  f.printf("%u\t%s\n", e.id, e.text.c_str());
  f.close();
}

void load() {
  if (!fsOk || !LittleFS.exists(FILE_PATH)) return;
  File f = LittleFS.open(FILE_PATH, "r");
  while (f.available()) {
    String row = f.readStringUntil('\n');
    int tab = row.indexOf('\t');
    if (tab <= 0) continue;
    uint32_t id = row.substring(0, tab).toInt();
    if (!id) continue;
    lines.push_back({id, row.substring(tab + 1)});
    totalBytes += row.length() - tab - 1;
    if (id >= nextId) nextId = id + 1;
  }
  f.close();
}

void dropOldest() {
  totalBytes -= lines.front().text.length();
  lines.erase(lines.begin());
}
}  // namespace

namespace cache {

void begin() {
  prefs.begin("pn", false);
  nextId = prefs.getUInt("next", 1);
  fsOk = LittleFS.begin(true);  // formats the partition on first boot
  if (!fsOk) Serial.println("cache: no flash filesystem, keeping lines in RAM only");
  load();
  prefs.putUInt("next", nextId);
  Serial.printf("cache: %u lines, next id %u\n", (unsigned)lines.size(), nextId);
}

bool add(const String& text) {
  for (const auto& e : lines)
    if (e.text == text) return false;  // heard twice, from two terminals or a repeat

  bool full = false;
  while (!lines.empty() &&
         (lines.size() >= CACHE_MAX_LINES || totalBytes + text.length() > CACHE_MAX_BYTES)) {
    dropOldest();
    full = true;
  }
  Entry e{nextId++, text};
  prefs.putUInt("next", nextId);
  lines.push_back(e);
  totalBytes += text.length();
  if (full) rewrite();
  else append(e);
  return true;
}

size_t resetUpTo(uint32_t last) {
  size_t before = lines.size();
  while (!lines.empty() && lines.front().id <= last) dropOldest();
  size_t dropped = before - lines.size();
  if (dropped) rewrite();
  return dropped;
}

const std::vector<Entry>& entries() { return lines; }

size_t bytes() { return totalBytes; }

}  // namespace cache
