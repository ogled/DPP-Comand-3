#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <mbedtls/base64.h>
#include <vector>
#include "esp_camera.h"

#define RADIO_RX 1
#define RADIO_TX 2
#define RADIO_AUX 14
#define FRAME_MAX 320
#define LINE_IDLE_MS 300

#define LISTEN_MS 4000
#define LISTEN_EXTEND_MS 1500

#define CACHE_MAX_LINES 300
#define CACHE_MAX_BYTES (48 * 1024)
#define CACHE_FILE "/cache.txt"

#define IMG_CHUNK 30
#define IMG_ID_MAX 9999
#define IMG_MAX_BYTES 4096
#define SETTLE_MS 2500

#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5
#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

const int QUALITY_STEPS[] = {10, 11, 12, 13, 14, 15, 16, 18, 20, 23, 26, 30, 35, 45, 63};

void log(const String &value)
{
  Serial.println(value);
}

String rx;
uint32_t lastByteMs = 0;

String fit(const String &text, size_t max)
{
  String s = text;
  s.replace("\r", " ");
  s.replace("\n", " ");
  s.trim();
  if (s.length() > max)
  {
    size_t cut = max;
    while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80)
    {
      cut--;
    }
    s = s.substring(0, cut);
    s.trim();
  }
  return s;
}

void radioSend(const String &text)
{
  String line = fit(text, FRAME_MAX);
  if (line.isEmpty())
  {
    return;
  }
  uint32_t start = millis();
  while (digitalRead(RADIO_AUX) == LOW && millis() - start < 3000)
  {
    delay(1);
  }
  Serial1.print(line);
  Serial1.print('\n');
  Serial1.flush();
}

struct Entry
{
  uint32_t id;
  String text;
};
std::vector<Entry> cache;
size_t cacheBytes = 0;
uint32_t nextId = 1;
Preferences prefs;
bool fsOk = false;

void cacheRewrite()
{
  if (!fsOk)
  {
    return;
  }
  File f = LittleFS.open(CACHE_FILE, "w");
  if (!f)
  {
    return;
  }
  for (const Entry &e : cache)
  {
    f.printf("%u\t%s\n", e.id, e.text.c_str());
  }
  f.close();
}

void cacheDropOldest()
{
  cacheBytes -= cache.front().text.length();
  cache.erase(cache.begin());
}

void cacheBegin()
{
  prefs.begin("pn", false);
  nextId = prefs.getUInt("next", 1);
  fsOk = LittleFS.begin(true);
  if (!fsOk)
  {
    log("[ERROR] Cache: no flash filesystem, keeping lines in RAM only");
  }
  if (fsOk && LittleFS.exists(CACHE_FILE))
  {
    File f = LittleFS.open(CACHE_FILE, "r");
    while (f.available())
    {
      String row = f.readStringUntil('\n');
      int tab = row.indexOf('\t');
      if (tab <= 0)
      {
        continue;
      }
      uint32_t id = row.substring(0, tab).toInt();
      if (!id)
      {
        continue;
      }
      cache.push_back({id, row.substring(tab + 1)});
      cacheBytes += row.length() - tab - 1;
      if (id >= nextId)
      {
        nextId = id + 1;
      }
    }
    f.close();
  }
  prefs.putUInt("next", nextId);
  log("[OK] Cache: " + String(cache.size()) + " lines, next id " + nextId);
}

bool cacheAdd(const String &text)
{
  for (const Entry &e : cache)
  {
    if (e.text == text)
    {
      return false;
    }
  }
  bool full = false;
  while (!cache.empty() && (cache.size() >= CACHE_MAX_LINES || cacheBytes + text.length() > CACHE_MAX_BYTES))
  {
    cacheDropOldest();
    full = true;
  }
  Entry e{nextId++, text};
  prefs.putUInt("next", nextId);
  cache.push_back(e);
  cacheBytes += text.length();
  if (full)
  {
    cacheRewrite();
  }
  else if (fsOk)
  {
    File f = LittleFS.open(CACHE_FILE, "a");
    if (f)
    {
      f.printf("%u\t%s\n", e.id, e.text.c_str());
      f.close();
    }
  }
  return true;
}

bool cameraStart()
{
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_HQVGA;
  c.jpeg_quality = QUALITY_STEPS[0];
  c.fb_count = 1;
  c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK)
  {
    log("[ERROR] Camera init failed 0x" + String(err, HEX));
    esp_camera_deinit();
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  if (s && s->id.PID == OV5640_PID)
  {
    s->set_reg(s, 0x5183, 0x80, 0x80);
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
  }
  return true;
}

void cameraSleep()
{
  sensor_t *s = esp_camera_sensor_get();
  if (s)
  {
    if (s->id.PID == OV3660_PID || s->id.PID == OV5640_PID)
    {
      s->set_reg(s, 0x3008, 0x40, 0x40);
    }
    else if (s->id.PID == OV2640_PID)
    {
      s->set_reg(s, 0x109, 0x10, 0x10);
    }
  }
  esp_camera_deinit();
}

bool cameraCapture(std::vector<uint8_t> &jpeg, uint16_t &width, uint16_t &height)
{
  if (!cameraStart())
  {
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  uint32_t t0 = millis();
  int warm = 0;
  while (millis() - t0 < SETTLE_MS || warm < 3)
  {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
      break;
    }
    esp_camera_fb_return(fb);
    warm++;
  }

  bool ok = false;
  for (int q : QUALITY_STEPS)
  {
    s->set_quality(s, q);
    camera_fb_t *stale = esp_camera_fb_get();
    if (stale)
    {
      esp_camera_fb_return(stale);
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
      continue;
    }
    bool complete = fb->len > 4 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8 &&
                    fb->buf[fb->len - 2] == 0xFF && fb->buf[fb->len - 1] == 0xD9;
    if (complete && fb->len <= IMG_MAX_BYTES)
    {
      jpeg.assign(fb->buf, fb->buf + fb->len);
      width = fb->width;
      height = fb->height;
      log("[CAM] " + String(width) + "x" + height + ", " + fb->len + " bytes at quality " + q);
      ok = true;
    }
    esp_camera_fb_return(fb);
    if (ok)
    {
      break;
    }
  }
  cameraSleep();
  if (!ok)
  {
    log("[ERROR] Camera: no photo small enough");
  }
  return ok;
}

std::vector<uint8_t> jpeg;
uint32_t imgId = 0;
uint16_t width = 0, height = 0;
String forWhom;
bool wantPhoto = false;
String requestedBy;
std::vector<uint16_t> toSend;
size_t sendNext = 0;
bool sending = false;

uint16_t chunks()
{
  return (jpeg.size() + IMG_CHUNK - 1) / IMG_CHUNK;
}

uint8_t crc8(const uint8_t *data, size_t len)
{
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int b = 0; b < 8; b++)
    {
      crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : crc << 1;
    }
  }
  return crc;
}

String chunkLine(uint16_t seq)
{
  size_t off = (size_t)seq * IMG_CHUNK;
  size_t len = min((size_t)IMG_CHUNK, jpeg.size() - off);
  unsigned char b64[IMG_CHUNK * 4 / 3 + 8];
  size_t outLen = 0;
  mbedtls_base64_encode(b64, sizeof b64, &outLen, jpeg.data() + off, len);
  char crc[3];
  snprintf(crc, sizeof crc, "%02x", crc8(jpeg.data() + off, len));
  return "IMG|" + String(imgId) + "|" + seq + "|" + crc + "|" + String((char *)b64).substring(0, outLen);
}

void photoAgain(const String &from, const String &rest)
{
  int bar = rest.indexOf('|');
  if (bar < 0 || (uint32_t)rest.substring(0, bar).toInt() != imgId || jpeg.empty())
  {
    return;
  }
  std::vector<uint16_t> seqs;
  String list = rest.substring(bar + 1);
  int start = 0;
  while (start < (int)list.length() && seqs.size() < chunks())
  {
    int comma = list.indexOf(',', start);
    if (comma < 0)
    {
      comma = list.length();
    }
    String item = list.substring(start, comma);
    int dash = item.indexOf('-');
    long a = item.toInt();
    long b = dash > 0 ? item.substring(dash + 1).toInt() : a;
    for (long seq = max(a, 0L); seq <= b && seq < chunks() && seqs.size() < chunks(); seq++)
    {
      seqs.push_back(seq);
    }
    start = comma + 1;
  }
  if (seqs.empty())
  {
    return;
  }
  log("[CAM] Photo " + String(imgId) + ": " + seqs.size() + " pieces again for #" + from);
  toSend = seqs;
  sendNext = 0;
  sending = true;
}

bool photoHandle(const String &line)
{
  if (!line.startsWith("CMD|"))
  {
    return false;
  }
  String rest = line.substring(4);
  int bar = rest.indexOf('|');
  if (bar <= 0)
  {
    return true;
  }
  String from = rest.substring(0, bar);
  String cmd = rest.substring(bar + 1);
  if (cmd == "cam")
  {
    if (!(from == forWhom && (sending || wantPhoto)))
    {
      wantPhoto = true;
      requestedBy = from;
      log("[CAM] Photo requested by #" + from);
    }
  }
  else if (cmd.startsWith("again|"))
  {
    photoAgain(from, cmd.substring(6));
  }
  return true;
}

bool photoStep(bool &ended)
{
  ended = false;
  if (wantPhoto)
  {
    wantPhoto = false;
    std::vector<uint8_t> shot;
    if (!cameraCapture(shot, width, height))
    {
      return false;
    }
    jpeg.swap(shot);
    imgId = (imgId ? imgId : esp_random() % 1000) % IMG_ID_MAX + 1;
    forWhom = requestedBy;
    radioSend("IMG|" + String(imgId) + "|BEGIN|" + forWhom + "|" + chunks() + "|" + jpeg.size() + "|" +
              width + "x" + height);
    toSend.resize(chunks());
    for (uint16_t i = 0; i < toSend.size(); i++)
    {
      toSend[i] = i;
    }
    sendNext = 0;
    sending = true;
    return true;
  }
  if (!sending)
  {
    return false;
  }
  if (sendNext < toSend.size())
  {
    radioSend(chunkLine(toSend[sendNext++]));
    return true;
  }
  radioSend("IMG|" + String(imgId) + "|END");
  sending = false;
  ended = true;
  return true;
}

bool dumping = false;
uint32_t listenUntil = 0;
std::vector<uint32_t> dumpIds;
size_t dumpNext = 0;
uint32_t lastDumped = 0;

void startDump()
{
  dumpIds.clear();
  for (const Entry &e : cache)
  {
    dumpIds.push_back(e.id);
  }
  dumpNext = 0;
  radioSend("SAT|BEGIN|" + String(dumpIds.front()) + "|" + dumpIds.back() + "|" + dumpIds.size());
  dumping = true;
}

void continueDump()
{
  if (dumpNext < dumpIds.size())
  {
    uint32_t id = dumpIds[dumpNext++];
    for (const Entry &e : cache)
    {
      if (e.id == id)
      {
        radioSend("SAT|" + String(id) + "|" + e.text);
        break;
      }
    }
    return;
  }
  lastDumped = dumpIds.back();
  radioSend("SAT|END|" + String(lastDumped));
  dumping = false;
  listenUntil = millis() + LISTEN_MS;
}

void onReset(const String &line)
{
  String arg = line.substring(strlen("RESET CACHE"));
  arg.trim();
  uint32_t upTo = arg.isEmpty() ? lastDumped : (uint32_t)arg.toInt();
  if (!upTo)
  {
    return;
  }
  size_t before = cache.size();
  while (!cache.empty() && cache.front().id <= upTo)
  {
    cacheDropOldest();
  }
  size_t dropped = before - cache.size();
  if (dropped)
  {
    cacheRewrite();
  }
  log("[SAT] RESET CACHE " + String(upTo) + ": dropped " + dropped + ", " + cache.size() + " left");
}

bool addressed(const String &line)
{
  if (!line.startsWith("MSG|"))
  {
    return false;
  }
  int b = line.indexOf('|', 4);
  int c = b < 0 ? -1 : line.indexOf('|', b + 1);
  return b > 4 && c > b + 1 && line.substring(b + 1, c) != "*";
}

void onHeard(const String &line)
{
  if (!dumping)
  {
    listenUntil = max(listenUntil, (uint32_t)(millis() + LISTEN_EXTEND_MS));
  }
  if (line.startsWith("RESET CACHE"))
  {
    onReset(line);
    return;
  }
  if (photoHandle(line) || !addressed(line))
  {
    return;
  }
  if (cacheAdd(fit(line, FRAME_MAX - 16)))
  {
    log("[SAT] Stored #" + String(cache.back().id) + " (" + cache.size() + " lines, " + cacheBytes + " bytes): " + line);
  }
}

void lineDone()
{
  String line = fit(rx, FRAME_MAX);
  rx = "";
  if (!line.isEmpty())
  {
    onHeard(line);
  }
}

void radioPoll()
{
  while (Serial1.available())
  {
    char c = Serial1.read();
    lastByteMs = millis();
    if (c == '\n')
    {
      lineDone();
    }
    else if (c != '\r' && rx.length() < 2 * FRAME_MAX)
    {
      rx += c;
    }
  }
  if (!rx.isEmpty() && millis() - lastByteMs > LINE_IDLE_MS)
  {
    lineDone();
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(RADIO_AUX, INPUT_PULLUP);
  Serial1.setRxBufferSize(2048);
  Serial1.begin(9600, SERIAL_8N1, RADIO_RX, RADIO_TX);
  cacheBegin();
  listenUntil = millis() + LISTEN_MS;
  log("[OK] Payload ready");
}

void loop()
{
  radioPoll();
  bool ended = false;
  if (photoStep(ended))
  {
    if (ended)
    {
      dumping = false;
      listenUntil = millis() + LISTEN_MS;
    }
  }
  else if (dumping)
  {
    continueDump();
  }
  else if (millis() >= listenUntil)
  {
    if (cache.empty())
    {
      listenUntil = millis() + LISTEN_MS;
    }
    else
    {
      startDump();
    }
  }
  delay(2);
}
