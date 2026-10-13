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
constexpr uint8_t TYPE_I2C = 0;
constexpr uint8_t TYPE_INPUT = 0;

constexpr size_t HEADER_SIZE = 12;
constexpr size_t PAYLOAD_SIZE = 13;
const char NO_SENSOR_MSG[] = "no sensor";
constexpr size_t NO_SENSOR_SIZE = sizeof(NO_SENSOR_MSG) - 1;

static_assert(sizeof(float) == 4, "Float must be 4 bytes");

void putUInt32LE(uint8_t* buffer, size_t& pos, uint32_t value) {
  buffer[pos++] = value & 0xFF;
  buffer[pos++] = (value >> 8) & 0xFF;
  buffer[pos++] = (value >> 16) & 0xFF;
  buffer[pos++] = (value >> 24) & 0xFF;
}

void putFloatLE(uint8_t* buffer, size_t& pos, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  putUInt32LE(buffer, pos, bits);
}

void setup() {
  pinMode(PB0, OUTPUT);
  pinMode(PA7, OUTPUT);
  digitalWrite(PB0, LOW);
  digitalWrite(PA7, LOW);

  pinMode(PB1, INPUT);

  LoRa.begin(9600);

  Wire.setSCL(PB8);
  Wire.setSDA(PB9);
  Wire.begin();

  bmeOk = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);
}

void loop() {
  uint8_t txBuffer[HEADER_SIZE + PAYLOAD_SIZE];
  size_t pos = 0;

  if (!bmeOk) {
    bmeOk = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);
  }

  txBuffer[pos++] = DEVICE_ID;

  putUInt32LE(txBuffer, pos, (uint32_t)LONGITUDE);
  putUInt32LE(txBuffer, pos, (uint32_t)LATITUDE);

  txBuffer[pos++] = TYPE_I2C;
  txBuffer[pos++] = ((packetId & 0x0F) << 4) | (TYPE_INPUT & 0x0F);
  txBuffer[pos++] = bmeOk ? PAYLOAD_SIZE : NO_SENSOR_SIZE;

  if (bmeOk) {
    txBuffer[pos++] = 1;
    putFloatLE(txBuffer, pos, bme.readTemperature());
    putFloatLE(txBuffer, pos, bme.readPressure() / 100.0f);
    putFloatLE(txBuffer, pos, bme.readHumidity());
  } else {
    memcpy(&txBuffer[pos], NO_SENSOR_MSG, NO_SENSOR_SIZE);
    pos += NO_SENSOR_SIZE;
  }

  while (!digitalRead(PB1)) {
    delay(1);
  }

  for (size_t i = 0; i < pos; i++) {
    LoRa.write(txBuffer[i]);
  }

  LoRa.flush();

  packetId = (packetId + 1) & 0x0F;

  delay(10000);
}

