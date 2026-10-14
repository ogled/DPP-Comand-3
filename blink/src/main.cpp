#include <Arduino.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 2  // the on-board LED of most ESP32 dev boards
#endif

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("LED on");
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("LED off");
  delay(500);
}
