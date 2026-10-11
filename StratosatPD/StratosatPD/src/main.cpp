#include <Arduino.h>

Uart Serial1(PA10, PA9);

void setup() {
    Serial1.begin(115200, SERIAL_8E1);
}

void loop() {
    Serial1.print("Hello world!"); 

    delay(1000);
}