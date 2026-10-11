#include <STM32SD.h>
#include <LSM6DS3.h>
#include <LIS3MDL.h>
#include <MS5611.h>
#include <LM75A.h>
#include <STM32SD.h>

void log(const String &value);
void log(const char *value);
void log(const __FlashStringHelper *value);

namespace Mission
{
    inline bool PayloadReady() { return false; }
    inline bool InEclipse() { return false; }
    inline unsigned long TimeSinceDeploy() { return millis(); }
    inline float Latitude() { return 0.0f; }
    inline float Longitude() { return 0.0f; }
    inline float Altitude() { return 0.0f; }

// Параметры спутника
#define USER_LED_PIN PE10
#define SD_DETECT_PIN PB8

#define X_PLUS_PIN PB11 // TODO
#define X_MINUS_PIN PB12
#define Y_PLUS_PIN PB13
#define Y_MINUS_PIN PB14
    File logFile;
    bool sd_active = true;

    Uart LoraSerial(PA10, PA9); // TODO
    Uart UsbSerial(PA3, PA2);

    bool heaveNotReadingMsg = 0;
    uint8_t data_frome_earth[64] = {0};
    uint8_t earth_msg_size = 0;
    uint8_t data[64] = {0};
    uint8_t versionHeadline = 0;
    uint8_t maxCLCWVersion = 0;

    uint16_t sourceIdDataUsefullData = 1;
    bool IsPayloadReady = true;

    uint16_t sourceIdDataTelemetryData = 0;
#define TIME_TELEMETRY 30000
    unsigned long timeTelinetry = TIME_TELEMETRY;
    unsigned long lastTimeTelemetrySend = 0;

    TwoWire Wire1(PB7, PB6);   // Шина I2C1 - системная
    TwoWire Wire4(PD13, PD12); // Шина I2C2 - полезная нагрузка

    LSM6DS3 sm(Wire1, 0x6A);   // Гироскоп и акселерометр
    LIS3MDL magn(Wire1, 0x1C); // Магнитрометр
    MS5611 bar(Wire1, 0x77);   // Барометр
    LM75A temp(Wire1, 0x4A);   // Датчик температуры на плате
    LM75A temp2(Wire1, 0x4C);  // Датчик температуры на плате связи
    LM75A temp3(Wire4, 0x48);  // Датчик температуры аккумулятора

    uint8_t mode = 0; /*
    0 - Дежурный     - базовый режим
    1 - Аварийный    - защита аппарата
    2 - Обслуживание - консоль
    */
    uint8_t lastMode = 0;
    unsigned long last_check_health_time = 0;
    unsigned long freq_cheal_health = 5000;
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

        void _send(uint8_t *buffer, uint8_t length, bool isTelemetry = false)
        {
            if (buffer == NULL || length == 0 || length > 58)
            {
                log("[ERROR] Invalid buffer or length");
                return;
            }
            for (uint8_t i = 0; i < length; i++)
            {
                LoraSerial.write(buffer[i]);
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
}