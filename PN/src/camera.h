// The onboard camera (GOOUUU ESP32-S3-CAM, OV2640/OV3660/OV5640), set up
// as in the CAMERAesp32 sketch. The sensor sleeps between photos.
#pragma once
#include <Arduino.h>
#include <vector>

namespace camera {
// Takes one small JPEG (240x176) that fits in IMG_MAX_BYTES, lowering the
// quality until it does. Blocks for about three seconds while the sensor
// settles. Returns false if the camera does not answer.
bool capture(std::vector<uint8_t>& jpeg, uint16_t& width, uint16_t& height);
}
