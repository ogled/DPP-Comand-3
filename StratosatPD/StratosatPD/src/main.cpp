#include <RadioLib.h>

#define RADIO_NSS   PA4
#define RADIO_DIO0  PA1
#define RADIO_NRST  PB0
#define RADIO_DIO1  PA2
#define RADIO_TXEN  PB10
#define RADIO_RXEN  PB11
#define LED    PC13
#define PIN_TXEN PB8
#define PIN_RXEN PB9

SX1278 radio = new Module(
    PA4,  // NSS
    PA2,  // DIO0
    PB10,  // NRST
    RADIOLIB_NC
);
int radioState;
void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);  // off

  radioState = radio.begin(433.0);
  if (radioState == RADIOLIB_ERR_NONE) {
    radio.setRfSwitchPins(PIN_RXEN, PIN_TXEN);
    digitalWrite(LED, LOW);  // solid on = radio OK
  }
}

void loop() {
  if (radioState != RADIOLIB_ERR_NONE) {  // fast blink = init failed
    digitalWrite(LED, !digitalRead(LED));
    delay(100);
  }
}