#include <Arduino.h>
#include <Wire.h>
#define DEBUG_ACTIVE true
#include <LSM6DS3.h>
#include <LIS3MDL.h>
#include <MS5611.h>
#include <LM75A.h>
#include <STM32SD.h>
#include <STM32LowPower.h>

using namespace IntroStratLib;

void log(const String &value);
void log(const char *value);
void log(const __FlashStringHelper *value);
bool PayloadReady()
{
    return true;
}
uint16_t getVol();
#define USER_LED_PIN PE10
#define SD_DETECT_PIN PB8

#define X_PLUS_PIN PB11 // TODO
#define X_MINUS_PIN PB12
#define Y_PLUS_PIN PB13
#define Y_MINUS_PIN PB14

uint8_t error_code = 0; // Коды ошибок:
    /*
    0 - ОК
    1 - Температура на одном из датчиков слишком низка
    2 - Температура на одном из датчиков слишком высока
    3 - Ошибка при инициализации гироскопа
    4 - Ошибка при инициализации акселерометра
    5 - Ошибка при инициализации магнитрометра
    6 - Ошибка при инициализации барометра
    7 - Ошибка при инициализации датчика температуры на плате контроллера
    8 - Ошибка при инициализации датчика температуры на плате связи
    9 - Ошибка на полезной нагрузке
    10 - Обрыв шины на акселерометре-гироскопе
    11 - Высокое напряжение
    12 - Низкое напряжение
    */
volatile bool loraWakeFlag = false;
void onLoraWake()
{
  loraWakeFlag = true;
}
volatile bool loraSleepFlag = false;

bool sd_active = true;
bool heaveNotReadingMsg = 0;
uint8_t data_frome_earth[58] = {0};
uint8_t earth_msg_size = 0;
uint8_t data[58] = {0};
File logFile;
TwoWire Wire1(PB7, PB6);   // Шина I2C1 - системная
TwoWire Wire4(PD13, PD12); // Шина I2C2 - полезная нагрузка

LSM6DS3 sm(Wire1, 0x6A);   // Гироскоп и акселерометр
LIS3MDL magn(Wire1, 0x1C); // Магнитрометр
MS5611 bar(Wire1, 0x77);   // Барометр
LM75A temp(Wire1, 0x4A);   // Датчик температуры на плате
LM75A temp2(Wire1, 0x4C);  // Датчик температуры на плате связи

Uart Conection(PA10, PA9);
Uart UsbSerial(PA10, PA9); // TODO
uint8_t mode = 0; /*
    0 - Дежурный     - базовый режим
    1 - Аварийный    - защита аппарата
    2 - Обслуживание - консоль
    */
uint8_t lastMode = 0;
unsigned long last_check_health_time = 0;
unsigned long freq_cheal_health = 5000;
uint16_t sourceIdDataUsefullData = 1;
bool IsPayloadReady = true;

uint16_t sourceIdDataTelemetryData = 0;
#define TIME_TELEMETRY 30000
unsigned long timeTelinetry = TIME_TELEMETRY;
unsigned long lastTimeTelemetrySend = 0;

struct SunRes
{
  float angle;
  float intensity;
};

inline uint16_t getVol()
{
  return static_cast<uint16_t>(14.8f * 100.0f);
}
unsigned long TimeSinceDeploy()
{
    return millis();
}
class dataSender
    {
    private:
        uint16_t msgCounter = 0;
        uint16_t packetCounter = 0;     // Счетчик пакетов
        bool waitForCLCW = false;       // Ждем подтверждение?
        uint8_t currentChunk = 0;       // Текущий отправляемый чанк
        uint8_t totalChunks = 0;        // Всего чанков
        uint8_t *currentData = NULL;    // Указатель на отправляемые данные
        uint8_t currentDataLength = 0;  // Длина отправляемых данных
        uint16_t currentAPID = 0;       // APID текущего сообщения
        unsigned long lastSendTime = 0; // Время последней отправки
        unsigned long timeout = 2000;   // Таймаут ожидания подтверждения
        bool sendingInProgress = false; // Идет ли процесс отправки?
        uint8_t versionHeadline = 0;
        uint8_t maxCLCWVersion = 0;

        void _send(uint8_t *buffer, uint8_t length, bool isTelemetry = false)
        {
            if (buffer == NULL || length == 0 || length > 58)
            {
                log("[ERROR] Invalid buffer or length");
                return;
            }
            for (uint8_t i = 0; i < length; i++)
            {
                Conection.write(buffer[i]);
            }
            if (!isTelemetry)
            {
                lastSendTime = millis();
            }
        }

        void createTitle(uint8_t *inputPacket, uint8_t *outputPacket, uint8_t length, uint16_t APID, uint8_t Flag) // Создание заголовка, насчет то, что за что отвечает, смотрите в пояснительной записке
        {
            if (length > 52)
            {
                if (DEBUG_ACTIVE)
                {
                    log("[ERROR] Data length > 52 bytes, cannot add header");
                }
                return;
            }

            uint16_t word1 = 0;
            word1 |= (versionHeadline & 0x07) << 13;

            if (APID == sourceIdDataTelemetryData)
            {
                word1 |= (0 << 12);
            }
            else
            {
                word1 |= (1 << 12);
            }

            word1 |= (0 << 11);
            word1 |= (APID & 0x7FF);

            uint16_t word2 = 0;
            word2 |= (Flag & 0x03) << 14;
            if (APID == sourceIdDataTelemetryData)
            {
                word2 |= (0 & 0x3FFF); // Телеметрия будет всегда без индекса
            }
            else
            {
                word2 |= (packetCounter & 0x3FFF);
            }

            uint16_t word3 = length - 1;

            outputPacket[0] = (word1 >> 8) & 0xFF;
            outputPacket[1] = word1 & 0xFF;
            outputPacket[2] = (word2 >> 8) & 0xFF;
            outputPacket[3] = word2 & 0xFF;
            outputPacket[4] = (word3 >> 8) & 0xFF;
            outputPacket[5] = word3 & 0xFF;

            for (uint8_t i = 0; i < length; i++)
            {
                outputPacket[6 + i] = inputPacket[i];
            }
        }

        // Отправка одного чанка
        void sendChunk(uint8_t chunkIndex)
        {
            if (currentData == NULL || chunkIndex >= totalChunks)
            {
                return;
            }

            uint8_t offset = chunkIndex * 52;
            uint8_t remaining = currentDataLength - offset;
            uint8_t chunkSize = (remaining > 52) ? 52 : remaining;

            uint8_t chunkData[52] = {0};

            for (uint8_t i = 0; i < chunkSize; i++)
            {
                chunkData[i] = currentData[offset + i];
            }

            // Определяем флаг
            uint8_t flag = 0;

            if (totalChunks == 1)
            {
                flag = 3; // 11 — пакет не сегментирован
            }
            else if (chunkIndex == 0)
            {
                flag = 1; // 01 — первый
            }
            else if (chunkIndex == totalChunks - 1)
            {
                flag = 2; // 10 — последний
            }
            else
            {
                flag = 0; // 00 — продолжение
            }

            // Создаем пакет
            uint8_t packet[58] = {0};
            createTitle(chunkData, packet, chunkSize, currentAPID, flag);

            // Отправляем
            _send(packet, chunkSize + 6);

            // Ждем подтверждение
            waitForCLCW = true;
            log("[LOG] MSG send");
        }

        uint8_t isAcknowledgment(uint8_t *clcwBuffer, uint8_t length) // Функция, которая проверяет пакет с Земли: правда ли Земля получила сообщение?
        {
            if (clcwBuffer == NULL || length < 4)
            {
                return 2;
            }

            bool requestRepeat = (clcwBuffer[2] >> 3) & 0x01;

            uint8_t cop = clcwBuffer[0] & 0x03; // Проверяем COP-1, если 01 ОК, если нет, это сообщение не нам
            if (cop != 1)
            {
                return 2;
            }

            uint8_t version = (clcwBuffer[0] >> 5) & 0x03;
            if (version > maxCLCWVersion)
            {
                log("[ERROR] CLCW BIG VERSION");
                return 3;
            }

            return requestRepeat;
        }

        void resendLastMessage() // Реотправка последнего пакета
        {
            if (waitForCLCW)
            {
                log("[REPEAT] Resending last chunk");

                if (sendingInProgress)
                {
                    sendChunk(currentChunk);
                }
            }
            else
            {
                log("[REPEAT] No message to resend");
            }
        }

    public:
        // Начать отправку больших данных (по чанкам и с подтверждением)
        void sendBigData(uint8_t *buffer, uint8_t length, uint16_t APID, bool isTelemetry = false)
        {
            if (buffer == NULL || length == 0)
            {
                log("[ERROR] sendBigData: Invalid buffer");
                return;
            }

            if (sendingInProgress)
            {
                log("[ERROR] Already sending, wait for completion");
                return;
            }
            if (isTelemetry)
            {
                uint8_t out[58] = {0};
                createTitle(buffer, out, length, APID, 3);
                _send(out, length + 6, true);
                return;
            }
            // Сохраняем данные для отправки
            currentData = buffer;
            currentDataLength = length;
            currentAPID = APID;
            currentChunk = 0;
            totalChunks = (length + 51) / 52;
            sendingInProgress = true;
            waitForCLCW = false;

            sendChunk(0);
        }

        // Обработка подтверждения с Земли
        void confirmReceived()
        {
            if (!waitForCLCW)
                return;

            waitForCLCW = false;

            if (DEBUG_ACTIVE)
            {
                UsbSerial.println("[CLCW] Received!");
            }

            // Переходим к следующему чанку
            currentChunk++;

            if (currentChunk < totalChunks)
            {
                packetCounter = (packetCounter + 1) & 0x3FFF;

                // Отправляем следующий чанк
                sendChunk(currentChunk);
            }
            else
            {
                // Все чанки отправлены
                msgCounter = (msgCounter + 1) & 0x3FFF;
                sendingInProgress = false;

                if (DEBUG_ACTIVE)
                {
                    UsbSerial.println("[COMPLETE] All chunks sent!");
                }
            }
        }

        // Обновление состояния
        uint8_t update()
        {
            if (heaveNotReadingMsg)
            {
                uint8_t v = isAcknowledgment(data_frome_earth, earth_msg_size);

                if (v == 1 || v == 0)
                {
                    if (v == 0)
                    {
                        confirmReceived();
                    }
                    else
                    {
                        resendLastMessage();
                    }
                }
                else
                {
                    return v;
                }

                heaveNotReadingMsg = false;
            }

            if (waitForCLCW && (millis() - lastSendTime > timeout)) // Отправляем текущий чанк, если сообщения долго нет
            {
                log("[TIMEOUT] No CLCW, resending chunk...");

                if (sendingInProgress)
                {
                    sendChunk(currentChunk);
                }
                else
                {
                    waitForCLCW = false;
                }
            }

            return 0;
        }

        bool isSending()
        {
            return sendingInProgress;
        }
    };

SunRes calculateSun()
{
    int xPlus = digitalRead(X_PLUS_PIN);
    int xMinus = digitalRead(X_MINUS_PIN);
    int yPlus = digitalRead(Y_PLUS_PIN);
    int yMinus = digitalRead(Y_MINUS_PIN);
    int sx = static_cast<int>(xPlus) - static_cast<int>(xMinus);
    int sy = static_cast<int>(yPlus) - static_cast<int>(yMinus);

    float angleRad = std::atan2(static_cast<float>(sy), static_cast<float>(sx));
    float angleDeg = angleRad * 180.0f / 3.14159265358979323846f;
    if (angleDeg < 0.0f) {
        angleDeg += 360.0f;
    }
    float intensity = std::sqrt(static_cast<float>(sx * sx + sy * sy));
    if (intensity > 255.0f) {
        intensity = 255.0f;
    }
    return { angleDeg, intensity };
}

dataSender Sender;
bool cheackHealth()
{
  error_code = 0;
  bool is_healthy = true;

  float t1 = temp.GetTemperature();
  float t2 = temp2.GetTemperature();
  if (t1 < -20.0 || t2 < -20.0)
  {
    log("[CRITICAL] Temperature low of limit! T1: " + String(t1) + " T2: " + String(t2));
    is_healthy = false;
    error_code = 1;
  }
  else if (t1 > 35.0 || t2 > 35)
  {
    log("[CRITICAL] Temperature high of limit! T1: " + String(t1) + " T2: " + String(t2));
    is_healthy = false;
    error_code = 2;
  }

  float accelX = sm.AX();
  float accelY = sm.AY();
  float accelZ = sm.AZ();
  float totalAccel = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);

  if (totalAccel < 0.1 || totalAccel > 16.0)
  {
    log("[ERROR] Accel out of limits: " + String(totalAccel));
    is_healthy = false;
    error_code = 13;
  }

  if (getVol() >= 16)
  {
    error_code = 14;
  }
  else if (getVol() < 10)
  {
    error_code = 15;
  }
  return is_healthy;
}

void setup()
{
  pinMode(USER_LED_PIN, OUTPUT);
  digitalWrite(USER_LED_PIN, HIGH);
  SD.setDx(PC8, PC9, PC10, PC11);
  SD.setCMD(PD2);
  SD.setCK(PC12);
  if (!SD.begin())
  {
    sd_active = false;
  }
  else
  {
    logFile = SD.open("datalog.txt", FILE_WRITE);
  }
  if (DEBUG_ACTIVE)
  {
    UsbSerial.begin(115200, SERIAL_8E1);
  }
  Conection.begin(115200);
  log("[OK] UART1 initialized");
  Wire4.begin();
  Wire1.begin();

  log("[OK] I2C initialized");

  if (!sm.InitGyro())
  {
    error_code = 5;
    log("[ERROR] Gyro no init");
  }
  if (!sm.InitAccel())
  {
    error_code = 6;
    log("[ERROR] Acel no init");
  }
  if (!magn.Init())
  {
    error_code = 7;
    log("[ERROR] Magn no init");
  }
  if (!bar.Init())
  {
    error_code = 8;
    log("[ERROR] Bar no init");
  }
  if (!temp.Init())
  {
    error_code = 9;
    log("[ERROR] Temp no init");
  }
  if (!temp2.Init())
  {
    error_code = 10;
    log("[ERROR] Temp2 no init");
  }
  LowPower.begin();
  pinMode(USER_LED_PIN, OUTPUT);
  digitalWrite(USER_LED_PIN, LOW);
  pinMode(X_PLUS_PIN, INPUT);
  pinMode(X_MINUS_PIN, INPUT);
  pinMode(Y_PLUS_PIN, INPUT);
  pinMode(Y_MINUS_PIN, INPUT);
  LowPower.begin();
  if (error_code == 0)
  {
    log("[OK] All systems init");
  }
  else
  {
    log("[ERROR] Initialization failed");
    lastMode = 255;
    mode = 1;
  }
}
void consumeCommandByte(uint8_t byte) // todo
{
  static uint8_t commandBuffer[58];
  static uint8_t commandIndex = 0;

  commandBuffer[commandIndex++] = byte;

  if (commandIndex >= sizeof(commandBuffer))
  {
    log("[ERROR] Command buffer overflow");
    commandIndex = 0;
    return;
  }

  if (byte == '\n') // Assuming commands end with a newline character
  {
    // Process the command
    log("[COMMAND] Received: " + String((char *)commandBuffer));
    // Reset the index for the next command
    commandIndex = 0;
  }
}
void sendTelemetry() // TODO
{
  digitalWrite(USER_LED_PIN, HIGH);
  uint8_t res[49];
  res[0] = (sm.RawGX() >> 8) & 0xFF;
  res[1] = sm.RawGX() & 0xFF; // Гироскоп по X
  res[2] = (sm.RawGY() >> 8) & 0xFF;
  res[3] = sm.RawGY() & 0xFF; // Гироскоп по Y
  res[4] = (sm.RawGZ() >> 8) & 0xFF;
  res[5] = sm.RawGZ() & 0xFF; // Гироскоп по Z

  res[6] = (sm.RawAX() >> 8) & 0xFF;
  res[7] = sm.RawAX() & 0xFF; // Акселерометр по X
  res[8] = (sm.RawAY() >> 8) & 0xFF;
  res[9] = sm.RawAY() & 0xFF; // Акселерометр по Y
  res[10] = (sm.RawAZ() >> 8) & 0xFF;
  res[11] = sm.RawAZ() & 0xFF; // Акселерометр по Z

  res[12] = (magn.RawMX() >> 8) & 0xFF;
  res[13] = magn.RawMX() & 0xFF; // Магнитрометр по X
  res[14] = (magn.RawMY() >> 8) & 0xFF;
  res[15] = magn.RawMY() & 0xFF; // Магнитрометр по Y
  res[16] = (magn.RawMZ() >> 8) & 0xFF;
  res[17] = magn.RawMZ() & 0xFF; // Магнитрометр по Z

  uint32_t t = bar.GetRawTemperature(); // Температура в барометре
  res[18] = (t >> 24) & 0xFF;
  res[19] = (t >> 16) & 0xFF;
  res[20] = (t >> 8) & 0xFF;
  res[21] = t & 0xFF;
  t = bar.GetRawPressure(); // Давление в барометре
  res[22] = (t >> 24) & 0xFF;
  res[23] = (t >> 16) & 0xFF;
  res[24] = (t >> 8) & 0xFF;
  res[25] = t & 0xFF;
  res[26] = (uint16_t(bar.GetHeight()) >> 8) & 0xFF; // Высота по мнению барометра
  res[27] = uint16_t(bar.GetHeight()) & 0xFF;

  res[28] = (temp.GetTemperatureTimes8() >> 8) & 0xFF; // Температура 1 датчика температуры
  res[29] = temp.GetTemperatureTimes8() & 0xFF;

  res[30] = (temp2.GetTemperatureTimes8() >> 8) & 0xFF; // Температура 2 датчика температуры
  res[31] = temp2.GetTemperatureTimes8() & 0xFF;

  res[32] = error_code;                                                                      // Код ошибки в норме 0
  res[33] = mode;                                                                            // Текущий режим работы
  res[34] = (sd_active ? 0x80 : 0) | (PayloadReady() ? 0x40 : 0);

  res[35] = (getVol() >> 8) & 0xFF; // Напряжение на аккумеляторе
  res[36] = getVol() & 0xFF;

  Sender.sendBigData(res, 49, sourceIdDataTelemetryData, true);
  digitalWrite(USER_LED_PIN, LOW);
}
void loop() {
  Conection.println("ss");
}
/* void loop()
{
  if (mode != 0 && mode != 4 && mode != 3)
  {
    if (last_check_health_time == 0 || millis() - last_check_health_time >= freq_cheal_health)
    {
      last_check_health_time = millis();
      if (!cheackHealth())
      {
        lastMode = mode;
        mode = 3; // Вход в аварийный режим
      }
    }
  }
  if (lastTimeTelemetrySend == 0 || millis() - lastTimeTelemetrySend >= timeTelinetry)
  {
    lastTimeTelemetrySend = millis();
    sendTelemetry();
  }
  while (Conection.available())
  {
    Sender.update();
    consumeCommandByte(static_cast<uint8_t>(Conection.read()));
  }
} */

void log(const char *value)
{
  if (value == nullptr)
  {
    return;
  }

  if (sd_active && logFile)
  {
    logFile.println(String(TimeSinceDeploy()) + ":" + value);
    logFile.flush();
  }
  if (DEBUG_ACTIVE)
  {
    UsbSerial.println(String(TimeSinceDeploy()) + ":" + value);
  }
}

void log(const String &value)
{
  if (sd_active && logFile)
  {
    logFile.println(String(TimeSinceDeploy()) + ":" + value);
    logFile.flush();
  }
  if (DEBUG_ACTIVE)
  {
    UsbSerial.println(String(TimeSinceDeploy()) + ":" + value);
  }
}

void log(const __FlashStringHelper *value)
{
  if (value == nullptr)
  {
    return;
  }

  if (sd_active && logFile)
  {
    logFile.println(String(TimeSinceDeploy()) + ":" + String(value));
    logFile.flush();
  }
  if (DEBUG_ACTIVE)
  {
    UsbSerial.println(String(TimeSinceDeploy()) + ":" + String(value));
  }
}