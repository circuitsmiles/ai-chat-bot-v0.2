# **ESP32 Voice Assistant v0.2 \- Voice Input Enabled**

This is a comprehensive guide to building a voice assistant using an **ESP32-S3-WROOM-1 N16R8** microcontroller, an OLED display, and a Python server for AI integration. The project now supports live audio recording and transcription using the Gemini API.

### **Features**

* **Live Voice Input:** The ESP32 captures audio from the **INMP441 I2S Microphone** and streams it to the Python server.  
* **Gemini STT & LLM Integration:** The server transcribes the received audio using the multimodal capabilities of the Gemini API, generates a textual response, and converts it to audio.  
* **Trinity Persona:** The LLM is configured with a system instruction to respond in the brief, laconic, and focused style of **Trinity from *The Matrix*** films, ensuring mission-critical and technical dialogue.  
* **Non-Repetitive Responses:** The server employs a **random seed** and high **temperature** to ensure that creative queries always yield unique answers.  
* **Wake Word Proxy:** The "Wake/Start" button acts as a proxy for the "TRINITY" wake word, initiating the listening state.  
* **Secure Wi-Fi:** The firmware will use a **Configuration Portal (AP mode)** to securely save Wi-Fi credentials to flash memory (to be implemented).  
* **Text-to-Speech (TTS):** The server returns a real-time PCM audio stream from the Gemini TTS model, which the ESP32 plays back via the I2S amplifier.  
* **Visual Feedback:** A monochrome OLED display shows the device's current status (e.g., "Listening...", "Sending...", "Speaking...").  
* **Status LED:** The **Onboard RGB LED (GPIO 48\)** provides visual cues for various states.

### **Hardware Components**

Here is a list of all the hardware components required for this project.

* **Microcontroller:** ESP32-S3-WROOM-1 N16R8 (8MB PSRAM)  
* **Display:** 0.96" OLED Display (SSD1306)  
* **Audio Input:** **INMP441 I2S Microphone**  
* **Audio Output:** MAX98357A I2S Class-D Amplifier connected to an 8-ohm speaker  
* **User Input:** Two tactile buttons for **"Wake/Start Listening"** and **"Stop/Send"** functions.  
* **Visual Cues:** **Onboard RGB LED** (accessible via GPIO 48\)  
* **Cables and Wires:** Breadboard, jumper wires, and at least a 1A USB charger

### **Pinout Guide (Verified and Definitive)**

This guide details the specific pin connections for all components, including required auxiliary connections and power rails.

| Component | Pin Name | ESP32 GPIO Pin | Detail / Connection |
| :---- | :---- | :---- | :---- |
| **OLED Display (SSD1306)** | SDA | **21** | I2C Data (Accessible on J3-18) |
|  | SCL | **41** | I2C Clock (Accessible on J3-7) |
| **I2S Amplifier (MAX98357A) \- Output** | DIN | **40** | I2S TX Data (Accessible on J3-8) |
|  | BCLK | **37** | I2S Clock (Shared, Accessible on J3-11) |
|  | LRC | **36** | I2S Word Select (Shared, Accessible on J3-12) |
|  | **SD (Shutdown)** | **3V3** | **Enables Amplifier** (MUST be connected to 3V3) |
|  | **Gain (SEL)** | **3V3** | **Sets Gain to 9 dB** (MUST be connected to 3V3) |
| **I2S Microphone (INMP441) \- Input** | SD/DO | **39** | I2S RX Data (Accessible on J3-9) |
|  | SCK | **37** | I2S Clock (Shared) |
|  | WS | **36** | I2S Word Select (Shared) |
|  | **L/R** | **GND** | **Left Channel Selection** (MUST be connected to GND) |
| **Button 1 (Wake/Start)** | \- | **12** | Input (Pullup, Accessible on J1-18) |
| **Button 2 (Stop/Send)** | \- | **14** | Input (Pullup, Accessible on J1-20) |
| **Onboard RGB LED** | Data Pin | **48** | Internal Neopixel/WS2812 Connection |

### **Power Rail Configuration**

| Component | Power Pin | ESP32 Power Rail | Reasoning |
| :---- | :---- | :---- | :---- |
| **OLED Display (SSD1306)** | VCC/VDD | **3V3** | Safe, standard logic level power. |
| **I2S Amplifier (MAX98357A)** | VDD | **5V** | Provides maximum power for the best speaker volume and clarity. |
| **I2S Microphone (INMP441)** | VCC/VDD | **3V3** | Required for the INMP441's operating voltage range. |
| **All Components** | GND | **GND** | Connect all component grounds to any ESP32 GND pin. |

### **Wiring Diagram**

For a visual guide to the circuit connections, please refer to the Fritzing breadboard diagram: [Circuit diagram](https://www.google.com/search?q=fritz.png)

### **System Overview**

The system operates in a series of states managed by the ESP32 firmware:

1. **Initialization:** The ESP32 starts, connects to your Wi-Fi network (or launches the configuration portal), sets up I2S for audio I/O, and enters the **READY** state. The screen displays "Ready" and the green light on the RGB LED is solid.  
2. **Listening Trigger (Wake Word Proxy):**  
   * The device is idle in the **READY** state.  
   * Pressing the **WAKE BUTTON (GPIO 12\)** acts as a proxy for the "TRINITY" wake word, transitioning the device to the **LISTENING** state.  
3. **Recording:**  
   * The device enters the **LISTENING** state. The RGB LED displays a solid color (e.g., Yellow/White).  
   * The ESP32 reads 16-bit PCM audio data from the INMP441 microphone and stores it in memory.  
   * Recording automatically stops after **10 seconds** or when the **STOP/SEND BUTTON (GPIO 14\)** is pressed.  
4. **Processing & Sending:**  
   * The device enters the **SENDING** state. The screen displays "Sending Audio & Thinking..." and the RGB LED displays a solid color (e.g., Blue).  
   * The recorded audio data is sent via HTTP POST to the Python server.  
5. **Server Process:** The server receives the audio, uses the Gemini API for **Transcription (STT)**, processes the query via the **LLM (now enforcing the Trinity persona)**, and returns the response as an audio stream via **Text-to-Speech (TTS)**.  
6. **Audio Playback:**  
   * The ESP32 enters the **PLAYING\_AUDIO** state. The screen displays "Speaking...", and the RGB LED cycles or pulses (e.g., Red) to indicate that the audio is playing.  
7. **Completion:** After the audio stream is finished, the ESP32 returns to the **READY** state.

### **Usage Guide**

Follow these steps to set up and run the voice assistant.

#### **1\. Firmware Setup (Arduino IDE)**

WARNING: CRITICAL ESP32-S3 CONFIGURATION  
The ESP32-S3 with 8MB PSRAM requires specific board settings to prevent the Watchdog Timer (WDT) from crashing the device. You must apply the following "Golden Configuration" to ensure stability:

1. **Install the Arduino IDE** and the **ESP32 Board** following the standard instructions.  
2. **Install Libraries:** Install the required libraries (**Adafruit GFX, Adafruit SSD1306, Adafruit NeoPixel**, and the I2S audio library, which will be specified in the final code).  
3. **Apply the Golden Configuration for ESP32-S3 N16R8:** Go to the **Tools** menu and set these parameters:

| Setting | Value to Select | Rationale |
| :---- | :---- | :---- |
| **Board** | **ESP32S3 Dev Module** | Exposes necessary advanced options. |
| **PSRAM** | **OPI PSRAM** (or **Enabled**) | **MANDATORY** for 8MB PSRAM. |
| **Flash Mode** | **DIO** | **Critical Fix:** Prevents Watchdog Timer (WDT) crashes (rst:0x10). |
| **Partition Scheme** | **Huge App (3MB No OTA)** | Ensures stable memory allocation with large external flash. |
| **USB CDC On Boot** | **Disabled** | Ensures the **right-side USB-to-Serial port** works reliably for debugging. |
| **Erase All Flash Before Sketch Upload** | **Enabled (All Flash Contents)** | Use this for the first upload to wipe conflicting data. |

4. **Upload the Code:** Open the firmware file, configure your Wi-Fi credentials (or use the Configuration Portal method), and upload the code to your ESP32.

#### **1b. Firmware Setup (PlatformIO)**

PlatformIO (PIO) is an excellent alternative to the Arduino IDE, offering a professional development environment within **VS Code**. It allows you to define all the "Golden Configuration" settings and dependencies in a single file (platformio.ini).

1. **Install VS Code:** Download and install Visual Studio Code.  
2. **Install PlatformIO Extension:** Open VS Code, go to the Extensions view (Ctrl+Shift+X), search for "PlatformIO IDE," and install it.  
3. **Project Setup:**  
   * Click the **PlatformIO Home** icon (the alien head) in the VS Code sidebar.  
   * Select **\+ New Project** and choose the **ESP32-S3-WROOM-1-N16R8** board.  
4. **Configure platformio.ini:** Open the platformio.ini file and ensure your environment matches the required configuration for the ESP32-S3. You'll need to define the build flags and environment settings here:  
   \[env:esp32-s3-wroom-1\]  
   platform \= espressif32  
   board \= esp32-s3-devkitc-1 ; A common dev board that supports the N16R8 module  
   framework \= arduino  
   upload\_speed \= 921600

   ; \--- Golden Configuration Flags \---  
   board\_build.flash\_mode \= dio  
   board\_build.partitions \= huge\_app.csv  
   build\_flags \=   
       \-D ARDUINO\_USB\_CDC\_ON\_BOOT=0   
       \-D CONFIG\_SPIRAM\_MODE\_QUAD=1 ; PSRAM enabled

   ; Add project libraries here  
   lib\_deps \=  
       adafruit/Adafruit GFX Library@^1.11.9  
       adafruit/Adafruit SSD1306@^2.5.9  
       adafruit/Adafruit NeoPixel@^1.12.0 ; Required for GPIO 48 RGB LED  
       ; Add the I2S audio library when specified

5. **Build and Upload:** Use the PIO buttons at the bottom of VS Code (a checkmark for **Build** and an arrow for **Upload**) to compile and flash the firmware to your ESP32.

#### **2\. Server Setup (Python)**

1. **Install Python** and **activate your virtual environment**.  
2. **Install Dependencies:** Run the following command using the updated requirements.txt:  
   pip install \-r requirements.txt

3. **Create .env File:** In the same directory as the server.py file, add your Gemini API key:  
   GEMINI\_API\_KEY="YOUR\_API\_KEY\_HERE"

4. **Run the Server:** Run the server from your terminal. This will start the server on your computer's local network.  
   python server.py

   *Note: You will need the local IP address of the machine running this server for the ESP32 firmware.*