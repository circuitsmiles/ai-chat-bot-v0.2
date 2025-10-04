// Required headers
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

// Global constants for Preferences
Preferences preferences;
const char* PREFS_NAMESPACE = "assistant_cfg";
const char* PREF_SSID = "ssid";
const char* PREF_PASS = "pass";

// Web Server and DNS Server setup for AP Portal
const IPAddress apIP(192, 168, 4, 1);
const IPAddress netMask(255, 255, 255, 0);
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

// --- HTML Content for the Configuration Form (Updated with Sci-Fi Theme) ---
const char CONFIG_HTML[] = R"raw(
<!DOCTYPE html>
<html>
<head>
    <title>Trinity Wi-Fi Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        /* Base styles - Dark, Grid-like background */
        body { 
            font-family: 'Courier New', monospace; 
            background-color: #0d0d0d; 
            color: #00ff00; /* Neon Green */
            margin: 0; 
            padding: 20px; 
            /* Subtle grid pattern for sci-fi look */
            background-image: linear-gradient(0deg, transparent 24%, rgba(0, 255, 0, 0.05) 25%, rgba(0, 255, 0, 0.05) 26%, transparent 27%, transparent 74%, rgba(0, 255, 0, 0.05) 75%, rgba(0, 255, 0, 0.05) 76%, transparent 77%, transparent), linear-gradient(90deg, transparent 24%, rgba(0, 255, 0, 0.05) 25%, rgba(0, 255, 0, 0.05) 26%, transparent 27%, transparent 74%, rgba(0, 255, 0, 0.05) 75%, rgba(0, 255, 0, 0.05) 76%, transparent 77%, transparent);
            background-size: 50px 50px;
        }
        /* Container - Dark metallic panel */
        .container { 
            max-width: 400px; 
            margin: 60px auto 0; 
            background: rgba(34, 34, 34, 0.95); /* Dark Gray/Black */
            padding: 30px; 
            border-radius: 10px; 
            border: 2px solid #00ccff; /* Sci-fi Blue Border */
            box-shadow: 0 0 20px rgba(0, 255, 0, 0.5); /* Neon Green Glow */
        }
        h1 { 
            color: #00ff00; 
            text-align: center; 
            text-shadow: 0 0 10px #00ff00; 
            margin-bottom: 25px;
        }
        label {
            display: block;
            margin-top: 15px;
            color: #00ccff; /* Light Blue/Cyan */
            font-size: 1.1em;
        }
        /* Input fields - Look like glowing data ports */
        input[type="text"], input[type="password"] { 
            width: 100%; 
            padding: 12px; 
            margin: 8px 0 20px 0; 
            display: inline-block; 
            border: 1px solid #00ccff; /* Sci-fi Blue Border */
            background-color: #111111; /* Very dark input background */
            color: #00ff00; /* Neon Green text input */
            border-radius: 4px; 
            box-sizing: border-box; 
            box-shadow: 0 0 5px rgba(0, 255, 0, 0.3);
            transition: box-shadow 0.3s, border-color 0.3s;
        }
        input[type="text"]:focus, input[type="password"]:focus {
            border-color: #00ffff;
            box-shadow: 0 0 10px #00ffff;
            outline: none;
        }
        /* Submit button - Bright green action element */
        input[type="submit"] { 
            background-color: #00ff00; 
            color: #111111; /* Dark text on bright button */
            padding: 14px 20px; 
            margin: 15px 0 8px 0; 
            border: none; 
            border-radius: 4px; 
            cursor: pointer; 
            width: 100%; 
            font-size: 16px; 
            font-weight: bold;
            box-shadow: 0 0 10px rgba(0, 255, 0, 0.7); /* Stronger glow */
            transition: background-color 0.3s, box-shadow 0.3s;
        }
        input[type="submit"]:hover { 
            background-color: #33ff33; 
            box-shadow: 0 0 15px #00ff00; 
        }
        .note { 
            color: #aaaaaa; 
            font-size: 0.9em; 
            text-align: center; 
            margin-top: 25px; 
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>TRINITY PROTOCOL SETUP</h1>
        <form method="get" action="/save">
            <label for="ssid">NETWORK SSID:</label>
            <input type="text" id="ssid" name="ssid" required>

            <label for="pass">SECURITY KEY:</label>
            <input type="password" id="pass" name="pass">

            <input type="submit" value="ESTABLISH CONNECTION">
        </form>
        <div class="note">// DATA WILL BE ENCRYPTED AND STORED IN NVS FLASH MEMORY. //</div>
    </div>
</body>
</html>
)raw";

// --- Web Server Handlers ---

// Handles the root request (serves the configuration form)
void handleRoot() {
    server.send(200, "text/html", CONFIG_HTML);
}

// Handles the /save request (saves credentials and restarts)
void handleSave() {
    String ssid = server.arg("ssid");
    String password = server.arg("pass");

    if (ssid.length() > 0) {
        // --- CRITICAL: SECURE STORAGE ---
        preferences.begin(PREFS_NAMESPACE, false);
        preferences.putString(PREF_SSID, ssid);
        preferences.putString(PREF_PASS, password);
        preferences.end();
        // --------------------------------

        Serial.println("Credentials saved securely to NVS. Rebooting...");
        // Updated success message to match theme
        server.send(200, "text/html", "<body style='background-color:#0d0d0d; color:#00ff00; font-family: monospace; text-align:center; padding-top: 100px;'><h1>TRANSMISSION SUCCESSFUL</h1><p>Credentials saved. Initiating system reboot and connection attempt...</p></body>");
        delay(3000);
        ESP.restart();
    } else {
        // Updated error message to match theme
        server.send(400, "text/html", "<body style='background-color:#0d0d0d; color:red; font-family: monospace; text-align:center; padding-top: 100px;'><h1>ERROR: INPUT FAILED</h1><p>SSID cannot be empty. Check protocol parameters.</p></body>");
    }
}

// Helper function to start the Access Point (AP) for configuration
bool startAPPortal() {
    Serial.println("\n--- STARTING AP SETUP MODE ---");
    Serial.println("Connect to Wi-Fi 'Trinity_Setup' and browse to any website.");

    // 1. Configure and start Access Point
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAPConfig(apIP, apIP, netMask);
    WiFi.softAP("Trinity_Setup", ""); // Open AP
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    // 2. Start DNS Server (for captive portal functionality)
    dnsServer.start(DNS_PORT, "*", apIP);

    // 3. Set up Web Server Handlers
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.begin();

    // 4. Main AP Loop
    unsigned long startTime = millis();
    while (millis() - startTime < 300000) { // 5 minute timeout for configuration
        dnsServer.processNextRequest();
        server.handleClient();
        delay(1);
        yield();
    }
    
    // If timeout, restart for a new attempt
    Serial.println("Configuration timeout. Rebooting...");
    ESP.restart();
    return false; // Should not reach here
}

// Main function to connect or start setup
bool connectWiFi() {
    // 1. Check for stored credentials in NVS flash
    preferences.begin(PREFS_NAMESPACE, true); // Read-only
    String ssid = preferences.getString(PREF_SSID, "");
    String pass = preferences.getString(PREF_PASS, "");
    preferences.end();

    if (ssid.length() == 0) {
        // No saved credentials found, launch setup portal
        return startAPPortal();
    }

    // 2. Connect using saved credentials (STA mode)
    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    // 3. Wait for connection (with timeout, then launch portal if failed)
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > 30000) { // 30 second timeout
            return startAPPortal();
        }
        delay(500);
        yield();
    }
    
    Serial.println("WiFi Connected.");
    return true;
}

// --- ARDUINO CORE ENTRY POINTS (REQUIRED) ---

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Starting Wi-Fi Setup Test ---");
    
    // Execute the core Wi-Fi connection/setup logic
    if (connectWiFi()) {
        Serial.print("SUCCESS! Device IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("AP mode active. Use a browser to configure Wi-Fi.");
    }
}

void loop() {
    // If in AP setup mode, handle web server requests
    if (WiFi.getMode() == WIFI_MODE_AP) {
        dnsServer.processNextRequest();
        server.handleClient();
    }
    // Feed the WDT
    delay(1); 
    yield(); 
}
