// =================================================================================================
// TRINITY VOICE ASSISTANT - MAIN FIRMWARE
// ESP32-S3 WROOM-1 N16R8 (44-Pin DevKitC-1)
//
// This file integrates all components: I2S Mic/Amp, OLED, RGB LED, Secure Wi-Fi, and server communication.
// PlatformIO Build System (uses src/nvs_globals.h and src/nvs_globals.cpp for NVS handle definition)
// =================================================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <driver/i2s.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// --- Fix for NVS Global Handle Compiler Conflict ---
// Including this header ensures 'nvs_handle_t' is defined and 'g_trinity_nvs_handle' is declared as 'extern'.
#include "nvs_globals.h"

// =================================================================================================
// 1. CONFIGURATION & CONSTANTS
// =================================================================================================

// --- Server & Network ---
// !!! CRITICAL: REPLACE THIS WITH THE LOCAL IP ADDRESS OF YOUR PYTHON SERVER !!!
const char* SERVER_URL = "http://192.168.2.10:5002/voice_input";
const char* NVS_NAMESPACE = "trinity_nvs";
const char* WIFI_SSID_KEY = "ssid";
const char* WIFI_PASS_KEY = "pass";
const char* AP_SSID = "Trinity_Setup";
const int AP_CHANNEL = 1;
const int AP_TIMEOUT_MS = 180000; // 3 minutes for AP mode

// --- GPIO Pin Definitions (Verified for 44-pin ESP32-S3 DevKitC-1) ---
#define PIN_OLED_SDA 21     // I2C Data (J3-18)
#define PIN_OLED_SCL 41     // I2C Clock (J3-7) - CORRECTED
#define PIN_I2S_DOUT 25     // I2S TX Data (To MAX98357A DIN)
#define PIN_I2S_DIN 39      // I2S RX Data (From INMP441 SD/DO)
#define PIN_I2S_BCLK 40     // I2S Bit Clock (Shared by Mic and Amp)
#define PIN_I2S_LRCK 37     // I2S Left/Right Clock (Shared by Mic and Amp)
#define PIN_BUTTON_WAKE 12  // Button 1 (Next/Wake)
#define PIN_BUTTON_SEND 14  // Button 2 (Stop/Send)

// Onboard RGB LED (NeoPixel)
#define PIN_RGB_LED 48

// --- I2S Configuration ---
// Audio format constants
const i2s_port_t I2S_PORT = I2S_NUM_0;
const int SAMPLE_RATE = 16000; // Standard rate for speech recognition
const int CHANNELS = 1;
const int BITS_PER_SAMPLE = 16;
const int BUFFER_SIZE = 1024 * 4; // 4KB buffer for audio transmission

// --- Display Configuration ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

// =================================================================================================
// 2. GLOBAL OBJECTS & STATE
// =================================================================================================

// Hardware Objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_NeoPixel rgbLed(1, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);
DNSServer dnsServer;
WebServer server(80);
HTTPClient httpClient;

// State Variables
enum Status { STATUS_INITIALIZING, STATUS_WIFI_SETUP, STATUS_CONNECTED, STATUS_LISTENING, STATUS_THINKING, STATUS_SPEAKING, STATUS_ERROR };
Status currentStatus = STATUS_INITIALIZING;
bool isListening = false;
bool wifiCredentialsSaved = false;
bool button1Pressed = false;
bool button2Pressed = false;
char saved_ssid[64] = "";
char saved_pass[64] = "";

// =================================================================================================
// 3. LED AND DISPLAY FUNCTIONS
// =================================================================================================

// Colors for the onboard RGB LED
#define C_OFF 0x000000
#define C_BLUE 0x0000FF
#define C_GREEN 0x00FF00
#define C_RED 0xFF0000
#define C_CYAN 0x00FFFF
#define C_ORANGE 0xFF8000
#define C_PURPLE 0x800080

void setLedColor(uint32_t color) {
    rgbLed.setPixelColor(0, color);
    rgbLed.show();
}

void updateStatus(Status newStatus, const char* message = "") {
    currentStatus = newStatus;

    // 1. Update LED
    switch (currentStatus) {
        case STATUS_INITIALIZING: setLedColor(C_PURPLE); break;
        case STATUS_WIFI_SETUP: setLedColor(C_CYAN); break;
        case STATUS_CONNECTED: setLedColor(C_GREEN); break;
        case STATUS_LISTENING: setLedColor(C_BLUE); break;
        case STATUS_THINKING: setLedColor(C_ORANGE); break;
        case STATUS_SPEAKING: setLedColor(C_CYAN); break;
        case STATUS_ERROR: setLedColor(C_RED); break;
    }

    // 2. Update Display
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Dynamic Display Content
    switch (currentStatus) {
        case STATUS_INITIALIZING:
            display.setCursor(0, 0);
            display.println("TRINITY VOICE");
            display.println("Initializing...");
            break;
        case STATUS_WIFI_SETUP:
            display.setTextSize(2);
            display.setCursor(0, 0);
            display.println("SETUP AP");
            display.setTextSize(1);
            display.setCursor(0, 20);
            display.printf("SSID: %s", AP_SSID);
            display.setCursor(0, 30);
            display.println("Connect to 192.168.4.1");
            break;
        case STATUS_CONNECTED:
            display.setCursor(0, 0);
            display.println("READY.");
            display.setCursor(0, 10);
            display.println("Press B1/B2 to Speak");
            display.setCursor(0, 20);
            display.printf("IP: %s", WiFi.localIP().toString().c_str());
            break;
        case STATUS_LISTENING:
            display.setTextSize(2);
            display.setCursor(0, 0);
            display.println("LISTENING...");
            break;
        case STATUS_THINKING:
            display.setTextSize(2);
            display.setCursor(0, 0);
            display.println("THINKING...");
            break;
        case STATUS_SPEAKING:
            display.setTextSize(2);
            display.setCursor(0, 0);
            display.println("SPEAKING...");
            display.setTextSize(1);
            display.setCursor(0, 20);
            display.println(message);
            break;
        case STATUS_ERROR:
            display.setCursor(0, 0);
            display.println("ERROR!");
            display.setCursor(0, 10);
            display.println(message);
            break;
    }

    display.display();
}

// =================================================================================================
// 4. NVS (Non-Volatile Storage) FUNCTIONS
// =================================================================================================

bool loadCredentials() {
    // NOTE: Using g_trinity_nvs_handle
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &g_trinity_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error opening NVS: %s", esp_err_to_name(err));
        return false;
    }

    size_t ssid_len = sizeof(saved_ssid);
    size_t pass_len = sizeof(saved_pass);

    // Read SSID and Password
    if (nvs_get_str(g_trinity_nvs_handle, WIFI_SSID_KEY, saved_ssid, &ssid_len) == ESP_OK &&
        nvs_get_str(g_trinity_nvs_handle, WIFI_PASS_KEY, saved_pass, &pass_len) == ESP_OK) {
        nvs_close(g_trinity_nvs_handle);
        wifiCredentialsSaved = true;
        ESP_LOGI("NVS", "Credentials loaded successfully.");
        return true;
    }

    nvs_close(g_trinity_nvs_handle);
    ESP_LOGW("NVS", "No credentials found in NVS.");
    return false;
}

void saveCredentials(const String& ssid, const String& pass) {
    // NOTE: Using g_trinity_nvs_handle
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_trinity_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE("NVS", "Error opening NVS for write: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_str(g_trinity_nvs_handle, WIFI_SSID_KEY, ssid.c_str());
    nvs_set_str(g_trinity_nvs_handle, WIFI_PASS_KEY, pass.c_str());

    err = nvs_commit(g_trinity_nvs_handle);
    nvs_close(g_trinity_nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI("NVS", "Credentials saved and committed.");
    } else {
        ESP_LOGE("NVS", "Error committing NVS: %s", esp_err_to_name(err));
    }
}

// =================================================================================================
// 5. WIFI AP CONFIGURATION PORTAL
// =================================================================================================

const char CONFIG_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Trinity Setup</title>
    <style>
        :root {
            --neon-green: #39ff14;
            --neon-cyan: #00ffff;
            --bg-color: #0d0d0d;
            --box-color: #1a1a1a;
            --text-color: #ffffff;
        }
        body {
            font-family: 'Space Mono', monospace;
            background-color: var(--bg-color);
            color: var(--text-color);
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
            background-image: linear-gradient(0deg, var(--bg-color) 90%, rgba(57, 255, 20, 0.1) 100%),
                              linear-gradient(90deg, transparent 99%, rgba(0, 255, 255, 0.1) 100%);
            background-size: 50px 50px;
        }
        .container {
            width: 90%;
            max-width: 400px;
            padding: 30px;
            border-radius: 12px;
            background-color: var(--box-color);
            box-shadow: 0 0 15px rgba(0, 255, 255, 0.5), 0 0 25px rgba(57, 255, 20, 0.3);
            border: 2px solid var(--neon-cyan);
            transition: all 0.3s ease;
        }
        h1 {
            color: var(--neon-cyan);
            text-shadow: 0 0 5px var(--neon-cyan);
            border-bottom: 2px solid var(--neon-green);
            padding-bottom: 10px;
            margin-bottom: 20px;
            text-align: center;
            font-size: 1.8em;
        }
        p {
            font-size: 0.9em;
            color: #ccc;
            text-align: center;
            margin-bottom: 25px;
        }
        input[type="text"], input[type="password"] {
            width: 100%;
            padding: 12px;
            margin: 10px 0;
            border: 1px solid var(--neon-green);
            border-radius: 8px;
            background-color: #000;
            color: var(--neon-green);
            box-shadow: 0 0 5px rgba(57, 255, 20, 0.5);
            outline: none;
            font-size: 1em;
            box-sizing: border-box;
            transition: border-color 0.3s, box-shadow 0.3s;
        }
        input[type="text"]:focus, input[type="password"]:focus {
            border-color: var(--neon-cyan);
            box-shadow: 0 0 10px rgba(0, 255, 255, 0.7);
        }
        button {
            width: 100%;
            padding: 12px;
            margin-top: 20px;
            border: none;
            border-radius: 8px;
            background: var(--neon-green);
            color: var(--bg-color);
            font-weight: bold;
            text-transform: uppercase;
            cursor: pointer;
            box-shadow: 0 0 10px rgba(57, 255, 20, 0.7);
            transition: background 0.3s, box-shadow 0.3s, transform 0.1s;
        }
        button:hover {
            background: var(--neon-cyan);
            box-shadow: 0 0 15px rgba(0, 255, 255, 1);
            color: #000;
        }
        button:active {
            transform: translateY(1px);
        }
        .status-message {
            margin-top: 20px;
            font-size: 0.9em;
            text-align: center;
            color: var(--neon-cyan);
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ACCESS POINT ENGAGED</h1>
        <p>INITIATE CONNECTION PROTOCOL</p>
        <form action="/save" method="post">
            <label for="ssid">NETWORK IDENTIFIER (SSID):</label>
            <input type="text" id="ssid" name="ssid" placeholder="Home Wi-Fi Network Name" required>
            <label for="password">ACCESS KEY (Password):</label>
            <input type="password" id="password" name="password" placeholder="Wi-Fi Password" required>
            <button type="submit">ACTIVATE & REBOOT</button>
        </form>
        <div class="status-message">Connecting to: Trinity_Setup AP</div>
    </div>
</body>
</html>
)rawliteral";

void handleRoot() {
    server.send(200, "text/html", CONFIG_HTML);
}

void handleSave() {
    String ssid = server.arg("ssid");
    String pass = server.arg("password");

    if (ssid.length() > 0 && pass.length() > 0) {
        saveCredentials(ssid, pass);
        
        String html = "<meta http-equiv='refresh' content='5;url=/'><div style='text-align:center;color:white;background:black;padding:20px;border:3px solid #00ffff;'>";
        html += "<h1>CREDENTIALS ACCEPTED</h1>";
        html += "<p>System rebooting to connect to " + ssid + ". Please switch your device back to your home network.</p>";
        html += "</div>";

        server.send(200, "text/html", html);
        
        // Wait a moment for the response to send before rebooting
        delay(100);
        ESP.restart();
    } else {
        server.send(400, "text/plain", "Error: SSID and Password are required.");
    }
}

void setupAP() {
    updateStatus(STATUS_WIFI_SETUP);
    
    // Start DNS and Web Server
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, NULL, AP_CHANNEL); // No password for setup AP
    
    // Set up captive portal redirection
    dnsServer.start(53, "*", WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    // Redirect all unknown requests to the root page (captive portal)
    server.onNotFound([](){
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    });
    server.begin();

    Serial.println("\n--- AP Mode Started ---");
    Serial.printf("Connect to AP: %s\n", AP_SSID);
    Serial.println("Browse to: http://192.168.4.1/");

    unsigned long startTime = millis();
    while (millis() - startTime < AP_TIMEOUT_MS) {
        dnsServer.processNextRequest();
        server.handleClient();
        delay(10);
        // Use yield() to prevent WDT reset during the long wait
        yield(); 
    }

    // Timeout
    updateStatus(STATUS_ERROR, "AP Timeout. Rebooting...");
    delay(2000);
    ESP.restart();
}

// =================================================================================================
// 6. I2S FUNCTIONS
// =================================================================================================

// Helper function to install the I2S driver for Microphone (RX)
void i2s_mic_init() {
    // I2S Configuration (RX Mode)
    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), // Master mode for timing, RX for input
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // INMP441 uses the left channel slot
        .communication_format = I2S_COMM_FORMAT_STAND_I2S, // <-- Updated per user's standard
        .intr_alloc_flags = 0, // <-- Updated per user's suggestion
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false, // Use internal clock
    };

    // Pin configuration uses short names (determined by previous trial-and-error)
    const i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_LRCK,
        .data_out_num = I2S_PIN_NO_CHANGE, // Not used in RX mode
        .data_in_num = PIN_I2S_DIN
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

// Helper function to install the I2S driver for Amplifier (TX)
void i2s_amp_init() {
    // I2S Configuration (TX Mode)
    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), // Master mode for timing, TX for output
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // MAX98357A is mono, configured for left channel
        .communication_format = I2S_COMM_FORMAT_STAND_I2S, // <-- Updated per user's standard
        .intr_alloc_flags = 0, // <-- Updated per user's suggestion
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
    };

    // Pin configuration uses short names (determined by previous trial-and-error)
    const i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_LRCK,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE // Not used in TX mode
    };

    // Install driver on I2S_NUM_0 again, as it supports both TX and RX
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);
}


void i2s_start_microphone() {
    // FIX: Dynamic mode switching using uninstall/reinstall because i2s_set_driver_mode is unavailable.
    i2s_driver_uninstall(I2S_PORT); 
    i2s_mic_init(); // Re-initialize driver in RX mode
    i2s_start(I2S_PORT);
}

void i2s_stop_microphone() {
    i2s_stop(I2S_PORT);
}

void i2s_playback_start() {
    // FIX: Dynamic mode switching using uninstall/reinstall because i2s_set_driver_mode is unavailable.
    i2s_driver_uninstall(I2S_PORT); 
    i2s_amp_init(); // Re-initialize driver in TX mode
    i2s_start(I2S_PORT);
}

// =================================================================================================
// 7. NETWORK REQUEST AND RESPONSE HANDLING
// =================================================================================================

// This function sends audio data and handles the streaming audio response.
void processVoiceCommand() {
    updateStatus(STATUS_THINKING);

    // 1. Prepare HTTP Client
    httpClient.begin(SERVER_URL);
    httpClient.addHeader("Content-Type", "application/octet-stream");
    
    // 2. Start Recording
    i2s_start_microphone();
    
    // 3. Button Press to Start/Stop Recording
    
    i2s_stop_microphone();
    
    // --- SIMPLIFIED UPLOAD (Actual implementation would stream a buffer) ---
    // We send a fixed-size dummy buffer to simulate an audio upload
    size_t bytes_written = 0;
    const size_t DUMMY_SIZE = 1024;
    uint8_t dummy_audio_data[DUMMY_SIZE] = {0}; // Send a zero buffer
    
    // FIX: Cast adjusted to non-const (uint8_t*) to satisfy HTTPClient function signature.
    int httpResponseCode = httpClient.sendRequest("POST", (uint8_t*)dummy_audio_data, DUMMY_SIZE);

    if (httpResponseCode > 0) {
        // 4. Handle Audio Response Stream
        if (httpResponseCode == HTTP_CODE_OK) {
            updateStatus(STATUS_SPEAKING, "Response received.");
            i2s_playback_start();
            
            WiFiClient* stream = httpClient.getStreamPtr();
            size_t availableBytes = 0;
            
            // Read until the stream closes or times out
            while (httpClient.connected() || stream->available()) {
                if (stream->available()) {
                    availableBytes = stream->available();
                    if (availableBytes > 0) {
                        // Read chunks of data
                        int bytesRead = stream->readBytes((char*)dummy_audio_data, min(availableBytes, DUMMY_SIZE));
                        if (bytesRead > 0) {
                            // Write PCM audio data to the I2S DAC (MAX98357A)
                            i2s_write(I2S_PORT, dummy_audio_data, bytesRead, &bytes_written, portMAX_DELAY);
                        }
                    }
                }
                yield(); // Prevent WDT reset
            }
            
            // Playback complete
            i2s_stop(I2S_PORT);
            updateStatus(STATUS_CONNECTED);

        } else if (httpResponseCode == HTTP_CODE_NOT_ACCEPTABLE) {
            updateStatus(STATUS_ERROR, "Server Error: No Speech Detected.");
        } else {
            // FIX: Added .c_str() to convert the concatenated String to const char*
            updateStatus(STATUS_ERROR, (String("HTTP Error: ") + String(httpResponseCode)).c_str());
        }
    } else {
        updateStatus(STATUS_ERROR, "Server Connection Failed.");
    }
    
    httpClient.end();
}


// =================================================================================================
// 8. CORE SETUP AND LOOP
// =================================================================================================

void setup() {
    // 1. Initialize System
    Serial.begin(115200);
    delay(100);
    
    // 2. LED/Display Init
    rgbLed.begin();
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL); // Initialize I2C for OLED
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println(F("SSD1306 allocation failed. Check wiring."));
        updateStatus(STATUS_ERROR, "OLED Fail");
        for (;;); // Loop indefinitely if display fails
    }
    updateStatus(STATUS_INITIALIZING);
    
    // 3. GPIO Setup (Buttons)
    pinMode(PIN_BUTTON_WAKE, INPUT_PULLUP);
    pinMode(PIN_BUTTON_SEND, INPUT_PULLUP);
    
    // 4. Initialize NVS (Always initialize NVS first)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 5. Load Wi-Fi Credentials
    if (!loadCredentials()) {
        Serial.println("Starting AP for Wi-Fi configuration...");
        setupAP();
    }

    // 6. Connect to Wi-Fi (if credentials exist)
    if (wifiCredentialsSaved) {
        Serial.printf("Connecting to %s...\n", saved_ssid);
        WiFi.begin(saved_ssid, saved_pass);

        // Wait for connection (15 seconds)
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
            delay(500);
            setLedColor(C_PURPLE); // Blinking purple
            delay(500);
            setLedColor(C_OFF);
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
            updateStatus(STATUS_CONNECTED);
        } else {
            Serial.println("\nFailed to connect. Starting AP mode.");
            updateStatus(STATUS_ERROR, "Wi-Fi Fail. Starting AP.");
            delay(2000);
            setupAP(); // Fall back to AP setup
        }
    }
    
    // 7. Initialize I2S
    // We only call one init function here to ensure the I2S driver is initially loaded. 
    // The start/playback functions will handle the mode switching via uninstall/reinstall.
    i2s_mic_init(); 
    i2s_stop(I2S_PORT); // Start stopped, will be enabled only when needed
}

void loop() {
    // Handle AP mode and server clients if in SETUP state
    if (currentStatus == STATUS_WIFI_SETUP) {
        dnsServer.processNextRequest();
        server.handleClient();
        return;
    }

    // Only process buttons if connected and not currently speaking/thinking
    if (currentStatus == STATUS_CONNECTED) {
        // Read button state
        button1Pressed = (digitalRead(PIN_BUTTON_WAKE) == LOW);
        button2Pressed = (digitalRead(PIN_BUTTON_SEND) == LOW);

        if (button1Pressed && !isListening) {
            // Start recording (Wake button)
            isListening = true;
            updateStatus(STATUS_LISTENING);
            Serial.println("Started listening...");
        } else if (button2Pressed && isListening) {
            // Stop recording and process (Send button)
            isListening = false;
            Serial.println("Stopped listening. Processing command...");
            processVoiceCommand();
            updateStatus(STATUS_CONNECTED);
        }
    }
    
    // During listening state, we would typically be streaming data
    if (isListening) {
        // Placeholder for reading audio data (this is where the recording buffer fills up)
        // In the final design, this would stream the buffer to the server later.
        delay(10);
        yield();
    }
    
    // Simple delay for stability and power saving
    delay(50);
}
