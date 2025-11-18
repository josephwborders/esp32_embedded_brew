/*
Project: ESP32 Embedded Brew v1.0
Author: Joseph Borders
Device: brew.local
Firmware: Feb 17 2025 22:14
Board: ESP32-C3 Dev Module
*/

#include <Adafruit_BMP280.h>
#include <Adafruit_Sensor.h>
#include <ElegantOTA.h>
#include <ESPmDNS.h>
#include <U8g2lib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

/*------- Global Variable Config -------*/
// Networking config
const char *STA_SSID = "YOUR_WIFI_SSID";            // Home Wi-Fi SSID (STA mode)
const char *STA_PASS = "YOUR_WIFI_PASS";            // Home Wi-Fi password
const char *AP_SSID = "smart_coffee";               // SAP SSID (SAP mode)
const char *AP_PASS = "11111111";                   // SAP password
const char *HOSTNAME = "brew";                      // mDNS hostname -> http://brew.local/
const wifi_power_t WIFI_TX_POWER = WIFI_POWER_5dBm; // Set radio TX power 5 dBm
bool clientMode = false;                            // Wi-Fi operating mode variable
WebServer server(80);                               // HTTP server on port 80
// Relay config (coffee pot on/off)
const int RELAY_PIN = 2; // GPIO pin 2
// Sensor config
Adafruit_BMP280 bmp;       // Reference BMP280 as bmp
bool bmpOk = false;        // Sensor operation variable
const int I2C_SDA_PIN = 5; // GPIO pin 5 - Sensor data pin
const int I2C_SCL_PIN = 6; // GPIO pin 6 - Sensor clock pin
// UI config
const float TEMP_MIN = 20.0f;                                // Temp gauge range min
const float TEMP_MAX = 80.0f;                                // Temp gauge range max
const float PRESS_MIN = 980.0f;                              // Pressure gauge min
const float PRESS_MAX = 1030.0f;                             // Pressure gauge max
bool brewOn = false;                                         // Brew state variable
unsigned long brewOnSince = 0;                               // Brew time variable
unsigned long bootMillis = 0;                                // Uptime variable
const unsigned long BREW_AUTO_OFF_MS = 40UL * 60UL * 1000UL; // Brew auto-off variable - 40 mins
// OLED display config
#define OLED_SDA 5
#define OLED_SCL 6
#define OLED_ADDR 0x3C
#define DISP U8G2_SSD1306_72X40_ER_F_HW_I2C
DISP u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

/*------- Setup Function -------*/
void setup()
{
    // On boot
    Serial.begin(115200); // Begin serial - 115200 baud rate
    delay(1200);          // Wait 1200ms
    Serial.println();     // Print boot message
    Serial.println("\n[BOOT] Client mode first, AP mode if needed, then OTA & mDNS");
    bootMillis = millis();                // Get current ms to set bootMillis
    pinMode(RELAY_PIN, OUTPUT);           // Set GPIO pin 2 (relay) to output mode
    digitalWrite(RELAY_PIN, HIGH);        // Set GPIO pin 2 idle state (no press)
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); // Initialize inter-integrated circuit (I2C) for BMP 280
    u8g2.begin();                         // Initialize OLED display
    u8g2.setPowerSave(1);                 // Put the OLED to sleep

    // Initialize BMP280 temperature and pressure sensor
    if (bmp.begin(0x76))
    {                 // Try sensor at address 0x76
        bmpOk = true; // Set sensor status bool true
        Serial.println("[BMP280] Sensor detected at 0x76");
    }
    else if (bmp.begin(0x77))
    {                 // Try sensor at address 0x77
        bmpOk = true; // Set sensor status bool true
        Serial.println("[BMP280] Sensor detected at 0x77");
    }
    else
    {                  // If sensor not found
        bmpOk = false; // Set sensor status bool false
        Serial.println("[BMP280] NOT FOUND");
    }

    // Initialize networking
    if (connectAsClientWithTimeout(60000))
    {                      // Try connecting to local network for 60 seconds
        clientMode = true; // Set client mode status bool true
    }
    else
    {                       // Switch to access point mode if local connection fails
        clientMode = false; // Set client mode status bool false
        startAccessPoint(); // Start access point mode
    }

    // Initialize multicast DNS
    if (MDNS.begin(HOSTNAME))
    {                                       // Start mDNS - point LAN requests to ESP32 at http://brew.local/
        MDNS.addService("http", "tcp", 80); // Setup HTTP and TCP services on port 80
        Serial.printf("[mDNS] http://%s.local/\n", HOSTNAME);
    }
    else
    { // Print message if mDNS fails
        Serial.println("[mDNS] Error starting mDNS");
    }

    // Initialize server and setup page routes
    server.on("/", handleRoot);                  // Page route for root - brew.local/
    server.on("/press", HTTP_POST, handlePress); // Page route to handle button press/relay operation
    server.on("/metrics", handleMetrics);        // Page route for ESP32 and sensor metrics
    server.onNotFound(handleNotFound);           // Page route for 404
    ElegantOTA.begin(&server);                   // Start OTA service and serve at brew.local/update
    server.begin();                              // Start server and print message
    Serial.printf("[HTTP] Server started on port 80\n[OTA] ElegantOTA ready at /update\n");
}

/*------- Loop Function -------*/
void loop()
{
    server.handleClient(); // Handle web requests and send page responses
    if (brewOn && brewOnSince > 0)
    { // Brew timer - Auto off toggle to reset UI state
        unsigned long now = millis();
        if (now - brewOnSince > BREW_AUTO_OFF_MS)
        {                    // Reset UI state 40 minutes after brew start
            brewOn = false;  // Reset bool
            brewOnSince = 0; // Reset timer
            Serial.println("[BREW] Auto UI off after 40-minute timeout");
        }
    }
}

/*------- Helper Functions -------*/

// Try to connect in STA mode for 60 seconds, return true on success, false on fail
bool connectAsClientWithTimeout(uint32_t timeoutMs)
{
    Serial.println("[WiFi] Trying STA mode..."); // Print msg to serial
    WiFi.mode(WIFI_STA);                         // Set Wi-Fi mode: station (client)
    WiFi.setTxPower(WIFI_TX_POWER);              // Set radio transmit power
    WiFi.begin(STA_SSID, STA_PASS);              // Start connection with home network SSID and password
    WiFi.setSleep(true);                         // Allow modem-sleep between packets
    uint32_t start = millis();                   // Local variable used for 60s counter
    // While status is not connected, print "." to serial every 0.5s
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs)
    {
        delay(500);
        Serial.print(".");
    }
    // Print new line to serial after timer completes or Wi-Fi connection succeeds
    Serial.println();
    // Return true or false based on Wi-Fi connection status
    if (WiFi.status() == WL_CONNECTED)
    { // Return true and print serial msg on connection success
        Serial.printf("[WiFi] Connected to %s. IP: %s\n", STA_SSID, WiFi.localIP().toString().c_str());
        return true;
    }
    else
    {
        // Return false on connection fail, print serial msg, disconnect Wi-Fi and erase AP
        Serial.println("[WiFi] Client mode connection timed out, starting smart_coffee access point.");
        WiFi.disconnect(true, true);
        return false;
    }
}

// Bring up smart_coffee access point on home network connection fail
void startAccessPoint()
{
    Serial.println("[WiFi] Starting smart_coffee access point...");
    WiFi.mode(WIFI_AP);                      // Set Wi-Fi mode: soft access point
    bool ok = WiFi.softAP(AP_SSID, AP_PASS); // Bring up access point and return true on success
    WiFi.setTxPower(WIFI_TX_POWER);          // Set radio transmit power
    if (ok)
    {
        Serial.printf("[WiFi] Access point started successfully. IP address: %s\n", WiFi.softAPIP().toString().c_str());
    }
    else
    {
        Serial.println("[WiFi] Failed to start access point.");
    }
}

// Root page definition
void handleRoot()
{
    // Wi-Fi / status snapshot
    const char *modeText = clientMode ? "Station (client)" : "Access Point"; // Set Wi-Fi mode text
    const char *network = clientMode ? STA_SSID : AP_SSID;                   // Get SSID based on Wi-Fi mode
    IPAddress ip = clientMode ? WiFi.localIP() : WiFi.softAPIP();            // Get IP based on Wi-Fi mode
    // Sensor snapshot
    float temp, press;    // Variables for temp and pressure
    readBmp(temp, press); // Get BMP280 sensor values
    // Build HTML
    String html;        // HTML for root page at brew.local/
    html.reserve(5000); // Prevent fragmentation - speed up page render
    html = F(
        "<!doctype html><html lang='en'><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Embedded Brew Control Panel</title>"
        "<style>"
        "body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;"
        "background:#05060a;color:#e5e7eb;display:flex;justify-content:center;align-items:center;"
        "min-height:100vh;padding:16px;}"
        ".card{background:#111827;border-radius:18px;padding:24px 22px 28px;max-width:420px;width:100%;"
        "box-shadow:0 18px 45px rgba(0,0,0,.45),0 2px 8px rgba(0,0,0,.6);}"
        "h1{margin:0 0 10px;font-size:1.4rem;color:#f9fafb;text-align:center;}"
        ".stats{margin-top:4px;margin-bottom:10px;font-size:.9rem;color:#e5e7eb;}"
        ".stats p{margin:2px 0;}"
        ".mug-wrap{display:flex;justify-content:center;margin:14px 0 6px;}"
        ".mug{position:relative;width:80px;height:65px;}"
        ".mug-body{position:absolute;bottom:0;left:4px;width:70px;height:54px;background:#f9faf5;"
        "border-radius:16px 16px 18px 18px;}"
        ".mug-handle{position:absolute;right:-14px;top:14px;width:22px;height:28px;border:4px solid #f9faf5;"
        "border-left:none;border-radius:0 18px 18px 0;}"
        ".steam{position:absolute;width:8px;height:22px;border-radius:999px;"
        "border:2px solid #60a5fa;border-bottom:none;opacity:0;transition:opacity .35s ease-out,transform 1.4s ease-out;}"
        ".steam.s1{left:18px;top:-20px;transform:translateY(6px);}"
        ".steam.s2{left:38px;top:-22px;transform:translateY(8px);}"
        ".mug.on .steam{opacity:1;}"
        ".mug.on .steam.s1{transform:translateY(0);}"
        ".mug.on .steam.s2{transform:translateY(0);}"
        ".gauges{display:flex;justify-content:space-between;margin-top:14px;gap:10px;}"
        ".gauge{flex:1;text-align:center;font-size:.8rem;}"
        ".gauge-svg{width:100%;display:block;}"
        ".g-arc{fill:none;stroke:#374151;stroke-width:6;stroke-linecap:round;}"
        ".g-tick{stroke:#4b5563;stroke-width:2;stroke-linecap:round;}"
        ".g-needle{stroke:#e5e7eb;stroke-width:2.2;stroke-linecap:round;"
        "transform-origin:100px 100px;transform:rotate(-90deg);transition:transform .25s ease-out;}"
        ".g-center{fill:#111827;stroke:#4b5563;stroke-width:2;}"
        ".gauge-value{margin-top:6px;font-size:.95rem;color:#f9fafb;}"
        ".gauge-label{margin-top:0;font-size:.8rem;color:#9ca3af;letter-spacing:.04em;text-transform:uppercase;}"
        ".sensor-status{margin-top:6px;font-size:.8rem;color:#f97373;text-align:center;}"
        ".btn-main{display:block;width:100%;margin-top:18px;padding:11px 18px;border-radius:999px;"
        "border:none;font-weight:600;font-size:.95rem;cursor:pointer;background:#2563eb;color:white;}"
        ".btn-main:active{transform:translateY(1px);}"
        ".ota-row{margin-top:8px;font-size:.75rem;color:#9ca3af;text-align:center;}"
        ".ota-row code{background:#020617;border-radius:6px;padding:1px 4px;font-size:.75rem;}"
        ".ota-link{display:inline-block;margin-top:4px;font-size:.78rem;color:#60a5fa;text-decoration:none;}"
        ".ota-link:hover{text-decoration:underline;}"
        ".link-row{text-align:center;margin-top:6px;font-size:.75rem;}"
        ".link-row a{color:#60a5fa;text-decoration:none;}"
        ".link-row a:hover{text-decoration:underline;}"
        "</style></head><body><main class='card'>"
        "<h1>Embedded Brew Control Panel</h1>");
    // Stats block
    html += "<div class='stats'>"
            "<p id='uptime'>Uptime: " + uptimeString() + "</p>"
            "<p>Wi-Fi mode: " + String(modeText) + "</p>"
            "<p>Network: " + String(network) + "</p>"
            "<p>IP address: " + ip.toString() + "</p>"
            "<p>UI page: http://brew.local/</p>"
            "</div>";
    // Mug
    html += "<div class='mug-wrap'><div id='mug' class='mug";
    if (brewOn)
        html += " on";
    html += "'>"
            "<div class='steam s1'></div>"
            "<div class='steam s2'></div>"
            "<div class='mug-body'></div>"
            "<div class='mug-handle'></div>"
            "</div></div>";
    // Gauges wrapper
    html += "<div class='gauges'>";
    // Temperature gauge SVG + value
    html +=
        "<div class='gauge gauge-temp'>"
        "<svg class='gauge-svg' viewBox='0 0 200 120'>"
        "<path class='g-arc' d='M20 100 A80 80 0 0 1 180 100' />"
        "<g class='g-ticks'>"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(-60 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(-30 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(30 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(60 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(90 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(-90 100 100)' />"
        "</g>"
        "<line id='tempNeedle' class='g-needle' x1='100' y1='100' x2='100' y2='28' />"
        "<circle class='g-center' cx='100' cy='100' r='6' />"
        "</svg>";
    if (bmpOk && !isnan(temp))
    {
        html += "<div id='tempValue' class='gauge-value'>" + String(temp, 1) + " &deg;C</div>";
    }
    else
    {
        html += "<div id='tempValue' class='gauge-value'>-- &deg;C</div>";
    }
    html += "<div class='gauge-label'>TEMPERATURE</div></div>";
    // Pressure gauge SVG + value
    html +=
        "<div class='gauge gauge-press'>"
        "<svg class='gauge-svg' viewBox='0 0 200 120'>"
        "<path class='g-arc' d='M20 100 A80 80 0 0 1 180 100' />"
        "<g class='g-ticks'>"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(-60 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(-30 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(30 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(60 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(90 100 100)' />"
        "<line class='g-tick' x1='100' y1='28' x2='100' y2='36' transform='rotate(-90 100 100)' />"
        "</g>"
        "<line id='pressNeedle' class='g-needle' x1='100' y1='100' x2='100' y2='28' />"
        "<circle class='g-center' cx='100' cy='100' r='6' />"
        "</svg>";
    if (bmpOk && !isnan(press))
    {
        html += "<div id='pressValue' class='gauge-value'>" + String(press, 1) + " hPa</div>";
    }
    else
    {
        html += "<div id='pressValue' class='gauge-value'>-- hPa</div>";
    }
    html += "<div class='gauge-label'>PRESSURE</div></div></div>";
    // Sensor status line
    if (!bmpOk)
    {
        html += "<p id='sensorStatus' class='sensor-status'>Sensor error (BMP280 not detected)</p>";
    }
    else
    {
        html += "<p id='sensorStatus' class='sensor-status' style='display:none;'>Sensor error</p>";
    }
    // Main button (form POST to /press)
    html += "<form method='POST' action='/press'><button id='brewButton' class='btn-main' type='submit'>" + String(brewOn ? "Turn Off" : "Start Brewing") + "</button></form>";
    html += "<div class='ota-row'>OTA update at <code>/update</code><br>"
            "<a class='ota-link' href='/update'>Open OTA Update</a></div>";
    // JS for polling /metrics and updating UI (angle range -90..+90, 2.5s poll, smoothed needles)
    html += F("<script>"
              "const TEMP_MIN=");
    html += String(TEMP_MIN, 1);
    html += F(",TEMP_MAX=");
    html += String(TEMP_MAX, 1);
    html += F(",PRESS_MIN=");
    html += String(PRESS_MIN, 1);
    html += F(",PRESS_MAX=");
    html += String(PRESS_MAX, 1);
    html += F(",GAUGE_MIN_ANGLE=-90,GAUGE_MAX_ANGLE=90;"
              "function clamp(v,min,max){return v<min?min:(v>max?max:v);} "
              "let lastTempAngle=null,lastPressAngle=null;"
              "function smoothAngle(target,last,alpha){"
              "if(last===null||isNaN(last))return target;"
              "return last+(target-last)*alpha;"
              "}"
              "function updateFromMetrics(data){"
              "if(data.uptime){var u=document.getElementById('uptime');if(u)u.textContent='Uptime: '+data.uptime;}"
              "var sensorOk=!!data.sensor_ok;"
              "var sensorMsg=document.getElementById('sensorStatus');"
              "if(sensorMsg){sensorMsg.style.display=sensorOk?'none':'block';}"
              "if(sensorOk){"
              "var t=data.temp_c;var p=data.pressure_hpa;"
              "var tv=document.getElementById('tempValue');"
              "var pv=document.getElementById('pressValue');"
              "if(tv){tv.innerHTML=(t!=null? t.toFixed(1)+' &deg;C':'-- &deg;C');}"
              "if(pv){pv.textContent=(p!=null? p.toFixed(1)+' hPa':'-- hPa');}"
              "var tn=document.getElementById('tempNeedle');"
              "if(tn && t!=null){"
              "var tnVal=clamp(t,TEMP_MIN,TEMP_MAX);"
              "var tnNorm=(tnVal-TEMP_MIN)/(TEMP_MAX-TEMP_MIN);"
              "var tnAng=GAUGE_MIN_ANGLE+(GAUGE_MAX_ANGLE-GAUGE_MIN_ANGLE)*tnNorm;"
              "tnAng=smoothAngle(tnAng,lastTempAngle,0.35);"
              "lastTempAngle=tnAng;"
              "tn.style.transform='rotate('+tnAng+'deg)';}"
              "var pn=document.getElementById('pressNeedle');"
              "if(pn && p!=null){"
              "var pnVal=clamp(p,PRESS_MIN,PRESS_MAX);"
              "var pnNorm=(pnVal-PRESS_MIN)/(PRESS_MAX-PRESS_MIN);"
              "var pnAng=GAUGE_MIN_ANGLE+(GAUGE_MAX_ANGLE-GAUGE_MIN_ANGLE)*pnNorm;"
              "pnAng=smoothAngle(pnAng,lastPressAngle,0.35);"
              "lastPressAngle=pnAng;"
              "pn.style.transform='rotate('+pnAng+'deg)';}"
              "}"
              "var mug=document.getElementById('mug');"
              "var btn=document.getElementById('brewButton');"
              "if(data.brew_on){"
              "if(mug)mug.classList.add('on');"
              "if(btn)btn.textContent='Turn Off';"
              "}else{"
              "if(mug)mug.classList.remove('on');"
              "if(btn)btn.textContent='Start Brewing';"
              "}"
              "}"
              "function pollMetrics(){"
              "fetch('/metrics').then(function(r){return r.json();}).then(updateFromMetrics)"
              ".catch(function(e){console && console.warn && console.warn('metrics error',e);});"
              "}"
              "document.addEventListener('DOMContentLoaded',function(){"
              "pollMetrics();"
              "setInterval(pollMetrics,2500);"
              "});"
              "</script>");
    html += "</main></body></html>";
    server.send(200, "text/html", html); // Send root HTML
}

// Get BMP280 sensor values
void readBmp(float &tempOut, float &pressOut)
{
    if (!bmpOk)
    { // Set NANs if sensor bool is not true
        tempOut = NAN;
        pressOut = NAN;
        return; // Exit if sensor is not up
    }
    tempOut = bmp.readTemperature();        // Get temperature
    pressOut = bmp.readPressure() / 100.0f; // Get pressure
}

// Build uptime char buffer
String uptimeString()
{
    unsigned long ms = millis();   // Get time in ms since boot
    unsigned long s = ms / 1000UL; // Get seconds
    unsigned long m = s / 60UL;    // Get minutes
    unsigned long h = m / 60UL;    // Get hours
    char buf[40];                  // Initialize 40 byte char buffer
    snprintf(buf, sizeof(buf), "%luh %lum %lus", (unsigned)h, (unsigned)(m % 60), (unsigned)(s % 60));
    return String(buf);
}

// Toggle relay and UI state
void handlePress()
{
    const uint16_t PRESS_MS = 250; // Relay close duration - 250ms
    Serial.printf("[RELAY] Simulating button press for %u ms\n", PRESS_MS);
    // UI handling
    brewOn = !brewOn;                    // Toggle logical brew state for UI presentation
    brewOnSince = brewOn ? millis() : 0; // Start/reset 40 minute UI timer
    // Relay handling
    digitalWrite(RELAY_PIN, LOW);             // Set GPIO2 low - simulate physical button press
    delay(PRESS_MS);                          // Hold for 250ms
    digitalWrite(RELAY_PIN, HIGH);            // Set GPIO2 high - simulate physical button release
    server.sendHeader("Location", "/", true); // Refresh page to present updated UI state
    server.send(303, "text/plain", "");       // Send page refresh request
}

// JSON metrics endpoint for JS polling
void handleMetrics()
{
    float temp, press;         // Declare variables for temp and pressure
    readBmp(temp, press);      // Get BMP280 sensor values
    char buf[256];             // Initialize 256 byte char buffer
    snprintf(buf, sizeof(buf), // Build JSON
             "{"
             "\"uptime\":\"%s\","
             "\"temp_c\":%s,"
             "\"pressure_hpa\":%s,"
             "\"sensor_ok\":%s,"
             "\"brew_on\":%s"
             "}",
             uptimeString().c_str(),
             (bmpOk && !isnan(temp)) ? String(temp, 2).c_str() : "null",
             (bmpOk && !isnan(press)) ? String(press, 2).c_str() : "null",
             bmpOk ? "true" : "false",
             brewOn ? "true" : "false");
    server.send(200, "application/json", buf); // Send JSON
}

// Handle invalid page routes
void handleNotFound()
{
    server.send(404, "text/plain", "Not found"); // Send 404
}