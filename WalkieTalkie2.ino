#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "driver/i2s.h"


// SCREEN DIM
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// VOLUME KNOB
#define POT_PIN  4

// PUSH TO TALK 
#define PTT_PIN  17

// I2S MIC
#define I2S_WS   9
#define I2S_SCK  10
#define I2S_SD   47

// I2S AMP
#define I2S_BCLK 14
#define I2S_LRC  18
#define I2S_DOUT 21
#define AMP_SD   16

// NAV SWITCH
#define UP     6
#define CENTER 39
#define DOWN   15
#define LEFT   7
#define RIGHT  40

// AUDIO
#define SAMPLE_RATE   16000
#define FRAME_SIZE    122
#define DMA_BUF_LEN   256
#define DMA_BUF_COUNT 8

// RX BUFFER
#define BUFFER_COUNT  8
int16_t audioQueue[BUFFER_COUNT][FRAME_SIZE];
volatile int writeIndex = 0;
volatile int readIndex = 0;
volatile int bufferedFrames = 0;
SemaphoreHandle_t audioMutex;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// MAC of PAIRED WALKIE TALKIE
uint8_t peerMAC[] = {0x10, 0xB4, 0x1D, 0xE0, 0x77, 0xCC}; // for my first walkie talkie
// This code can be used for the second walkie talkie:
// uint8_t peerMAC[] = {0x10, 0xB4, 0x1D, 0xE0, 0x6B, 0x48}; // for the second. comment out first line and uncomment this.

// for your own implentation, print your MAC addresses to find the pair of MACs you use to pair.

// MENU
const char* menuItems[] = {"VOLUME", "MAC", "INFO"};
int menuSize = 3;
int menuIndex = 0;
bool inMenu = true;
int volume = 50;

// CONNECTIVITY
bool connected = false;
unsigned long lastContact = 0;
volatile bool txMode = false;

// ================= ESP-NOW CALLBACKS =================

void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    connected = true;
    lastContact = millis();
  }
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  connected = true;
  lastContact = millis();

  if (len != FRAME_SIZE * sizeof(int16_t)) return;

  if (xSemaphoreTakeFromISR(audioMutex, NULL) == pdTRUE) {
    memcpy(audioQueue[writeIndex], data, len);
    writeIndex = (writeIndex + 1) % BUFFER_COUNT;
    if (bufferedFrames < BUFFER_COUNT) bufferedFrames++;
    xSemaphoreGiveFromISR(audioMutex, NULL);
  }
}

// ================= AUDIO TASK (Core 0) =================

void audioTask(void *param) {
  int16_t txFrame[FRAME_SIZE];
  int16_t rxFrame[FRAME_SIZE];
  size_t bytesRead, bytesWritten;

  // Filter and processing values
  static float dcPrev   = 0.0f;
  static float dcFilter = 0.0f;
  static int   holdFrames = 0;
  static float fadeGain   = 0.0f;

  for (int i = 0; i < DMA_BUF_COUNT; i++) {
    i2s_read(I2S_NUM_1, txFrame, sizeof(txFrame), &bytesRead, portMAX_DELAY);
  }

  while (true) {

    // ===== TX =====
    if (txMode) {
      i2s_read(I2S_NUM_1, txFrame,
               FRAME_SIZE * sizeof(int16_t),
               &bytesRead, portMAX_DELAY);

      if (bytesRead > 0) {

        // FADE GAIN TO SMOOTH AUDIO
        static float prev  = 0.0f;
        float alpha = 0.5f;

        int32_t sum = 0;
        for (int i = 0; i < FRAME_SIZE; i++) {
          sum += abs(txFrame[i] * 8);
        }
        int32_t avg = sum / FRAME_SIZE;

        if (avg > 200) {
          holdFrames = 20;
          fadeGain = 1.0f;
        } else if (holdFrames > 0) {
          holdFrames--;
          fadeGain = (float)holdFrames / 12.0f;
        } else {
          fadeGain = 0.0f;
        }

        if (holdFrames > 0 || fadeGain > 0.0f) {
          for (int i = 0; i < FRAME_SIZE; i++) {
            float x = (float)txFrame[i];

            // DC blocking (NOISE)
            float dcIn = x;
            dcFilter = 0.995f * (dcFilter + dcIn - dcPrev);
            dcPrev = dcIn;
            x = dcFilter;

            // Low pass Filter
            x = alpha * x + (1.0f - alpha) * prev;
            prev = x;

            // Compression (Help with distortion)
            float threshold = 4000.0f;
            float ratio = 6.0f;
            if (fabs(x) > threshold) {
              float excess = fabs(x) - threshold;
              excess /= ratio;
              x = (x > 0 ? 1 : -1) * (threshold + excess);
            }

            // Pre-gain (allows for our sound to be louder)
            x = x * 1.5f;

            // Fade (smooths voice)
            x = x * fadeGain;

            // Soft limiter (prevent some distortion)
            x = x / (1.0f + fabs(x) / 30000.0f);

            txFrame[i] = (int16_t)x;
          }

          // SEND

          esp_now_send(peerMAC, (uint8_t*)txFrame, FRAME_SIZE * sizeof(int16_t));
        }
      }

    // ===== RX =====
    } else {
      // read frames and apply volume control
      if (bufferedFrames > 1) {
        if (xSemaphoreTake(audioMutex, 0) == pdTRUE) {
          memcpy(rxFrame, audioQueue[readIndex], FRAME_SIZE * sizeof(int16_t));
          readIndex = (readIndex + 1) % BUFFER_COUNT;
          bufferedFrames--;
          xSemaphoreGive(audioMutex);
        }

        for (int i = 0; i < FRAME_SIZE; i++) {

         float s = (int32_t)rxFrame[i];

         s = s * 1.4;

        float gain = volume / 75.0f * 2.5f;  
        s = s * gain;

        s = 30000.0f * tanh(s / 30000.0f);

        // soft limiter
        s = s / (1.0f + fabs(s) / 15000.0f);

        rxFrame[i] = (int16_t)s;
        }

        // play sound
        i2s_write(I2S_NUM_0, rxFrame,
                  FRAME_SIZE * sizeof(int16_t),
                  &bytesWritten, portMAX_DELAY);
      } else {
        // keep silent
        int16_t silence[FRAME_SIZE] = {0};
        i2s_write(I2S_NUM_0, silence,
                  FRAME_SIZE * sizeof(int16_t),
                  &bytesWritten, portMAX_DELAY);
      }
    }
  }
}

// ================= I2S MIC =================

void setupMic() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_SCK,
    .ws_io_num    = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD
  };
  i2s_driver_install(I2S_NUM_1, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pins);
  i2s_zero_dma_buffer(I2S_NUM_1);
  Serial.println("MIC READY");
}

// ================= I2S AMP =================

void setupAmp() {
  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, HIGH);

  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = DMA_BUF_COUNT,
    .dma_buf_len = DMA_BUF_LEN,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
  Serial.println("AMP READY");
}

// ================= SETUP =================

void setup() {
  Serial.begin(115200);

  // setup display
  Wire.begin(41, 42);
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED FAILED");
    while (1);
  }
  display.setRotation(3);
  display.setTextColor(SSD1306_WHITE);

  // setup ESP NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(300);
  esp_wifi_start();
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Display local MAC
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  esp_now_init();
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, peerMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&peer);

  // setup or amplifier and mic
  setupAmp();
  setupMic();

  audioMutex = xSemaphoreCreateMutex();

  // pin audio to core 0. Smoother audio transmission on dedicated core.
  xTaskCreatePinnedToCore(
    audioTask, "audio", 8192, NULL, 10, NULL, 0
  );

  // setup pins
  pinMode(PTT_PIN, INPUT_PULLUP);
  pinMode(UP,      INPUT_PULLUP);
  pinMode(DOWN,    INPUT_PULLUP);
  pinMode(LEFT,    INPUT_PULLUP);
  pinMode(RIGHT,   INPUT_PULLUP);
  pinMode(CENTER,  INPUT_PULLUP);

  Serial.println("READY");
}

// ================= LOOP (Core 1) =================

void loop() {
  if (connected && millis() - lastContact > 5000)
    connected = false;

  // read PTT input
  txMode = (digitalRead(PTT_PIN) == LOW);
  digitalWrite(AMP_SD, txMode ? LOW : HIGH);

  // read volume from potentiometer
  int raw = analogRead(POT_PIN);
  float norm = raw / 4095.0f;
  volume = (int)(pow(norm, 1.5) * 75);

  // menu navigation
  if (digitalRead(UP) == LOW) {
    menuIndex--;
    if (menuIndex < 0) menuIndex = menuSize - 1;
    delay(200);
  }
  if (digitalRead(DOWN) == LOW) {
    menuIndex++;
    if (menuIndex >= menuSize) menuIndex = 0;
    delay(200);
  }
  if (digitalRead(RIGHT) == LOW) { inMenu = false; delay(200); }
  if (digitalRead(LEFT)  == LOW) { inMenu = true;  delay(200); }

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  // display connectivity status
  if (txMode)         display.println("TX");
  else if (connected) display.println("PAIRED");
  else                display.println("WAITING...");

  // menu display
  if (inMenu) {
    for (int i = 0; i < menuSize; i++) {
      display.setCursor(0, 15 + i * 10);
      if (i == menuIndex) display.print("> ");
      else display.print("  ");
      display.println(menuItems[i]);
    }
  } else {
    display.setCursor(0, 15);
    if (menuIndex == 0) {
      display.println("VOLUME");
      display.setTextSize(2);
      display.setCursor(0, 35);
      display.print(volume);
      display.println("%");
    } else if (menuIndex == 1) {
      display.println("ADDRESS");
      display.setTextSize(1);
      display.setCursor(0, 35);
      // for first walkie talkie
      display.println("10:B4:1D");
      display.println("E0:6B:48");
      // for second : comment out first and uncomment the following.
      //display.println("10:B4:1D");
      //display.println("E0:77:CC");

    } else if (menuIndex == 2) {
      display.println("INFO");
      display.setCursor(0, 30);
      display.println("ESP32");
      display.println("WALKIE");
      display.println("TALKIE");
      display.println("BY");
      display.println(" ");
      display.println("FABIO");
      display.println("SALGADO");
    }
  }

  //display 
  display.display();
  delay(50);
}
