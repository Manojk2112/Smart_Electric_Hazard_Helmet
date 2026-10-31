#include <Arduino.h>
#include <driver/i2s_std.h>
#include "audio_adpcm.h"

#define I2S_PORT I2S_NUM_0
#define I2S_WS_PIN 19
#define I2S_BCLK_PIN 20
#define I2S_DIN_PIN 18
#define SAMPLE_RATE 48000

i2s_chan_handle_t tx_handle = nullptr;
volatile bool stopRequested = false;

const int indexTable[16] = {
  -1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8
};
const int stepTable[89] = {
  7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,
  34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,
  157,173,190,209,230,253,279,307,337,371,408,449,494,544,
  598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1876,
  2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,
  6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
  18500,20350,22385,24623,27086,29794,32767
};

void setupI2S() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;

  if (i2s_new_channel(&chan_cfg, &tx_handle, nullptr) != ESP_OK) {
    Serial.println("I2S channel creation failed");
    while (true) delay(1000);
  }

  i2s_std_config_t cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_BCLK_PIN,
      .ws = (gpio_num_t)I2S_WS_PIN,
      .dout = (gpio_num_t)I2S_DIN_PIN,
      .din = I2S_GPIO_UNUSED,
      .invert_flags = { false, false, false }
    }
  };

  if (i2s_channel_init_std_mode(tx_handle, &cfg) != ESP_OK ||
      i2s_channel_enable(tx_handle) != ESP_OK) {
    Serial.println("I2S initialization failed");
    while (true) delay(1000);
  }
  Serial.println("I2S: 48 kHz / 16-bit / mono");
}

void stopAudio() { stopRequested = true; }

void playClip(const AudioClip &clip, const char *name) {
  stopRequested = false;
  Serial.printf("\nPlaying %s\n", name);

  int predictor = clip.firstSample;
  int index = 0;

  int16_t out[512];
  size_t outCount = 0;

  // First PCM sample.
  out[outCount++] = (int16_t)predictor;

  for (size_t bytePos = 0; bytePos < clip.bytes && !stopRequested; bytePos++) {
    uint8_t packed = pgm_read_byte(clip.data + bytePos);

    for (int nib = 0; nib < 2 && !stopRequested; nib++) {
      int code = (nib == 0) ? (packed & 0x0F) : ((packed >> 4) & 0x0F);
      int step = stepTable[index];
      int diff = step >> 3;

      if (code & 1) diff += step >> 2;
      if (code & 2) diff += step >> 1;
      if (code & 4) diff += step;

      if (code & 8) predictor -= diff;
      else predictor += diff;

      predictor = constrain(predictor, -32768, 32767);

      index += indexTable[code];
      index = constrain(index, 0, 88);

      out[outCount++] = (int16_t)predictor;

      if (outCount == 512) {
        size_t written = 0;
        if (i2s_channel_write(tx_handle, out, sizeof(out), &written, 1000) != ESP_OK) {
          stopRequested = true;
          break;
        }
        outCount = 0;
      }

      // Do not output beyond the original sample count.
      // Extra nibble at the very end is harmless but is skipped by exact count below.
    }
  }

  if (outCount && !stopRequested) {
    size_t written = 0;
    i2s_channel_write(tx_handle, out, outCount * sizeof(int16_t), &written, 1000);
  }

  int16_t silence[128] = {0};
  size_t written = 0;
  i2s_channel_write(tx_handle, silence, sizeof(silence), &written, 1000);

  Serial.println(stopRequested ? "Stopped." : "Finished.");
}

void menu() {
  Serial.println("\n=== SMART HELMET AUDIO TEST ===");
  Serial.println("1 = A01");
  Serial.println("2 = A02");
  Serial.println("4 = A04");
  Serial.println("5 = A05");
  Serial.println("6 = A06");
  Serial.println("a = play all");
  Serial.println("s = stop");
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nSMART HELMET AUDIO TEST - COMPACT");
  setupI2S();
  menu();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();

    if (c == '1') playClip(A01_clip, "A01");
    else if (c == '2') playClip(A02_clip, "A02");
    else if (c == '4') playClip(A04_clip, "A04");
    else if (c == '5') playClip(A05_clip, "A05");
    else if (c == '6') playClip(A06_clip, "A06");
    else if (c == 'a' || c == 'A') {
      playClip(A01_clip, "A01");
      if (!stopRequested) playClip(A02_clip, "A02");
      if (!stopRequested) playClip(A04_clip, "A04");
      if (!stopRequested) playClip(A05_clip, "A05");
      if (!stopRequested) playClip(A06_clip, "A06");
    }
    else if (c == 's' || c == 'S') stopAudio();

    menu();
  }
  delay(5);
}
