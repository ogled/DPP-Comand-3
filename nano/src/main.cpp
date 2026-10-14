// Nano + E32 + BME280: a terminal that puts its sensor reading on air every
// 30 s, and the radio for whatever is on its Serial (D0/D1, 9600): a PC's
// serial monitor, or the ESP32-S3 web terminal (espNano/).
//
// One message is one line, ending in '\n'. On Serial:
//   in:  a line to put on air
//   out: every line heard on air, and the Nano's own lines, which start "NANO|":
//     NANO|READY       just started
//     NANO|OK          the line from Serial is with the E32; send the next one
//     NANO|TX|<line>   a line it put on air itself (the sensor report)
//     NANO|<note>      anything else ("BME280 ok", ...)
// Only whole lines go out, so its own lines never land inside one heard on air.
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define SEA_LEVEL_HPA 1013.25f
const char NANO_ID[] = "1";  // the Nano + ESP bundle is terminal 0001
const unsigned long REPORT_MS = 30000;
const size_t LINE_MAX = 320;              // the ESP's RADIO_FRAME_MAX
const unsigned long LINE_IDLE_MS = 300;   // a line heard with no '\n' ends after this much silence

Adafruit_BME280 bme;
bool bmeOk = false;

SoftwareSerial radio(10, 11);
const uint8_t AUX = 2;

unsigned long lastReport = 0;

char airLine[LINE_MAX + 1];  // heard on air, not yet passed on
size_t airLen = 0;
unsigned long airLast = 0;
char inLine[LINE_MAX + 1];   // from Serial, not yet on air
size_t inLen = 0;

void flushAir() {
  airLine[airLen] = 0;
  if (airLen) Serial.println(airLine);
  airLen = 0;
}

void pollAir() {
  while (radio.available()) {
    char c = radio.read();
    airLast = millis();
    if (c == '\n') flushAir();
    else if (c != '\r' && airLen < LINE_MAX) airLine[airLen++] = c;
  }
  if (airLen && millis() - airLast > LINE_IDLE_MS) flushAir();
}

// Keeps passing on what is heard while the module is busy.
void waitAux() {
  uint32_t t = millis();
  while (!digitalRead(AUX) && millis() - t < 1000) pollAir();
}

// The whole line in one go; the E32 cuts it into packets itself.
void putOnAir(const char *line) {
  waitAux();
  radio.print(line);
  radio.print('\n');
  delay(50);  // let AUX drop before the next line waits on it
}

void sendLine(const char *to, const char *text) {
  char wire[64];
  snprintf(wire, sizeof wire, "MSG|%s|%s|%s", NANO_ID, to, text);
  putOnAir(wire);
  Serial.print(F("NANO|TX|"));
  Serial.println(wire);
}

void report() {
  if (!bmeOk) bmeOk = bme.begin(0x76) || bme.begin(0x77);

  char text[40];
  if (bmeOk) {
    char t[8], p[9], h[7], a[8];
    dtostrf(bme.readTemperature(), 1, 1, t);
    dtostrf(bme.readPressure() / 100.0f, 1, 1, p);
    dtostrf(bme.readHumidity(), 1, 1, h);
    dtostrf(bme.readAltitude(SEA_LEVEL_HPA), 1, 0, a);
    snprintf(text, sizeof text, "T=%s P=%s H=%s A=%s", t, p, h, a);
  } else {
    strcpy(text, "no sensor");
  }

  sendLine("@antonc", text);
  sendLine("9", text);
}

void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (!inLen) continue;
      inLine[inLen] = 0;
      inLen = 0;
      putOnAir(inLine);
      Serial.println(F("NANO|OK"));
    } else if (inLen < LINE_MAX) {
      inLine[inLen++] = c;
    }
  }
}

void setup() {
  Serial.begin(9600);
  radio.begin(9600);
  pinMode(AUX, INPUT_PULLUP);
  Serial.println(F("NANO|READY"));
  bmeOk = bme.begin(0x76) || bme.begin(0x77);
  Serial.println(bmeOk ? F("NANO|BME280 ok") : F("NANO|BME280 not found"));
  lastReport = millis() - REPORT_MS;  // first report right away
}

void loop() {
  pollAir();
  pollSerial();

  if (millis() - lastReport >= REPORT_MS) {
    lastReport = millis();
    report();
  }
}
