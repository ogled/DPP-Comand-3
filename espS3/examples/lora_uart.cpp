#include <Arduino.h>
#include <Wire.h>

#define PIN_M0  PB0
#define PIN_M1  PA7
#define PIN_AUX PB1 
#define LED     PC13

#define TX_POWER_BITS 0b11

Uart LoRa(PB11, PB10);

bool powerSet = false;

bool waitAux(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (!digitalRead(PIN_AUX)) {
    if (millis() - start > timeoutMs) return false;
  }
  delay(2); 
  return true;
}

void setMode(bool m0, bool m1) {
  digitalWrite(PIN_M0, m0);
  digitalWrite(PIN_M1, m1);
  delay(10);
  waitAux(1000);
}

bool readReply(uint8_t *buf, size_t len) {
  return LoRa.readBytes(buf, len) == len;
}

// Lower TX power before the first transmission (temporary, not saved to module flash)
bool configurePower() {
  setMode(LOW, HIGH);  // M0=0, M1=1 -> configuration mode
  while (LoRa.available()) LoRa.read();

  const uint8_t readCmd[] = {0xC1, 0x04, 0x01};  // read REG1
  LoRa.write(readCmd, sizeof(readCmd));
  uint8_t reply[4];
  bool ok = readReply(reply, 4) && reply[0] == 0xC1;

  if (ok) {
    uint8_t reg1 = (reply[3] & ~0x03) | TX_POWER_BITS;
    const uint8_t writeCmd[] = {0xC2, 0x04, 0x01, reg1};  // C2 = temporary write
    LoRa.write(writeCmd, sizeof(writeCmd));
    ok = readReply(reply, 4) && reply[0] == 0xC1 && reply[3] == reg1;
  }

  setMode(LOW, LOW);  // normal mode
  return ok;
}

void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
  pinMode(PIN_M0, OUTPUT);
  pinMode(PIN_M1, OUTPUT);
  pinMode(PIN_AUX, INPUT);

  LoRa.begin(9600);  // config mode only works at 9600 8N1
  LoRa.setTimeout(200);
  Wire.begin();      // I2C1: SDA=PB7, SCL=PB6

  powerSet = configurePower();
}

void loop() {
  if (!powerSet) {  // fast blink = couldn't lower TX power, don't transmit at 22 dBm
    digitalWrite(LED, !digitalRead(LED));
    delay(100);
    return;
  }

  if (!waitAux(1000)) return;
  digitalWrite(LED, LOW);
  LoRa.println("GLEBOTRON online");
  LoRa.flush();
  waitAux(1000);
  digitalWrite(LED, HIGH);
  delay(10000);
}
