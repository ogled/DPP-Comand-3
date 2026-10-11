#include "Arduino.h"
#include "RadioLib.h"
#include "STM32LowPower.h"
#include <HardwareTimer.h>

SX1268 radio = new Module(A4, A1, PB0, A2); // NSS, DIO1, RESET, BUSY
int32_t  longitude = 52031700;
int32_t  latitude  = 113501000;
uint16_t ID = 2;
uint8_t  packetId = 0;
uint8_t sendTo = 0;

volatile bool radioReceived = false;
volatile bool serialReceived = false;
volatile bool timerFlag = false;

#define PIN_TXEN PB8
#define PIN_RXEN PB9
#define STANDE_FREQUENCY 450.0
#define OUTPUT_POWER 0.1
#define MARK_LOG    0x01
#define MARK_DATA   0x02
#define TIMER_PERIOD_US   10000000UL
#define TIMER_INSTANCE    TIM3

struct __attribute__((packed)) Packet {
  uint8_t deviceId = ID;
  int32_t longitude = longitude;
  int32_t latitude  = latitude;
  uint8_t type = 0;
  // 0 - i2c, 1 - spi, 2 - uart, 3 - digital, 4 - analog
  // 5 - privi2c, 6 - privSpi, 7 - privDig, 8 - privAnalog
  uint8_t typeInput : 4;
  // i2c:
  // spi:
  // uart: Serial
  // digital: 
  // analog:
  uint8_t packetNumber : 4;
  uint8_t size_data = 0;

  // uint8_t толкьо для private, с номером куда пакет
  };

void onRadioInterrupt(void) {
  radioReceived = true;
}

void onSerialInterrupt(void) {
  serialReceived = true;
}
void onTimerInterrupt(void)  { 
  timerFlag = true; 
}
void logMsg(const char* msg) {
    Serial.write(MARK_LOG);
    Serial.write((const uint8_t*)msg, strlen(msg));
    Serial.write('\n');
}

HardwareTimer *timer = nullptr;

void setupTimer() {
    timer = new HardwareTimer(TIMER_INSTANCE);
    timer->setOverflow(TIMER_PERIOD_US, MICROSEC_FORMAT);
    timer->attachInterrupt(onTimerInterrupt);
    timer->resume();
}

void logMsg(const String& msg) {
    Serial.write(MARK_LOG);
    Serial.write((const uint8_t*)msg.c_str(), msg.length());
    Serial.write('\n');
}
void setup()
{
  pinMode(PIN_TXEN, OUTPUT);
  pinMode(PIN_RXEN, OUTPUT);
  pinMode(PA11, INPUT);
  pinMode(PA12, INPUT);
  digitalWrite(PIN_TXEN, LOW);
  digitalWrite(PIN_RXEN, LOW);

  LowPower.begin();
  Serial.begin(9600);
  logMsg("[INFO] Starting up...");

  int state = radio.begin(STANDE_FREQUENCY);
  if (state == RADIOLIB_ERR_NONE)
  {
    logMsg("[INFO] Radio init");
  }
  else
  {
    logMsg("[ERROR] Radio init failed, code: " + String(state));
    __NVIC_SystemReset();
  }
  radio.setRfSwitchPins(PIN_RXEN, PIN_TXEN);
  state = radio.setOutputPower(OUTPUT_POWER);
  if (state != RADIOLIB_ERR_NONE) {
    logMsg(F("[ERROR] Failed to set output power!"));
  }
  radio.setPreambleLength(8);
  radio.setSyncWord(0x12);
  radio.setCRC(2);
  radio.setDio1Action(onRadioInterrupt);

  LowPower.attachInterruptWakeup(A1, onRadioInterrupt, RISING);
  LowPower.attachInterruptWakeup(PA10, onSerialInterrupt, FALLING);

  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    logMsg("[ERROR] Failed to start receive mode!");
  }

  setupTimer();
}

class Sender
{
public:
  Packet packet;
  uint8_t loraBuffer[64];
  uint8_t cursor = 0;
  void createTitle()
  {
    packet.packetNumber = packetId++;
    memcpy(&loraBuffer[cursor], &packet, sizeof(packet));
    cursor += sizeof(packet);
    payload_start = cursor;
  }
  void addData(uint8_t* data, size_t length)
  {
    memcpy(&loraBuffer[cursor], data, length);
    cursor += length;
  }
  void setType(uint8_t type)
  {
    packet.type = type;
  }
  void setTypeInput(uint8_t typeInput)
  {
    packet.typeInput = typeInput;
  }
  void finalize()
  {
    packet.size_data = cursor - payload_start;
    loraBuffer[offsetof(Packet, size_data)] = packet.size_data;
  }
  int send()
  {
    return 0; //radio.transmit(loraBuffer, cursor);
  }
private:
  uint8_t payload_start;
};

void loop()
{
  if (serialReceived) {
    serialReceived = false;
    String input = Serial.readStringUntil('\n');
    if(input.substring(0, 2) == "~:")
    {
      sendTo = input.substring(3, 6).toInt();
      input = input.substring(6);
    }
    Sender sender;
    sender.setType(2);
    sender.setTypeInput(0);
    sender.createTitle();
    sender.addData((uint8_t*)input.c_str(), input.length());
    sender.finalize();
    int state = sender.send();
    if (state == RADIOLIB_ERR_NONE) {
      logMsg("[INFO] Packet sent successfully!");
    }
    else {
      logMsg("[ERROR] Packet send failed, code: " + String(state));
    }
    radio.startReceive();
  }
  if(radioReceived)
  {
    radioReceived = false;
    uint8_t rxBuffer[64];
    size_t length = radio.getPacketLength();
    if (length > sizeof(rxBuffer)) length = sizeof(rxBuffer);
    
    int state = radio.readData(rxBuffer, length);
    
    if (state == RADIOLIB_ERR_NONE) {
      logMsg("[INFO] Antenna package received!");
      Serial.write(MARK_DATA);
      Serial.write((uint8_t)(length & 0xFF));
      Serial.write((uint8_t)((length >> 8) & 0xFF));
      Serial.write(rxBuffer, length);
    } else {
      logMsg("[ERROR] Radio read error, code: " + String(state));
    }
    radio.startReceive();
  }
  if(timerFlag)
  {
    timerFlag = false;
  }
  Serial.flush();
  LowPower.sleep(); 
}