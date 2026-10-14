// Screen blink test: SSD1306 128x64 OLED on I2C.
// Wiring: VCC -> 3V3, GND -> GND, SDA -> GPIO8, SCL -> GPIO9.
// The screen shows a text and flips between normal and inverted every 500 ms.
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SDA_PIN 8
#define SCL_PIN 9
#define OLED_ADDR 0x3C  // some modules use 0x3D

Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool inverted = false;

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 not found: check wiring and address");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 24);
  display.print("BLINK");
  display.display();
}

void loop() {
  inverted = !inverted;
  display.invertDisplay(inverted);
  Serial.println(inverted ? "inverted" : "normal");
  delay(500);
}
