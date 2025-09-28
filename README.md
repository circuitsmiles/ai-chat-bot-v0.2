# **ESP32 Voice Assistant v0.1**

This is a comprehensive guide to building a voice assistant using an ESP32 microcontroller, an OLED display, and a Python server for AI integration. The project allows you to select from a list of predefined phrases using buttons to generate a spoken response.

### **Features**

* **Offline Operation:** The project operates by using "Next" and "Speak" buttons to select from a list of pre-written prompts. This allows for quick, repeatable responses and functionality without a microphone.  
* **Gemini API Integration:** The Python server uses the Gemini API to convert a predefined text prompt into a spoken response.  
* **Text-to-Speech (TTS):** The server returns a real-time audio stream from the Gemini API, which the ESP32 then plays back.  
* **Visual Feedback:** A monochrome OLED display shows the device's current status (e.g., "Thinking...", "Speaking...", "Ready").  
* **Status LEDs:** LEDs provide visual cues for various states.

### **Hardware Components**

Here is a list of all the hardware components required for this project.

* **Microcontroller:** ESP32 Dev Kit C  
* **Display:** 0.96" OLED Display (SSD1306)  
* **Audio Output:** MAX98357A I2S Class-D Amplifier connected to an 8-ohm speaker  
* **User Input:** Two tactile buttons for "Next" and "Speak" functions.  
* **Visual Cues:** One red LED and one green LED  
* **Cables and Wires:** Breadboard, jumper wires, and at least a 1A USB charger

### **Pinout Guide**

This guide details the specific pin connections for all components in your voice assistant project. All GND pins on the components should be connected to a common ground rail.

| Component | Pin Name | ESP32 GPIO Pin |
| :---- | :---- | :---- |
| **OLED Display (SSD1306)** | SDA | 21 |
|  | SCL | 22 |
| **I2S Amplifier (MAX98357A)** | DIN | 25 |
|  | BCLK | 27 |
|  | LRC | 26 |
| **Button 1 (Next)** | \- | 12 |
| **Button 2 (Speak)** | \- | 14 |
| **LED (Red)** | \- | 17 |
| **LED (Green)** | \- | 16 |

### **Wiring Diagram**

For a visual guide to the circuit connections, please refer to the Fritzing breadboard diagram: [Circuit diagram](fritz.png)


### **System Overview**

The system operates in a series of states managed by the ESP32 firmware:

1. **Initialization:** The ESP32 starts, connects to your Wi-Fi network, and enters the READY state. The screen displays "Ready" and the green LED is solid.  
2. **User Input:**  
   * Pressing the **"Next" button** cycles to the next static sentence.  
   * Pressing the **"Speak" button** initiates the audio request to the server.  
3. **Processing:** The screen displays "Thinking..." and the red LED is solid while the server processes the request.  
4. **Audio Playback:** Once the server has returned a response, the ESP32 enters the PLAYING\_AUDIO state. The screen changes to "Speaking...", the green LED is solid, and the red LED blinks to indicate that the audio is playing.  
5. **Completion:** After the audio stream is finished, the ESP32 returns to the READY state, and the LEDs and screen revert to their initial state.

### **Usage Guide**

Follow these steps to set up and run the voice assistant.

#### **1\. Firmware Setup (Arduino IDE)**

1. **Install the Arduino IDE:** Download and install the Arduino IDE if you don't have it already.  
2. **Add the ESP32 Board:** Go to File \> Preferences and add the ESP32 board manager URL: https://www.google.com/search?q=https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package\_esp32\_index.json  
3. **Install the Board:** Go to Tools \> Board \> Boards Manager, search for "esp32", and install the esp32 package.  
4. **Install Libraries:**  
   * Adafruit GFX Library: Go to Sketch \> Include Library \> Manage Libraries, search for "Adafruit GFX", and install it.  
   * Adafruit SSD1306 Library: Install this library from the Library Manager as well.  
5. **Upload the Code:** Open the firmware file, ensure your Wi-Fi credentials are set up, and upload the code to your ESP32.

#### **2\. Server Setup (Python)**

1. **Install Python:** Ensure you have Python 3 installed on your computer.  
2. Create and Activate a Virtual Environment:  
   It is highly recommended to use a virtual environment to manage project dependencies.  
   * On macOS/Linux:  
     python3 \-m venv venv  
     source venv/bin/activate  
   * On Windows:  
     python \-m venv venv  
     venv\\Scripts\\activate  
3. Install Dependencies: Ensure that the requirements.txt file is in the same directory as your server code. Next, open a terminal or command prompt and run the following command to install all the required Python libraries:  
   pip install \-r requirements.txt  
4. Create .env File: In the same directory as the server.py file, create a new file named .env. Inside this file, add your Gemini API key:  
   GEMINI\_API\_KEY="YOUR\_API\_KEY\_HERE"  
5. Run the Server: Run the server from your terminal. This will start the server on your computer's local network.  
   python server.py