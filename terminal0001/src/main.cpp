#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

#define MARK_LOG 0x01

Uart LoRa(PB11, PB10);
Adafruit_BME280 bme;

bool bmeOk = false;
uint8_t packetId = 0;
constexpr uint8_t DEVICE_ID = 1;
constexpr int32_t LONGITUDE = 55755819;
constexpr int32_t LATITUDE = 37617644;

struct __attribute__((packed)) Packet {
  uint8_t deviceId;
  int32_t longitude;
  int32_t latitude;
  uint8_t type;
  uint8_t typeInput : 4;
  uint8_t packetNumber : 4;
  uint8_t size_data;
};

static_assert(sizeof(Packet) == 12, "Invalid packet header size");

void logMsg(const char* msg) {
  Serial.write(MARK_LOG);
  Serial.write((const uint8_t*)msg, strlen(msg));
  Serial.write('\n');
}

void setup() {
  pinMode(PB0, OUTPUT);
  pinMode(PA7, OUTPUT);
  digitalWrite(PB0, LOW);
  digitalWrite(PA7, LOW);

  pinMode(PB1, INPUT);

  Serial.begin(9600);
  LoRa.begin(9600);

  Wire.setSCL(PB8);
  Wire.setSDA(PB9);
  Wire.begin();

  bmeOk = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);

  logMsg("[INFO] Starting up...");
  logMsg(bmeOk ? "[INFO] BME280 initialized" : "[ERROR] BME280 not found");
  logMsg("[INFO] UART LoRa initialized");
}

void loop() {
  char msg[53];

  if (bmeOk) {
    float t = bme.readTemperature();
    float p = bme.readPressure() / 100.0f;
    float h = bme.readHumidity();

    snprintf(
      msg,
      sizeof(msg),
      "GLEBOTRON T=%.2fC P=%.2fhPa H=%.1f%%",
      t, p, h
    );
  } else {
    snprintf(msg, sizeof(msg), "GLEBOTRON BME280 not found");
  }

  logMsg(msg);

  while (!digitalRead(PB1)) {
    delay(1);
  }

  Packet packet{};
  packet.deviceId = DEVICE_ID;
  packet.longitude = LONGITUDE;
  packet.latitude = LATITUDE;
  packet.type = 0;
  packet.typeInput = 0;
  packet.packetNumber = packetId & 0x0F;

  uint8_t txBuffer[64];

  size_t headerSize = sizeof(Packet);
  size_t payloadSize = strlen(msg);

  if (payloadSize > sizeof(txBuffer) - headerSize) {
    payloadSize = sizeof(txBuffer) - headerSize;
  }

  packet.size_data = payloadSize;

  memcpy(txBuffer, &packet, headerSize);
  memcpy(txBuffer + headerSize, msg, payloadSize);

  LoRa.write(txBuffer, headerSize + payloadSize);
  LoRa.flush();

  packetId = (packetId + 1) & 0x0F;

  logMsg("[INFO] Packet written to UART LoRa");

  delay(10000);
}