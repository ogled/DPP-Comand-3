#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#define E32_RX 16
#define E32_TX 17
#define E32_AUX 4

IPAddress apIP(192, 168, 4, 1);
WebServer server(80);
DNSServer dns;

struct Msg { uint32_t id; bool own; String text; };
const int MAX_MSGS = 50;
Msg msgs[MAX_MSGS];
uint32_t nextId = 1;
String rxBuf;
uint32_t lastRx = 0;

const char PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>LoRa Chat</title>
<style>body{margin:0;font-family:sans-serif;background:#111;color:#eee;display:flex;flex-direction:column;height:100vh}
#log{flex:1;overflow-y:auto;padding:10px}.m{max-width:80%;margin:4px 0;padding:8px 12px;border-radius:12px;background:#2a2a2a;word-wrap:break-word}
.o{background:#1f6f43;margin-left:auto}form{display:flex;gap:6px;padding:8px;background:#1b1b1b}
input{padding:10px;border:0;border-radius:8px;background:#333;color:#eee}#t{flex:1}#n{width:80px}button{padding:10px 16px;border:0;border-radius:8px;background:#2e8b57;color:#fff}</style></head>
<body><div id="log"></div><form id="f"><input id="n" placeholder="Ник" maxlength="12"><input id="t" placeholder="Сообщение" maxlength="180" autocomplete="off"><button>➤</button></form>
<script>
let s=0;const log=document.getElementById('log'),n=document.getElementById('n'),t=document.getElementById('t');
try{n.value=localStorage.getItem('nick')||''}catch(e){}
function add(m){const d=document.createElement('div');d.className='m'+(m.o?' o':'');d.textContent=m.t;log.appendChild(d);log.scrollTop=log.scrollHeight}
async function poll(){try{const r=await fetch('/msgs?s='+s);const a=await r.json();a.forEach(m=>{add(m);s=m.i})}catch(e){}setTimeout(poll,1000)}
document.getElementById('f').onsubmit=async e=>{e.preventDefault();const v=t.value.trim();if(!v)return;
try{localStorage.setItem('nick',n.value)}catch(e){}
const msg=(n.value.trim()?n.value.trim()+': ':'')+v;t.value='';
await fetch('/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'t='+encodeURIComponent(msg)})};
poll();
</script></body></html>)rawliteral";

void addMsg(const String& t, bool own) {
  msgs[nextId % MAX_MSGS] = {nextId, own, t};
  nextId++;
}

String jsonEscape(const String& s) {
  String o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

void waitAux() {
  uint32_t t = millis();
  while (!digitalRead(E32_AUX) && millis() - t < 1000) delay(1);
}

void handleSend() {
  String t = server.arg("t");
  t.replace("\n", " ");
  t.replace("\r", " ");
  t.trim();
  if (t.isEmpty() || t.length() > 240) { server.send(400, "text/plain", "bad"); return; }
  waitAux();
  Serial2.print(t);
  Serial2.print('\n');
  addMsg(t, true);
  server.send(200, "text/plain", "ok");
}

void handleMsgs() {
  uint32_t since = server.arg("s").toInt();
  uint32_t start = nextId > MAX_MSGS ? nextId - MAX_MSGS : 1;
  if (since + 1 > start) start = since + 1;
  String out = "[";
  for (uint32_t i = start; i < nextId; i++) {
    Msg& m = msgs[i % MAX_MSGS];
    if (out.length() > 1) out += ',';
    out += "{\"i\":" + String(m.id) + ",\"o\":" + (m.own ? "1" : "0") + ",\"t\":\"" + jsonEscape(m.text) + "\"}";
  }
  out += "]";
  server.send(200, "application/json", out);
}

void flushRx() {
  rxBuf.trim();
  if (!rxBuf.isEmpty()) addMsg(rxBuf, false);
  rxBuf = "";
}

void setup() {
  Serial.begin(115200);
  pinMode(E32_AUX, INPUT);
  Serial2.begin(9600, SERIAL_8N1, E32_RX, E32_TX);

  WiFi.mode(WIFI_AP);
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  char ssid[20];
  snprintf(ssid, sizeof(ssid), "LoRaChat-%02X%02X", mac[4], mac[5]);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, nullptr, 1);
  Serial.println(ssid);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", apIP);

  server.on("/", [] { server.send_P(200, "text/html", PAGE); });
  server.on("/send", HTTP_POST, handleSend);
  server.on("/msgs", handleMsgs);
  server.onNotFound([] {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();

  Serial.println(WiFi.softAPIP());
}

void loop() {
  dns.processNextRequest();
  server.handleClient();
  while (Serial2.available()) {
    char c = Serial2.read();
    lastRx = millis();
    if (c == '\n') flushRx();
    else if (rxBuf.length() < 256) rxBuf += c;
  }
  if (!rxBuf.isEmpty() && millis() - lastRx > 300) flushRx();
}