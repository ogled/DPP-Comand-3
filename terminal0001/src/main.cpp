#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

Uart LoRa(PB11, PB10);
Adafruit_BME280 bme;
bool bmeOk = false;

void setup() {
  pinMode(PB0, OUTPUT);
  pinMode(PA7, OUTPUT);
  digitalWrite(PB0, LOW);
  digitalWrite(PA7, LOW);
  pinMode(PB1, INPUT);
  LoRa.begin(9600);
  Serial.begin(115200);

  Wire.setSCL(PB8);
  Wire.setSDA(PB9);
  Wire.begin();
  bmeOk = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);
}

void loop() {
  char msg[64];
  if (bmeOk) {
    float t = bme.readTemperature();
    float p = bme.readPressure() / 100.0f;
    float h = bme.readHumidity();
    snprintf(msg, sizeof(msg), "GLEBOTRON T=%.2fC P=%.2fhPa H=%.1f%%", t, p, h);
  } else {
    snprintf(msg, sizeof(msg), "GLEBOTRON BME280 not found");
  }
  Serial.println(msg);

  while (!digitalRead(PB1));
  LoRa.println(msg);
  delay(10000);
}
