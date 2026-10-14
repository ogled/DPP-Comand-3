#include "camera.h"
#include "esp_camera.h"
#include "satproto.h"

// Camera pins, from the GOOUUU ESP32-S3-CAM pinout. None of them is
// shared with the LoRa module (1, 2, 14).
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5
#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

namespace {

// 240x176: a photo has to cross a link of a few hundred bytes a second,
// and every piece lost has to be asked for again.
const framesize_t FRAME = FRAMESIZE_HQVGA;
// JPEG quality (0-63, lower is better), tried best first until the photo
// fits in IMG_MAX_BYTES. Small steps at the top, so the photo comes out
// as close to the limit, and as sharp, as it can.
const int QUALITY_STEPS[] = {10, 11, 12, 13, 14, 15, 16, 18, 20, 23, 26, 30, 35, 45, 63};
// Exposure and white balance need a moment after waking.
const uint32_t SETTLE_MS = 2500;

bool start() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAME;
  c.jpeg_quality = QUALITY_STEPS[0];
  c.fb_count = 1;
  c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("camera: init failed 0x%x\n", err);
    esp_camera_deinit();
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  if (s && s->id.PID == OV5640_PID) {
    s->set_reg(s, 0x5183, 0x80, 0x80);  // simple AWB: this lens lets IR through
    s->set_vflip(s, 1);                 // mounted upside down: flip + mirror
    s->set_hmirror(s, 1);
  }
  return true;
}

// Software standby for the sensor, and the driver off (stops XCLK and DMA).
void sleep() {
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    if (s->id.PID == OV3660_PID || s->id.PID == OV5640_PID) s->set_reg(s, 0x3008, 0x40, 0x40);
    else if (s->id.PID == OV2640_PID) s->set_reg(s, 0x109, 0x10, 0x10);
  }
  esp_camera_deinit();
}

bool completeJpeg(const camera_fb_t* fb) {
  return fb->len > 4 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8 && fb->buf[fb->len - 2] == 0xFF &&
         fb->buf[fb->len - 1] == 0xD9;
}

}  // namespace

namespace camera {

bool capture(std::vector<uint8_t>& jpeg, uint16_t& width, uint16_t& height) {
  if (!start()) return false;
  sensor_t* s = esp_camera_sensor_get();

  uint32_t t0 = millis();
  int warm = 0;
  while (millis() - t0 < SETTLE_MS || warm < 3) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) break;
    esp_camera_fb_return(fb);
    warm++;
  }

  bool ok = false;
  for (int q : QUALITY_STEPS) {
    s->set_quality(s, q);
    camera_fb_t* stale = esp_camera_fb_get();  // caught mid-change
    if (stale) esp_camera_fb_return(stale);
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) continue;
    if (completeJpeg(fb) && fb->len <= IMG_MAX_BYTES) {
      jpeg.assign(fb->buf, fb->buf + fb->len);
      width = fb->width;
      height = fb->height;
      Serial.printf("camera: %ux%u, %u bytes at quality %d\n", width, height, (unsigned)fb->len, q);
      ok = true;
    }
    esp_camera_fb_return(fb);
    if (ok) break;
  }
  sleep();
  if (!ok) Serial.println("camera: no photo small enough");
  return ok;
}

}  // namespace camera
