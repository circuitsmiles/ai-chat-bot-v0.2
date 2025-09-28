#include <Arduino.h>
#include <driver/i2s.h>
#include "driver/adc.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h> // Needed for I2C communication

// --- Configuration ---
// I2S connections for MAX98357A amplifier
const int BCLK_PIN = 27;
const int LRC_PIN = 26;
const int DIN_PIN = 25;

// OLED Display pins (I2C)
#define OLED_SDA 21 
#define OLED_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1 

// Button pins with internal pull-ups
const int WAKE_BUTTON_PIN = 12; // Next/Cycle button
const int FUNCTION_BUTTON_PIN = 14; // Action/Speak button

// LED pins
const int GREEN_LED_PIN = 16;
const int RED_LED_PIN = 17;

// --- Audio Configuration ---
const int SAMPLE_RATE = 16000;

// --- Server URL ---
const char* SERVER_URL = "http://192.168.2.10:5002/get_audio_response";

// --- Static Sentence Array ---
const char* sentences[] = {
  "temperature in amsterdam",
  "how are you",
  "your favorite color",
  "your favorite pokemon",
  "what is 2+2",
  "what is the capital of France",
  "what is your name",
  "what is the weather like today",
  "do you like dogs",
  "tell me a joke"
};
const int NUM_SENTENCES = 10;
int currentSentenceIndex = 0;

// --- State Machine ---
enum class AppState {
  INITIALIZING,
  CONNECTING_WIFI,
  READY,
  GETTING_RESPONSE, 
  PLAYING_AUDIO, 
  ERROR
};
AppState currentState = AppState::INITIALIZING;
AppState previousState = AppState::INITIALIZING;

// --- Global Objects ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
HTTPClient http;
WiFiClient *audioStream = nullptr; 

// --- Button State Tracking for Edge Detection ---
bool lastWakeButtonState = HIGH;
bool lastFunctionButtonState = HIGH;


// -----------------------------------------------------------------------------
// HELPER FUNCTIONS
// -----------------------------------------------------------------------------

void setLEDs(bool red, bool green) {
  digitalWrite(RED_LED_PIN, red ? HIGH : LOW);
  digitalWrite(GREEN_LED_PIN, green ? HIGH : LOW);
}

void displayStatus(const char* message, uint16_t color = SSD1306_WHITE) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(color);
  display.setCursor(0, 0);
  display.println(message);
  display.display();
}

void displayPrompt() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Status/Help text
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("READY. A: Cycle | B: Speak");
  
  // Prompt text (Larger)
  display.setCursor(0, 20);
  display.setTextSize(2); 
  display.println(sentences[currentSentenceIndex]);
  
  display.display();
}

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
  i2s_pin_config_t pin_config = {
    .bck_io_num = BCLK_PIN,
    .ws_io_num = LRC_PIN,
    .data_out_num = DIN_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE 
  };
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

WiFiClient* getAudioStream(const char* prompt) {
  displayStatus("Thinking...", SSD1306_WHITE);
  setLEDs(true, false);

  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(30000); 

  String jsonPayload = "{\"prompt\": \"" + String(prompt) + "\"}";
  Serial.print("Sending prompt: ");
  Serial.println(prompt);

  int httpResponseCode = http.POST(jsonPayload);
    
  if (httpResponseCode == HTTP_CODE_OK) {
    Serial.printf("[HTTP] POST success, code: %d\n", httpResponseCode);
    int payloadSize = http.getSize();
    Serial.printf("Payload size: %d\n", payloadSize);
    return http.getStreamPtr();
  } else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
    http.end();
    return nullptr;
  }
}

// -----------------------------------------------------------------------------
// ARDUINO SETUP
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // Initialize LEDs and Buttons
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  setLEDs(false, false);
  pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(FUNCTION_BUTTON_PIN, INPUT_PULLUP);

  // Initialize OLED (I2C)
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.display(); 
  delay(2000);
  display.clearDisplay();

  // State: Initializing
  displayStatus("Initializing...", SSD1306_WHITE);
  currentState = AppState::CONNECTING_WIFI;
}

// -----------------------------------------------------------------------------
// ARDUINO MAIN LOOP
// -----------------------------------------------------------------------------

void loop() {
  // Check for state change and update display/LEDs if necessary
  if (currentState != previousState) {
    switch (currentState) {
      case AppState::CONNECTING_WIFI:
        displayStatus("Connecting to saved WiFi...", SSD1306_WHITE);
        break;
      case AppState::READY:
        displayPrompt();
        setLEDs(false, true); // Green LED on
        break;
      case AppState::PLAYING_AUDIO:
        displayStatus("Speaking...", SSD1306_WHITE); 
        break;
      case AppState::ERROR:
        displayStatus("Error! Check serial.", SSD1306_WHITE);
        setLEDs(true, false);
        break;
      default:
        break;
    }
    previousState = currentState;
  }

  // Handle state transitions and actions
  switch (currentState) {
    case AppState::CONNECTING_WIFI:
      {
        if (WiFi.status() != WL_CONNECTED) {
            // Attempt connection using stored credentials (per user request)
            WiFi.begin(); 
            unsigned long startAttemptTime = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
              delay(500);
              Serial.print(".");
            }
        }
        
        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\nWiFi connected.");
          Serial.print("IP Address: ");
          Serial.println(WiFi.localIP());
          setupI2S();
          currentState = AppState::READY;
        } else {
          displayStatus("WiFi Failed! Check saved credentials.", SSD1306_WHITE);
          delay(2000);
        }
      }
      break;
        
    case AppState::READY:
      { // FIX: Added local scope for variable declaration to prevent compilation error
        // Read current button states
        bool currentWakeButtonState = digitalRead(WAKE_BUTTON_PIN);
        bool currentFunctionButtonState = digitalRead(FUNCTION_BUTTON_PIN);

        // === WAKE BUTTON (Cycle Prompt) - Detect the press (LOW state after HIGH) ===
        if (currentWakeButtonState == LOW && lastWakeButtonState == HIGH) { 
          currentSentenceIndex = (currentSentenceIndex + 1) % NUM_SENTENCES;
          displayPrompt(); 
        }
        
        // === FUNCTION BUTTON (Speak) - Detect the press (LOW state after HIGH) ===
        if (currentFunctionButtonState == LOW && lastFunctionButtonState == HIGH) { 
          currentState = AppState::GETTING_RESPONSE;
          // IMPORTANT: Break here to exit the switch and immediately process the new state
          break; 
        }

        // Store the current states for the next loop iteration (Edge detection/Debounce)
        lastWakeButtonState = currentWakeButtonState;
        lastFunctionButtonState = currentFunctionButtonState;
      
      } // End of local scope
      break;
    
    case AppState::GETTING_RESPONSE:
      // Initiate the request and get the stream
      audioStream = getAudioStream(sentences[currentSentenceIndex]);
      if (audioStream != nullptr) {
        currentState = AppState::PLAYING_AUDIO;
      } else {
        currentState = AppState::ERROR;
      }
      break;

    case AppState::PLAYING_AUDIO:
    {
      // Blinking red LED and solid green LED logic
      static unsigned long lastBlink = 0;
      const unsigned long blinkInterval = 500; 
      if (millis() - lastBlink > blinkInterval) {
        digitalWrite(RED_LED_PIN, !digitalRead(RED_LED_PIN)); // Toggle red LED
        digitalWrite(GREEN_LED_PIN, HIGH); // Green LED is solid
        lastBlink = millis();
      }

      // Read audio data and write to I2S
      int read_bytes = 0;
      if (audioStream->available()) {
        uint8_t buffer[1024];
        read_bytes = audioStream->readBytes(buffer, sizeof(buffer));
        if (read_bytes > 0) {
          size_t bytes_written;
          i2s_write(I2S_NUM_0, buffer, read_bytes, &bytes_written, portMAX_DELAY);
        }
      }
      
      // Check if playback is finished
      if (!audioStream->connected()) {
        Serial.println("Audio playback complete. Closing stream.");
        http.end();
        i2s_zero_dma_buffer(I2S_NUM_0);
        setLEDs(false, false);
        currentState = AppState::READY;
      }
      break;
    }
      
    case AppState::ERROR:
      delay(5000); 
      currentState = AppState::READY;
      break;
    
    case AppState::INITIALIZING:
      // Handled in setup
      break;
  }
}