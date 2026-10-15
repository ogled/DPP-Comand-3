#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <deque>
#include <vector>

#ifndef DEFAULT_TERMINAL
#define DEFAULT_TERMINAL 1
#endif
#define DEFAULT_PIN "TEST1234"
#define BOOT_BUTTON 0
#define FACTORY_RESET_MS 5000

#define RADIO_RX 17
#define RADIO_TX 18
#define RADIO_BAUD 9600
#define MSG_MAX 240
#define FRAME_MAX 320
#define LINE_IDLE_MS 300
#define OK_TIMEOUT_MS 3000
#define QUEUE_MAX 24

#define REPLY_MIN_MS 200
#define REPLY_MAX_MS 1500
#define RESEND_PER_WINDOW 3
#define PENDING_MAX 16
#define PENDING_TRIES 30
#define PENDING_AGE_MS (48UL * 3600 * 1000)
#define SEEN_KEEP 256

#define IMG_CHUNK 30
#define IMG_MAX_BYTES 4096
#define LINE_ONE_PACKET 57
#define CAM_REQUEST_MS (30UL * 60 * 1000)
#define CAM_IDLE_ASKS 12
#define CAM_STALL_MS 10000
#define CAM_KEEP 3

#define CHAT_KEEP 60

extern const char PAGE[];

const IPAddress AP_IP(192, 168, 4, 1);
WebServer server(80);
DNSServer dns;
Preferences prefs;
uint32_t apRestartAt = 0;
uint32_t bootPressedAt = 0;

uint16_t terminal;
String apName;
String apPass;
String pin;

String apNameOf(uint16_t term, const String &name)
{
  if (!name.isEmpty())
  {
    return name;
  }
  char buf[20];
  snprintf(buf, sizeof buf, "Glebotron-%03u", term);
  return buf;
}

void loadConfig()
{
  terminal = prefs.getUShort("id", DEFAULT_TERMINAL);
  if (terminal < 1 || terminal > 999)
  {
    terminal = DEFAULT_TERMINAL;
  }
  apName = prefs.getString("apn", "");
  apPass = prefs.getString("app", "");
  pin = prefs.getString("pin", DEFAULT_PIN);
}

void restartAp()
{
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apNameOf(terminal, apName).c_str(), apPass.isEmpty() ? nullptr : apPass.c_str());
  Serial.println("[OK] Access point: " + apNameOf(terminal, apName));
}

struct ChatLine
{
  uint32_t id;
  const char *o;
  String from;
  String to;
  String text;
  uint32_t img;
};
ChatLine chat[CHAT_KEEP];
uint32_t chatNext = 1;

String clean(const String &s)
{
  String out = s;
  for (size_t i = 0; i < out.length(); i++)
  {
    if ((uint8_t)out[i] < 0x20)
    {
      out.setCharAt(i, ' ');
    }
  }
  return out;
}

void chatAdd(const char *o, const String &from, const String &to, const String &text, uint32_t img = 0)
{
  chat[chatNext % CHAT_KEEP] = {chatNext, o, clean(from), clean(to), clean(text), img};
  chatNext++;
}

bool chatContains(const String &text)
{
  for (uint32_t id = chatNext > CHAT_KEEP ? chatNext - CHAT_KEEP : 1; id < chatNext; id++)
  {
    if (chat[id % CHAT_KEEP].text == text)
    {
      return true;
    }
  }
  return false;
}

String nanoRx;
uint32_t lastByteMs = 0;
std::deque<String> txQueue;
bool nanoBusy = false;
uint32_t handedAt = 0;

String fit(const String &text, size_t max = MSG_MAX)
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

void handOver()
{
  if (nanoBusy || txQueue.empty())
  {
    return;
  }
  Serial.println("[RADIO TX] (" + String(txQueue.front().length()) + " B): " + txQueue.front());
  Serial1.print(txQueue.front());
  Serial1.print('\n');
  nanoBusy = true;
  handedAt = millis();
}

void txDone()
{
  if (!nanoBusy)
  {
    return;
  }
  nanoBusy = false;
  txQueue.pop_front();
}

bool radioSend(const String &text)
{
  String line = fit(text, FRAME_MAX);
  if (line.isEmpty())
  {
    return false;
  }
  if (txQueue.size() >= QUEUE_MAX)
  {
    Serial.println("[ERROR] Radio queue full, dropped: " + line);
    return false;
  }
  txQueue.push_back(line);
  handOver();
  return true;
}

bool parseMsg(const String &line, String &from, String &to, String &text)
{
  if (!line.startsWith("MSG|"))
  {
    return false;
  }
  int b = line.indexOf('|', 4);
  int c = b < 0 ? -1 : line.indexOf('|', b + 1);
  if (b <= 4 || c <= b + 1)
  {
    return false;
  }
  from = line.substring(4, b);
  to = line.substring(b + 1, c);
  text = line.substring(c + 1);
  return !text.isEmpty();
}

bool isCallsign(const String &s)
{
  if (s.length() < 3 || s.length() > 20)
  {
    return false;
  }
  for (char ch : s)
  {
    if (!isalnum((uint8_t)ch) && ch != '_')
    {
      return false;
    }
  }
  return true;
}

bool isNumber(const String &s)
{
  if (s.isEmpty() || s.length() > 5)
  {
    return false;
  }
  for (char ch : s)
  {
    if (!isdigit((uint8_t)ch))
    {
      return false;
    }
  }
  return true;
}

void splitTarget(const String &typed, String &to, String &text)
{
  String s = typed;
  s.trim();
  to = "*";
  text = s;
  int space = s.indexOf(' ');
  if (space < 2 || (s[0] != '@' && s[0] != '#'))
  {
    return;
  }
  String key = s.substring(1, space);
  String rest = s.substring(space + 1);
  rest.trim();
  if (rest.isEmpty())
  {
    return;
  }
  if (s[0] == '@' && isCallsign(key))
  {
    to = "@" + key;
  }
  else if (s[0] == '#' && isNumber(key))
  {
    long n = key.toInt();
    if (n >= 1 && n <= 999)
    {
      to = String(n);
    }
    else if (n >= 1000 && n <= 65000)
    {
      to = "#" + key;
    }
    else
    {
      return;
    }
  }
  else
  {
    return;
  }
  text = rest;
}

bool camRequesting = false;
uint32_t camRequestedAt = 0;
uint32_t camAgainAt = 0;
uint32_t lastImgMs = 0;
uint32_t lastAskMs = 0;

struct Incoming
{
  bool active = false;
  uint32_t id = 0;
  uint16_t total = 0;
  uint16_t got = 0;
  size_t bytes = 0;
  String size;
  uint8_t idleAsks = 0;
  uint16_t gotAtLastAsk = 0;
  std::vector<uint8_t> jpeg;
  std::vector<bool> have;
};
Incoming img;

struct Photo
{
  uint32_t id;
  std::vector<uint8_t> jpeg;
};
std::deque<Photo> photos;

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

void camAskAgain()
{
  img.idleAsks = img.got > img.gotAtLastAsk ? 0 : img.idleAsks + 1;
  img.gotAtLastAsk = img.got;
  if (img.idleAsks > CAM_IDLE_ASKS)
  {
    img.active = false;
    chatAdd("sys", "", "", "Снимок пришёл не целиком. Отправьте /cam ещё раз, когда спутник будет рядом.");
    return;
  }
  String head = "CMD|" + String(terminal) + "|again|" + img.id + "|";
  String list;
  for (uint16_t i = 0; i < img.total;)
  {
    if (img.have[i])
    {
      i++;
      continue;
    }
    uint16_t j = i;
    while (j + 1 < img.total && !img.have[j + 1])
    {
      j++;
    }
    String item = j > i ? String(i) + "-" + j : String(i);
    if (head.length() + list.length() + (list.isEmpty() ? 0 : 1) + item.length() > LINE_ONE_PACKET)
    {
      break;
    }
    if (!list.isEmpty())
    {
      list += ',';
    }
    list += item;
    i = j + 1;
  }
  if (list.isEmpty())
  {
    return;
  }
  radioSend(head + list);
  lastAskMs = millis();
  Serial.println("[CAM] Photo " + String(img.id) + ": " + img.got + " of " + img.total + ", asked again: " + list);
}

void camBegin(uint32_t id, const String &rest)
{
  int a = rest.indexOf('|'), b = rest.indexOf('|', a + 1), c = rest.indexOf('|', b + 1);
  if (a < 0 || b < 0 || c < 0)
  {
    return;
  }
  if (rest.substring(0, a) != String(terminal))
  {
    return;
  }
  if (img.id == id && (img.active || !camRequesting))
  {
    return;
  }
  img = Incoming();
  img.active = true;
  img.id = id;
  img.total = rest.substring(a + 1, b).toInt();
  img.bytes = rest.substring(b + 1, c).toInt();
  img.size = rest.substring(c + 1);
  camRequesting = false;
  camAgainAt = 0;
  if (!img.total || img.bytes > IMG_MAX_BYTES || img.total != (img.bytes + IMG_CHUNK - 1) / IMG_CHUNK)
  {
    img.active = false;
    return;
  }
  img.jpeg.assign(img.bytes, 0);
  img.have.assign(img.total, false);
  lastImgMs = millis();
}

void camPiece(uint32_t id, uint16_t seq, const String &rest)
{
  if (!img.active || id != img.id || seq >= img.total || img.have[seq])
  {
    return;
  }
  if (rest.indexOf('|') != 2)
  {
    return;
  }
  uint8_t crc = strtoul(rest.substring(0, 2).c_str(), nullptr, 16);
  String b64 = rest.substring(3);
  size_t want = seq + 1 < img.total ? IMG_CHUNK : img.bytes - (size_t)seq * IMG_CHUNK;
  unsigned char buf[IMG_CHUNK + 4];
  size_t n = 0;
  if (mbedtls_base64_decode(buf, sizeof buf, &n, (const unsigned char *)b64.c_str(), b64.length()) != 0 ||
      n != want || crc8(buf, n) != crc)
  {
    Serial.println("[CAM] Photo " + String(id) + ": piece " + seq + " damaged, will ask again");
    return;
  }
  memcpy(img.jpeg.data() + (size_t)seq * IMG_CHUNK, buf, n);
  img.have[seq] = true;
  if (++img.got == img.total)
  {
    img.active = false;
    photos.push_back({img.id, std::move(img.jpeg)});
    if (photos.size() > CAM_KEEP)
    {
      photos.pop_front();
    }
    chatAdd("sat", "", String(terminal), "Снимок со спутника, " + img.size, img.id);
    Serial.println("[CAM] Photo " + String(img.id) + ": " + img.bytes + " bytes");
  }
}

bool camHandle(const String &line)
{
  if (line.startsWith("CMD|"))
  {
    return true;
  }
  if (!line.startsWith("IMG|"))
  {
    return false;
  }
  String rest = line.substring(4);
  int bar = rest.indexOf('|');
  if (bar <= 0)
  {
    return true;
  }
  uint32_t id = rest.substring(0, bar).toInt();
  if (id == img.id)
  {
    lastImgMs = millis();
  }
  String tail = rest.substring(bar + 1);
  if (tail.startsWith("BEGIN|"))
  {
    camBegin(id, tail.substring(6));
  }
  else if (tail == "END")
  {
    if (img.active && id == img.id)
    {
      camAgainAt = millis() + random(REPLY_MIN_MS, REPLY_MAX_MS);
    }
  }
  else
  {
    int bar2 = tail.indexOf('|');
    if (bar2 > 0)
    {
      camPiece(id, tail.substring(0, bar2).toInt(), tail.substring(bar2 + 1));
    }
  }
  return true;
}

bool camWantsWindow()
{
  if (camRequesting && millis() - camRequestedAt > CAM_REQUEST_MS)
  {
    camRequesting = false;
    chatAdd("sys", "", "", "Спутник не ответил на /cam за полчаса.");
  }
  return camRequesting || img.active;
}

void camPoll()
{
  if (camAgainAt && millis() >= camAgainAt)
  {
    camAgainAt = 0;
    if (img.active)
    {
      camAskAgain();
    }
    return;
  }
  uint32_t now = millis();
  if (img.active && !camAgainAt && now - lastImgMs > CAM_STALL_MS && now - lastAskMs > CAM_STALL_MS)
  {
    camAskAgain();
  }
}

uint32_t seen[SEEN_KEEP];
size_t seenNext = 0;

struct Pending
{
  String wire;
  uint8_t tries;
  uint32_t since;
};
std::vector<Pending> pending;
uint32_t replyAt = 0;

void satItem(uint32_t id, const String &wire)
{
  for (size_t i = 0; i < pending.size(); i++)
  {
    if (pending[i].wire == wire)
    {
      pending.erase(pending.begin() + i);
      break;
    }
  }
  for (uint32_t s : seen)
  {
    if (s == id)
    {
      return;
    }
  }
  seen[seenNext++ % SEEN_KEEP] = id;

  String from, to, text;
  if (parseMsg(wire, from, to, text) && to == String(terminal) && !chatContains(text))
  {
    chatAdd("sat", from, to, text);
  }
}

bool satHandle(const String &line)
{
  if (line.startsWith("RESET CACHE"))
  {
    return true;
  }
  if (!line.startsWith("SAT|"))
  {
    return false;
  }
  if (line.startsWith("SAT|BEGIN|"))
  {
    return true;
  }
  if (line.startsWith("SAT|END|"))
  {
    if (!pending.empty() || camWantsWindow())
    {
      replyAt = millis() + random(REPLY_MIN_MS, REPLY_MAX_MS);
    }
    return true;
  }
  String rest = line.substring(4);
  int bar = rest.indexOf('|');
  uint32_t id = bar > 0 ? rest.substring(0, bar).toInt() : 0;
  if (id)
  {
    satItem(id, rest.substring(bar + 1));
  }
  return true;
}

void satPoll()
{
  if (!replyAt || millis() < replyAt)
  {
    return;
  }
  replyAt = 0;
  uint32_t now = millis();
  for (size_t i = 0; i < pending.size();)
  {
    if (pending[i].tries >= PENDING_TRIES || now - pending[i].since > PENDING_AGE_MS)
    {
      pending.erase(pending.begin() + i);
    }
    else
    {
      i++;
    }
  }
  int sent = 0;
  for (Pending &p : pending)
  {
    if (sent >= RESEND_PER_WINDOW)
    {
      break;
    }
    radioSend(p.wire);
    p.tries++;
    sent++;
  }
  if (sent)
  {
    Serial.println("[SAT] Sent " + String(sent) + " line(s) again, " + pending.size() + " waiting for the payload");
  }
  if (camRequesting && camWantsWindow())
  {
    radioSend("CMD|" + String(terminal) + "|cam");
  }
  else if (img.active)
  {
    camAskAgain();
  }
}

void typedHere(const String &nick, const String &typed)
{
  String trimmed = typed;
  trimmed.trim();
  if (trimmed == "/cam")
  {
    camRequesting = true;
    camRequestedAt = millis();
    radioSend("CMD|" + String(terminal) + "|cam");
    chatAdd("sys", "", "", "Запрошен снимок со спутника. Он придёт, когда спутник будет рядом.");
    return;
  }
  if (trimmed.startsWith("/"))
  {
    chatAdd("sys", "", "", "Неизвестная команда " + trimmed + ". Есть: /cam");
    return;
  }
  String to, body;
  splitTarget(typed, to, body);
  String text = fit(nick.isEmpty() ? body : nick + ": " + body);
  if (text.isEmpty())
  {
    return;
  }
  String wire = "MSG|" + String(terminal) + "|" + to + "|" + text;
  if (!radioSend(wire))
  {
    return;
  }
  chatAdd("here", String(terminal), to, text);
  if (to == "*")
  {
    return;
  }
  for (const Pending &p : pending)
  {
    if (p.wire == wire)
    {
      return;
    }
  }
  if (pending.size() >= PENDING_MAX)
  {
    pending.erase(pending.begin());
  }
  pending.push_back({wire, 0, millis()});
}

void heardOnAir(const String &line)
{
  if (satHandle(line) || camHandle(line))
  {
    return;
  }
  String from, to, text;
  if (parseMsg(line, from, to, text))
  {
    chatAdd("air", from, to, text);
  }
  else
  {
    chatAdd("air", "", "", line);
  }
}

void nanoLine()
{
  String line = fit(nanoRx, FRAME_MAX);
  nanoRx = "";
  if (line.isEmpty())
  {
    return;
  }
  if (!line.startsWith("NANO|"))
  {
    Serial.println("[RADIO RX] (" + String(line.length()) + " B): " + line);
    heardOnAir(line);
    return;
  }
  String rest = line.substring(5);
  if (rest == "OK")
  {
    txDone();
  }
  else if (rest == "READY")
  {
    Serial.println("[RADIO] Nano restarted");
    nanoBusy = false;
  }
  else if (rest.startsWith("TX|"))
  {
    heardOnAir(rest.substring(3));
  }
  else
  {
    chatAdd("sys", "", "", "Nano: " + rest);
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
      nanoLine();
    }
    else if ((uint8_t)c >= 0x20 && nanoRx.length() < 2 * FRAME_MAX)
    {
      nanoRx += c;
    }
  }
  if (!nanoRx.isEmpty() && millis() - lastByteMs > LINE_IDLE_MS)
  {
    nanoLine();
  }
  if (nanoBusy && millis() - handedAt > OK_TIMEOUT_MS)
  {
    Serial.println("[RADIO] No OK from the Nano, going on");
    txDone();
  }
  handOver();
}

void sendJson(JsonDocument &doc)
{
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", out);
}

void fail(int code, const char *message)
{
  server.send(code, "text/plain; charset=utf-8", message);
}

bool pinOk()
{
  if (pin.isEmpty() || server.arg("k") == pin)
  {
    return true;
  }
  fail(403, "pin");
  return false;
}

void handleState()
{
  JsonDocument doc;
  doc["terminal"] = terminal;
  doc["site"] = "off";
  doc["pin"] = !pin.isEmpty();
  if (img.active)
  {
    doc["photo"]["got"] = img.got;
    doc["photo"]["total"] = img.total;
  }
  sendJson(doc);
}

void handleLines()
{
  JsonDocument doc;
  JsonArray out = doc.to<JsonArray>();
  uint32_t first = chatNext > CHAT_KEEP ? chatNext - CHAT_KEEP : 1;
  uint32_t after = server.arg("after").toInt();
  if (after + 1 > first)
  {
    first = after + 1;
  }
  for (uint32_t id = first; id < chatNext; id++)
  {
    const ChatLine &l = chat[id % CHAT_KEEP];
    JsonObject o = out.add<JsonObject>();
    o["id"] = l.id;
    o["o"] = l.o;
    o["at"] = 0;
    o["f"] = l.from;
    o["to"] = l.to;
    o["t"] = l.text;
    if (l.img)
    {
      o["img"] = l.img;
    }
  }
  sendJson(doc);
}

void handleImage()
{
  uint32_t id = server.arg("id").toInt();
  for (const Photo &p : photos)
  {
    if (p.id == id)
    {
      server.sendHeader("Cache-Control", "max-age=86400");
      server.send_P(200, "image/jpeg", (const char *)p.jpeg.data(), p.jpeg.size());
      return;
    }
  }
  fail(404, "Снимка уже нет на терминале");
}

void handleSend()
{
  String nick = server.arg("n"), text = fit(server.arg("t"));
  nick.replace("|", "");
  nick.replace(":", "");
  nick.trim();
  if (nick.length() > 24)
  {
    nick = nick.substring(0, 24);
  }
  if (text.isEmpty())
  {
    return fail(400, "empty");
  }
  typedHere(nick, text);
  server.send(204);
}

void handleAdminInfo()
{
  if (!pinOk())
  {
    return;
  }
  JsonDocument doc;
  doc["terminal"] = terminal;
  doc["apName"] = apName;
  doc["apDefault"] = apNameOf(terminal, "");
  doc["apOpen"] = apPass.isEmpty();
  doc["pin"] = !pin.isEmpty();
  sendJson(doc);
}

void handleAdminSave()
{
  if (!pinOk())
  {
    return;
  }
  long newTerminal = server.arg("terminal").toInt();
  String newName = server.arg("apName"), pass = server.arg("apPass"), newPin = server.arg("pin");
  bool open = server.arg("apOpen") == "1";
  newName.trim();

  if (newTerminal < 1 || newTerminal > 999)
  {
    return fail(400, "Номер терминала: от 1 до 999");
  }
  if (newName.length() > 32)
  {
    return fail(400, "Имя точки доступа: до 32 символов");
  }
  if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 64))
  {
    return fail(400, "Пароль точки доступа: от 8 до 64 символов");
  }
  if (newPin.length() > 16)
  {
    return fail(400, "PIN: до 16 символов");
  }
  String newPass = open ? "" : pass.isEmpty() ? apPass : pass;
  if (!open && newPass.isEmpty())
  {
    return fail(400, "Пароль точки доступа: от 8 до 64 символов");
  }
  if (server.arg("noPin") == "1")
  {
    newPin = "";
  }
  else if (newPin.isEmpty())
  {
    newPin = pin;
  }

  bool apChanged = apNameOf(newTerminal, newName) != apNameOf(terminal, apName) || newPass != apPass;
  prefs.putUShort("id", newTerminal);
  prefs.putBool("api", false);
  prefs.putString("apn", newName);
  prefs.putString("app", newPass);
  prefs.putString("pin", newPin);
  loadConfig();
  server.send(200, "text/plain", apChanged ? "ap" : "ok");
  if (apChanged)
  {
    apRestartAt = millis() + 1000;
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  randomSeed(esp_random());

  prefs.begin("cfg", false);
  loadConfig();

  Serial1.setRxBufferSize(2048);
  Serial1.begin(RADIO_BAUD, SERIAL_8N1, RADIO_RX, RADIO_TX);

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  restartAp();

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);
  server.on("/", HTTP_GET, []
  {
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html; charset=utf-8", PAGE);
  });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/lines", HTTP_GET, handleLines);
  server.on("/api/image", HTTP_GET, handleImage);
  server.on("/api/send", HTTP_POST, handleSend);
  server.on("/api/admin/info", HTTP_POST, handleAdminInfo);
  server.on("/api/admin/save", HTTP_POST, handleAdminSave);
  server.onNotFound([]
  {
    server.sendHeader("Location", "http://" + AP_IP.toString() + "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  Serial.println("[OK] Terminal #" + String(terminal) + " ready");
}

void loop()
{
  dns.processNextRequest();
  server.handleClient();
  if (apRestartAt && millis() > apRestartAt)
  {
    apRestartAt = 0;
    restartAp();
  }
  radioPoll();
  satPoll();
  camPoll();

  if (digitalRead(BOOT_BUTTON) != LOW)
  {
    bootPressedAt = 0;
  }
  else if (!bootPressedAt)
  {
    bootPressedAt = millis();
  }
  else if (millis() - bootPressedAt > FACTORY_RESET_MS)
  {
    Serial.println("[RESET] Factory reset");
    prefs.clear();
    Preferences wifi;
    wifi.begin("wifi", false);
    wifi.clear();
    ESP.restart();
  }
  delay(2);
}

const char PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#070b10"><title>Терминал</title>
<style>
:root{--bg:#070b10;--s1:#0b1119;--s2:#101822;--s3:#16202b;--line:rgba(146,178,204,.11);--line2:rgba(146,178,204,.22);
--ink:#dde6ef;--ink2:#97a8b9;--ink3:#6d7e8f;--sig:#29c5e8;--sigd:rgba(41,197,232,.13);--ok:#3ddc97;--okd:rgba(61,220,151,.13);
--warn:#e8a33d;--bad:#ef6461;--r:2px;--mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace}
*{box-sizing:border-box}
[hidden]{display:none!important}
html{color-scheme:dark}
body{margin:0;height:100vh;height:100dvh;display:flex;flex-direction:column;background:var(--bg);color:var(--ink);
font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-font-smoothing:antialiased}
button,input{font:inherit;color:inherit}
button{cursor:pointer}
:focus-visible{outline:2px solid var(--sig);outline-offset:2px}
.num{font-family:var(--mono);font-variant-numeric:tabular-nums}
.top{display:flex;align-items:center;gap:12px;padding:10px 16px;padding-top:max(10px,env(safe-area-inset-top));border-bottom:1px solid var(--line)}
.who{flex:1;min-width:0}
.who b{display:block;font-size:15px;font-weight:600}
.st{display:flex;flex-wrap:wrap;gap:4px 14px;font-size:12px;color:var(--ink2)}
.st span{display:inline-flex;align-items:center;gap:6px}
.dot{width:7px;height:7px;border-radius:50%;background:var(--ink3);flex:none}
.dot.on{background:var(--ok);box-shadow:0 0 0 3px var(--okd)}
.dot.warn{background:var(--warn)}.dot.bad{background:var(--bad)}
.btn{height:34px;padding:0 12px;border-radius:var(--r);border:1px solid var(--line2);background:none;font-size:13px}
.btn:hover{background:var(--s2)}
.btn.pri{background:var(--sig);border-color:var(--sig);color:#041018;font-weight:600}
.btn.pri:disabled{background:var(--s3);border-color:var(--line);color:var(--ink3);cursor:default}
#log{flex:1;overflow-y:auto;padding:16px;display:flex;flex-direction:column;gap:10px}
.empty{margin:auto;max-width:34ch;text-align:center;color:var(--ink3);font-size:14px}
.m{max-width:min(82%,520px);padding:8px 12px;border:1px solid var(--line);border-radius:var(--r);background:var(--s1);align-self:flex-start}
.m.here{align-self:flex-end;background:var(--s2);border-color:var(--line2)}
.m.site,.m.sat{border-left:2px solid var(--sig)}
.who-said{display:block;font-size:12px;font-weight:600;color:var(--ink2);margin-bottom:2px}
.txt{white-space:pre-wrap;overflow-wrap:anywhere}
.meta{display:flex;justify-content:flex-end;gap:8px;margin-top:2px;font-size:11px;color:var(--ink3)}
.meta .from{color:var(--sig)}
.m .photo{display:block;width:100%;max-width:320px;margin:4px 0 2px;border-radius:var(--r);background:var(--s2)}
.m .save{font-size:12px;color:var(--sig);text-decoration:none}
.sys{align-self:center;max-width:42ch;text-align:center;font-size:12px;color:var(--ink3)}
.under .photo-prog{color:var(--sig)}
.compose{border-top:1px solid var(--line);padding:10px 16px;padding-bottom:max(10px,env(safe-area-inset-bottom))}
.row{display:flex;gap:8px}
.in{height:40px;padding:0 12px;min-width:0;background:var(--s1);border:1px solid var(--line2);border-radius:var(--r);font-size:16px}
.in:focus{outline:none;border-color:var(--sig)}
.in::placeholder{color:var(--ink3)}
#nick{width:96px;flex:none}#text{flex:1}
.send{width:40px;height:40px;flex:none;display:grid;place-items:center;border:0;border-radius:var(--r);background:var(--sig);color:#041018}
.send:disabled{background:var(--s3);color:var(--ink3);cursor:default}
.under{display:flex;flex-wrap:wrap;justify-content:space-between;gap:4px 12px;margin-top:6px;font-size:12px;color:var(--ink3)}
.under b{color:var(--ink2);font-weight:600}
.under .over{color:var(--bad)}
.sheet{position:fixed;inset:0;z-index:5;display:flex;align-items:flex-end;justify-content:center;background:rgba(0,0,0,.6)}
.panel{width:100%;max-width:460px;max-height:92vh;overflow-y:auto;background:var(--s1);border:1px solid var(--line2);border-bottom:0;
border-radius:var(--r) var(--r) 0 0;padding:16px;padding-bottom:max(18px,env(safe-area-inset-bottom));display:flex;flex-direction:column;gap:14px}
@media(min-width:600px){.sheet{align-items:center}.panel{border-bottom:1px solid var(--line2);border-radius:var(--r)}}
.ph{display:flex;align-items:center;justify-content:space-between}.ph b{font-size:16px}
.x{width:32px;height:32px;border:0;background:none;color:var(--ink2);font-size:18px}
.tabs{display:flex;border:1px solid var(--line2);border-radius:var(--r)}
.tab{flex:1;height:34px;border:0;background:none;color:var(--ink2);font-size:13px}
.tab+.tab{border-left:1px solid var(--line2)}
.tab[aria-selected=true]{background:var(--s3);color:var(--ink)}
.sec{display:flex;flex-direction:column;gap:12px}
.fl{display:flex;flex-direction:column;gap:6px;font-size:12px;color:var(--ink2)}
.fl .in{width:100%;color:var(--ink)}
.hint{font-size:12px;color:var(--ink3)}
.sw{display:flex;align-items:center;justify-content:space-between;gap:12px;font-size:14px}
.sw input{appearance:none;-webkit-appearance:none;width:40px;height:22px;margin:0;flex:none;border-radius:11px;background:var(--s3);position:relative;transition:background .11s}
.sw input::before{content:"";position:absolute;top:3px;left:3px;width:16px;height:16px;border-radius:50%;background:var(--ink2);transition:left .11s}
.sw input:checked{background:var(--sig)}.sw input:checked::before{left:21px;background:#041018}
.card{display:flex;align-items:center;gap:10px;padding:10px 12px;border:1px solid var(--line2);border-radius:var(--r);font-size:14px}
.card small{display:block;font-size:12px;color:var(--ink3)}
.lh{display:flex;align-items:center;justify-content:space-between;font-size:12px;color:var(--ink2)}
.lk{border:0;background:none;padding:0;color:var(--sig);font-size:12px}
#nets{display:flex;flex-direction:column;border-top:1px solid var(--line);max-height:34vh;overflow-y:auto}
.net{display:flex;align-items:center;gap:10px;width:100%;padding:10px 4px;border:0;border-bottom:1px solid var(--line);background:none;text-align:left}
.net span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net[aria-pressed=true]{color:var(--sig)}
.net small{font-size:11px;color:var(--ink3)}
.bars{display:flex;align-items:flex-end;gap:2px;height:12px}.bars i{width:3px;background:var(--s3)}.bars i.on{background:var(--ink2)}
.msg{min-height:18px;font-size:13px;color:var(--ink3)}.msg.e{color:var(--bad)}.msg.k{color:var(--ok)}
</style></head><body>
<header class="top">
  <div class="who"><b id="name">Терминал</b>
    <div class="st"><span><i class="dot on"></i>Радио</span><span><i class="dot" id="sd"></i><span id="ss">…</span></span></div>
  </div>
  <button class="btn" id="open">Настройки</button>
</header>
<main id="log" aria-live="polite"><p class="empty" id="empty">Здесь появятся сообщения радиоканала. Напишите первым: сообщение услышат все терминалы рядом.</p></main>
<form class="compose" id="f">
  <div class="row">
    <input class="in" id="nick" placeholder="Имя" maxlength="12" autocomplete="nickname" aria-label="Ваше имя">
    <input class="in" id="text" placeholder="Сообщение" autocomplete="off" enterkeyhint="send" aria-label="Сообщение">
    <button class="send" id="send" aria-label="Отправить"><svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 8h10M8.5 3.5L13 8l-4.5 4.5"/></svg></button>
  </div>
  <div class="under"><span id="tip"></span><span class="num" id="bytes"></span></div>
</form>

<div class="sheet" id="sheet" hidden><div class="panel" role="dialog" aria-labelledby="sh">
  <div class="ph"><b id="sh">Настройки</b><button class="x" id="close" aria-label="Закрыть">✕</button></div>
  <div id="login" class="sec">
    <label class="fl">PIN<input class="in" id="pin" type="password" maxlength="16" autocomplete="current-password"></label>
    <div class="msg e" id="lm"></div><button class="btn pri" id="enter">Войти</button>
  </div>
  <div id="admin" class="sec" hidden>
    <div class="tabs" role="tablist">
      <button class="tab" role="tab" aria-selected="true" data-t="0">Терминал</button>
      <button class="tab" role="tab" aria-selected="false" data-t="1">Доступ</button>
    </div>
    <div class="sec">
      <label class="fl">Номер терминала<input class="in num" id="c_terminal" type="number" min="1" max="999" inputmode="numeric"></label>
      <label class="fl">Имя точки доступа<input class="in" id="c_apName" maxlength="32"><span class="hint" id="aph"></span></label>
      <label class="sw">Открытая точка доступа<input id="c_apOpen" type="checkbox"></label>
      <label class="fl" id="appl">Пароль точки доступа<input class="in" id="c_apPass" type="password" maxlength="64"><span class="hint">От 8 символов. Пусто: без изменений</span></label>
      <button class="btn pri sv">Сохранить</button><div class="msg"></div>
    </div>
    <div class="sec" hidden>
      <label class="fl">Новый PIN<input class="in" id="c_pin" type="password" maxlength="16" autocomplete="new-password"><span class="hint">Пусто: без изменений</span></label>
      <label class="sw">Настройки без PIN<input id="c_noPin" type="checkbox"></label>
      <button class="btn pri sv">Сохранить</button><div class="msg"></div>
      <button class="btn" id="logout">Выйти из настроек</button>
    </div>
  </div>
</div></div>

<script>
const $=id=>document.getElementById(id),log=$('log'),enc=new TextEncoder(),MAX=240;
let after=0,pin='',st={},seen={},sel=null,watch=0;
try{$('nick').value=localStorage.getItem('nick')||''}catch(e){}

const SAID=/^([^:\n]{1,12}): ([\s\S]+)$/;
function hm(m){const s=m.at||(seen[m.id]??=Date.now()/1000);return new Date(s*1e3).toLocaleTimeString('ru-RU',{hour:'2-digit',minute:'2-digit'})}
function who(x){return !x||x=='*'?'':/^\d+$/.test(x)?'#'+x:x}
function tag(text,cls){const s=document.createElement('span');s.className=cls||'';s.textContent=text;return s}
function add(m){
  $('empty')?.remove();
  if(m.o=='sys'){const n=tag(m.t,'sys');log.append(n);log.scrollTop=log.scrollHeight;return}
  const d=document.createElement('div');d.className='m '+m.o;
  const p=SAID.exec(m.t);
  const speaker=p?p[1]:(m.f&&!/^\d+$/.test(m.f)?m.f:'');
  if(speaker)d.append(tag(speaker,'who-said'));
  const t=document.createElement('div');t.className='txt';t.textContent=p?p[2]:m.t;d.append(t);
  if(m.img){const src='/api/image?id='+m.img,im=document.createElement('img');im.className='photo';im.src=src;im.alt='Снимок со спутника';
    const a=document.createElement('a');a.className='save';a.href=src;a.download='sputnik_'+m.img+'.jpg';a.textContent='Сохранить снимок';d.append(im,a)}
  const meta=document.createElement('div');meta.className='meta';
  if(m.o=='site')meta.append(tag('с сайта','from'));
  if(m.o=='sat')meta.append(tag('через спутник','from'));
  if((m.o=='air'||m.o=='sat')&&/^\d+$/.test(m.f||''))meta.append(tag('с терминала #'+m.f));
  if(m.o!='sat'&&who(m.to)&&m.to!=String(st.terminal))meta.append(tag('→ '+who(m.to)));
  meta.append(tag(hm(m),'num'));d.append(meta);
  const near=log.scrollHeight-log.scrollTop-log.clientHeight<80;log.append(d);if(near||m.o=='here')log.scrollTop=log.scrollHeight}
async function lines(){try{const r=await fetch('/api/lines?after='+after);for(const m of await r.json()){add(m);after=m.id}}catch(e){}setTimeout(lines,1000)}

const SITE={online:['on','Сайт на связи'],key:['bad','Сайт не принял ключ'],trouble:['warn','Сайт не отвечает'],nowifi:['','Без интернета'],off:['','Без интернета']};
async function state(){try{st=await(await fetch('/api/state')).json();
  const n=String(st.terminal).padStart(3,'0');$('name').textContent='Терминал '+n;document.title=$('name').textContent;
  const [c,l]=SITE[st.site]||SITE.off;$('sd').className='dot '+c;$('ss').textContent=l;count()}catch(e){}}
setInterval(()=>{if(st.photo)state()},1000);
setInterval(state,3000);state().then(lines);

function line(){const n=$('nick').value.trim(),v=$('text').value.replace(/\s+/g,' ').trim();return v?(n?n+': ':'')+v:''}
function count(){const l=line(),b=enc.encode(l).length;
  $('bytes').textContent=b+' / '+MAX;$('bytes').className='num'+(b>MAX?' over':'');$('send').disabled=!l||b>MAX;
  $('tip').innerHTML=st.photo
    ?'<span class="photo-prog">Принимаем снимок: '+st.photo.got+' из '+st.photo.total+'</span>'
    :'<b>@позывной</b> или <b>#номер</b> в начале: лично. <b>/cam</b>: снимок со спутника'}
$('text').oninput=count;$('nick').oninput=count;count();
$('f').onsubmit=async e=>{e.preventDefault();const l=line();if(!l||enc.encode(l).length>MAX)return;
  const n=$('nick').value.trim(),v=$('text').value.replace(/\s+/g,' ').trim();
  try{localStorage.setItem('nick',n)}catch(e){}
  $('send').disabled=true;
  try{const r=await fetch('/api/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'n='+encodeURIComponent(n)+'&t='+encodeURIComponent(v)});
    if(r.ok)$('text').value=''}catch(e){}
  count();$('text').focus()};

const C=k=>$('c_'+k);
function post(u,o){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:Object.entries(Object.assign({k:pin},o||{})).map(([a,v])=>a+'='+encodeURIComponent(v)).join('&')})}
function say(el,t,c){el.textContent=t;el.className='msg'+(c?' '+c:'')}
function showLogin(err){$('login').hidden=false;$('admin').hidden=true;say($('lm'),err||'','e');$('pin').value='';setTimeout(()=>$('pin').focus(),50)}
async function info(fill){let r;try{r=await post('/api/admin/info')}catch(e){return}
  if(r.status==403){pin='';showLogin(fill?'Неверный PIN':'Нужно войти');return}
  const c=await r.json();
  if(!fill)return;
  $('login').hidden=true;$('admin').hidden=false;
  C('terminal').value=c.terminal;
  C('apName').value=c.apName;C('apName').placeholder=c.apDefault;$('aph').textContent='Пусто: '+c.apDefault;
  C('apOpen').checked=c.apOpen;C('apPass').value='';C('pin').value='';C('noPin').checked=!c.pin;apOpen()}
function apOpen(){$('appl').hidden=C('apOpen').checked}C('apOpen').onchange=apOpen;
function open_(){$('sheet').hidden=false;if(pin||!st.pin)info(1);else showLogin();clearInterval(watch);watch=setInterval(()=>{if(!$('admin').hidden)info(0)},3000)}
function close_(){$('sheet').hidden=true;clearInterval(watch)}
$('open').onclick=open_;$('close').onclick=close_;$('sheet').onclick=e=>{if(e.target==$('sheet'))close_()};
document.addEventListener('keydown',e=>{if(e.key=='Escape')close_()});
$('enter').onclick=()=>{pin=$('pin').value;info(1)};$('pin').onkeydown=e=>{if(e.key=='Enter')$('enter').click()};
$('logout').onclick=()=>{pin='';showLogin()};
document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tab').forEach(x=>x.setAttribute('aria-selected',x==b));
  document.querySelectorAll('#admin>.sec').forEach((x,i)=>x.hidden=i!=b.dataset.t)});
document.querySelectorAll('.sv').forEach(b=>b.onclick=async()=>{const m=b.nextElementSibling;b.disabled=true;
  const o={terminal:C('terminal').value,apName:C('apName').value,apPass:C('apPass').value,pin:C('pin').value,
    apOpen:C('apOpen').checked?1:0,noPin:C('noPin').checked?1:0};
  try{const r=await post('/api/admin/save',o),x=await r.text();
    if(r.ok){if(o.noPin==1)pin='';else if(o.pin)pin=o.pin;
      say(m,x=='ap'?'Сохранено. Точка доступа перезапускается: подключитесь к ней снова.':'Сохранено','k');
      C('apPass').value='';C('pin').value='';info(1);state()}
    else if(r.status==403){pin='';showLogin('Нужно войти')}else say(m,x,'e')}catch(e){say(m,'Нет связи с терминалом','e')}
  b.disabled=false});
</script></body></html>)html";
