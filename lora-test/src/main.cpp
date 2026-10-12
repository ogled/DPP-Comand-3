#include <Arduino.h>
#include <RadioLib.h>

// E22-400M22S per "terminal dpp" schematic: NSS, DIO1, NRST, BUSY
SX1268 radio = new Module(PA4, PB1, PB3, PB0);

#define PIN_TXEN PB11
#define PIN_RXEN PB10
#define LED      PC13   // Blue Pill LED, LOW = on
#define FREQ     433.0  // MHz, tune SDR# here
#define TX_POWER 0      // dBm, low so a bench supply / ST-Link 3.3V can handle it

volatile int16_t  radioState = 1;  // 1 = begin() not reached yet; read via ST-Link
volatile int16_t  txState = 1;
volatile uint32_t txCount = 0;

void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);  // off

  radioState = radio.begin(FREQ);
  if (radioState == RADIOLIB_ERR_NONE) {
    radio.setRfSwitchPins(PIN_RXEN, PIN_TXEN);
    radio.setOutputPower(TX_POWER);
  }
}

void loop() {
  if (radioState != RADIOLIB_ERR_NONE) {  // fast blink = init failed
    digitalWrite(LED, !digitalRead(LED));
    delay(100);
    return;
  }

  // one packet per second, LED on while transmitting
  digitalWrite(LED, LOW);
  txState = radio.transmit("DPP test");
  digitalWrite(LED, HIGH);
  if (txState == RADIOLIB_ERR_NONE) {
    txCount++;
  }
  delay(1000);
}
