#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

const char* ssid = "Wi-Fi";  // WiFi SSID
const char* password = "LetsBak3It!1337";  // WiFi password

ESP8266WebServer server(80); // Create a web server on port 80

String apiURL = "https://api.sunrise-sunset.org/json?lat=51.76625442504883&lng=14.3240385055542&formatted=0";

WiFiClientSecure client; // Use WiFiClientSecure for HTTPS connections

unsigned long previousMillis = 0;  // will store last time API was fetched or LED was updated
const long interval = 200;         // interval at which to blink (milliseconds)
const long apiInterval = 60000;    // interval to refresh API data (milliseconds)
bool isTwinkling = false;          // state to track twinkling status
bool lastTwinklingState = false;   // track the last state to avoid duplicate messages

String currentTimeStr;
String sunriseTimeStr;
String sunsetTimeStr;
String wifiStatus = "Disconnected";
String apiStatus = "Not Fetched";
String ledStatus = "Off";
String ledMessage = "Daytime. LED will be Off.\nStopping Twinkling Effect. LED is Off.";

void setup() {
    Serial.begin(115200);
    pinMode(2, OUTPUT);  // Set GPIO 2 as an output for the LED
    digitalWrite(2, HIGH);  // Turn off the LED initially
    connectToWiFi();     // Connect to WiFi network
    configTime("CET-1CEST,M3.5.0,M10.5.0/3", "de.pool.ntp.org"); // Set timezone and NTP server for Germany
    client.setInsecure();  // Skip SSL certificate verification for testing purposes

    server.on("/", handleRoot); // Define the root route
    server.begin();  // Start the server
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient(); // Handle incoming client requests
    unsigned long currentMillis = millis();

    if (WiFi.status() == WL_CONNECTED) {
        wifiStatus = "Connected";
        if (currentMillis - previousMillis >= apiInterval) {  // Update every 60 seconds
            previousMillis = currentMillis;
            fetchSunTimes();  // Fetch sunrise and sunset times
            updateCurrentTime();  // Update current time string
        }
    } else {
        wifiStatus = "Disconnected";
        Serial.println("WiFi not connected. Reconnecting...");
        connectToWiFi();
    }
    handleTwinkling();  // Handle LED twinkling based on the last known state
}

void connectToWiFi() {
    WiFi.begin(ssid, password);
    Serial.println("Attempting to connect to WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected successfully.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void fetchSunTimes() {
    HTTPClient http;
    http.begin(client, apiURL);  // Begin preparing a connection
    int httpCode = http.GET();   // Perform the HTTP GET request

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("HTTP request successful. Parsing API response...");
        apiStatus = "Success";
        deserializeAndExecute(payload);
    } else {
        Serial.print("Failed to retrieve data. HTTP response code: ");
        Serial.println(httpCode);
        apiStatus = "Failed";
    }
    http.end();  // End the connection
}

void updateCurrentTime() {
    time_t now = time(nullptr);
    struct tm *tm_now = localtime(&now);

    // Format time as 12-hour clock with AM/PM
    char buf[64];
    strftime(buf, sizeof(buf), "%I:%M:%S %p", tm_now);

    currentTimeStr = String(buf);
}

void deserializeAndExecute(String json) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, json);

    if (!error) {
        String sunriseUTC = doc["results"]["sunrise"].as<String>();
        String sunsetUTC = doc["results"]["sunset"].as<String>();

        // Convert the UTC times to local time (CET/CEST)
        sunriseTimeStr = convertUTCToLocalTime(sunriseUTC);
        sunsetTimeStr = convertUTCToLocalTime(sunsetUTC);

        Serial.print("Sunrise (Local): ");
        Serial.println(sunriseTimeStr);
        Serial.print("Sunset (Local): ");
        Serial.println(sunsetTimeStr);

        bool isNightTime = isWithinSunsetSunrise(sunriseTimeStr, sunsetTimeStr);
        if (isNightTime && !lastTwinklingState) {
            Serial.println("Night Time. LED will be On.\nStart Twinkling Effect. LED will be On.");
            startTwinkling();
        } else if (!isNightTime && lastTwinklingState) {
            Serial.println("Daytime. LED will be Off.\nStopping Twinkling Effect. LED is Off.");
            stopTwinkling();
        }
        lastTwinklingState = isNightTime;
    } else {
        Serial.print("JSON deserialization failed: ");
        Serial.println(error.c_str());
        apiStatus = "Failed to Parse";
    }
}

String convertUTCToLocalTime(String timeStr) {
    struct tm tm;
    char ampm[3]; // Buffer to store AM/PM
    strptime(timeStr.c_str(), "%Y-%m-%dT%I:%M:%S", &tm);  // Parse the ISO 8601 datetime string without AM/PM
    sscanf(timeStr.c_str(), "%*[^T]T%*d:%*d:%*d%2s", ampm);  // Extract the AM/PM part

    // Adjust for AM/PM manually
    if (strcasecmp(ampm, "PM") == 0 && tm.tm_hour < 12) {
        tm.tm_hour += 12;  // Convert PM times to 24-hour format
    } else if (strcasecmp(ampm, "AM") == 0 && tm.tm_hour == 12) {
        tm.tm_hour = 0;  // Convert 12 AM to 00:00 in 24-hour format
    }

    // Convert time to UTC timestamp
    time_t utcTime = mktime(&tm);

    // Manually adjust the time for CET/CEST
    time_t localTime;
    if (tm.tm_isdst > 0) {  // Check if daylight saving time is in effect
        localTime = utcTime + 2 * 3600;  // Add 2 hours for CEST
    } else {
        localTime = utcTime + 1 * 3600;  // Add 1 hour for CET
    }

    // Convert back to tm structure
    struct tm *tm_local = localtime(&localTime);

    // Format time as 24-hour clock
    char buffer[12];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", tm_local);

    return String(buffer);
}

bool isWithinSunsetSunrise(String sunrise, String sunset) {
    struct tm sunr = {};
    struct tm suns = {};
    strptime(sunrise.c_str(), "%H:%M:%S", &sunr);  // Parse as 24-hour format
    strptime(sunset.c_str(), "%H:%M:%S", &suns);  // Parse as 24-hour format

    time_t now = time(nullptr);
    struct tm *tm_now = localtime(&now);

    // Ensure the year, month, and day are the same for accurate comparison
    sunr.tm_year = suns.tm_year = tm_now->tm_year;
    sunr.tm_mon = suns.tm_mon = tm_now->tm_mon;
    sunr.tm_mday = suns.tm_mday = tm_now->tm_mday;

    time_t sunriseTime = mktime(&sunr);
    time_t sunsetTime = mktime(&suns);

    Serial.print("Current Time (24h format): ");
    Serial.print(tm_now->tm_hour);
    Serial.print(":");
    Serial.print(tm_now->tm_min);
    Serial.print(":");
    Serial.println(tm_now->tm_sec);

    // Correct logic to determine if it's nighttime or daytime
    if (now >= sunriseTime && now < sunsetTime) {
        ledMessage = "Daytime. LED will be Off.\nStopping Twinkling Effect. LED is Off.";
        return false;  // It's daytime
    } else {
        ledMessage = "Night Time. LED will be On.\nStart Twinkling Effect. LED will be On.";
        return true;  // It's nighttime
    }
}

void startTwinkling() {
    isTwinkling = true;
    ledStatus = "Twinkling";
    Serial.println("Night Time. LED will be On.\nStart Twinkling Effect. LED will be On.");
    previousMillis = millis(); // reset the timer for LED blinking
}

void stopTwinkling() {
    isTwinkling = false;
    ledStatus = "Off";
    ledMessage = "Daytime. LED will be Off.\nStopping Twinkling Effect. LED is Off.";
    Serial.println(ledMessage);
    digitalWrite(2, HIGH);  // Ensure LED is off when it's not twinkling time
}

void handleTwinkling() {
    if (isTwinkling) {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            int ledState = digitalRead(2);
            digitalWrite(2, !ledState);  // Toggle the LED state to create twinkling effect
        }
    } else {
        digitalWrite(2, HIGH);  // Ensure the LED is completely off during daytime
    }
}

void handleRoot() {
    String html = R"=====(
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Sunrise & Sunset Times</title>
        <link href="https://fonts.googleapis.com/css2?family=Poppins:wght@400;500;600;700&display=swap" rel="stylesheet">
        <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.0.0-beta3/css/all.min.css">
        <style>
            body {
                font-family: 'Poppins', sans-serif;
                margin: 0;
                padding: 20px;
                display: flex;
                justify-content: center;
                align-items: center;
                height: 100vh;
                transition: background-color 0.3s ease, color 0.3s ease;
            }
            .container {
                background-color: #ffffff;
                border-radius: 12px;
                padding: 25px;
                box-shadow: 0 10px 20px rgba(0, 0, 0, 0.1);
                text-align: center;
                max-width: 500px;
                width: 100%;
                transition: background-color 0.3s ease, color 0.3s ease;
            }
            h1 {
                color: #2c3e50;
                font-size: 28px;
                margin-bottom: 15px;
                transition: color 0.3s ease;
            }
            p {
                font-size: 18px;
                margin: 10px 0;
                transition: color 0.3s ease;
            }
            .status {
                display: flex;
                align-items: center;
                justify-content: space-between;
                padding: 15px;
                margin-bottom: 20px;
                border-radius: 10px;
                background-color: #ecf0f1;
                transition: background-color 0.3s ease;
            }
            .status-icon {
                font-size: 24px;
                margin-right: 10px;
                transition: color 0.3s ease;
            }
            .status-text {
                font-size: 18px;
                font-weight: 600;
                transition: color 0.3s ease;
            }
            .status-success {
                color: #27ae60;
            }
            .status-fail {
                color: #e74c3c;
            }
            .led-message {
                font-size: 16px;
                font-weight: 500;
                color: #16a085;
                margin-top: 15px;
                transition: color 0.3s ease;
                white-space: pre-wrap;
            }
            button {
                background-color: #3498db;
                border: none;
                padding: 12px 24px;
                border-radius: 5px;
                font-size: 16px;
                color: white;
                cursor: pointer;
                transition: background-color 0.3s ease;
                margin-top: 20px;
            }
            button:hover {
                background-color: #2980b9;
            }
            .toggle-container {
                display: flex;
                justify-content: center;
                margin-bottom: 15px;
            }
            .toggle-switch {
                position: relative;
                display: inline-block;
                width: 60px;
                height: 34px;
            }
            .toggle-switch input {
                opacity: 0;
                width: 0;
                height: 0;
            }
            .slider {
                position: absolute;
                cursor: pointer;
                top: 0;
                left: 0;
                right: 0;
                bottom: 0;
                background-color: #ccc;
                transition: 0.4s;
                border-radius: 34px;
            }
            .slider:before {
                position: absolute;
                content: "";
                height: 26px;
                width: 26px;
                left: 4px;
                bottom: 4px;
                background-color: white;
                transition: 0.4s;
                border-radius: 50%;
            }
            input:checked + .slider {
                background-color: #3498db;
            }
            input:checked + .slider:before {
                transform: translateX(26px);
            }
            body.dark-mode {
                background-color: #2c3e50;
                color: #ecf0f1;
            }
            .container.dark-mode {
                background-color: #34495e;
                color: #ecf0f1;
            }
            h1.dark-mode, p.dark-mode, .status-text.dark-mode, .led-message.dark-mode {
                color: #ecf0f1;
            }
            .status.dark-mode {
                background-color: #3b4a5a;
            }
            .slider.dark-mode {
                background-color: #3498db;
            }
            .toggle-container.dark-mode .slider {
                background-color: #1abc9c;
            }
            .toggle-container.dark-mode .slider:before {
                background-color: #16a085;
            }
        </style>
        <script>
            function refreshPage() {
                window.location.reload();
            }

            function toggleDarkMode() {
                var body = document.body;
                body.classList.toggle("dark-mode");
                var container = document.querySelector(".container");
                container.classList.toggle("dark-mode");
                var headers = document.querySelectorAll("h1");
                var paragraphs = document.querySelectorAll("p");
                var statuses = document.querySelectorAll(".status");
                var ledMessage = document.querySelector(".led-message");
                headers.forEach(function(h) {
                    h.classList.toggle("dark-mode");
                });
                paragraphs.forEach(function(p) {
                    p.classList.toggle("dark-mode");
                });
                statuses.forEach(function(s) {
                    s.classList.toggle("dark-mode");
                });
                ledMessage.classList.toggle("dark-mode");
            }
        </script>
    </head>
    <body>
        <div class="container">
            <div class="toggle-container">
                <label class="toggle-switch">
                    <input type="checkbox" onclick="toggleDarkMode()">
                    <span class="slider"></span>
                </label>
            </div>
            <h1>Device Status</h1>
            <div class="status">
                <i class="fas fa-wifi status-icon status-success"></i>
                <span class="status-text">Wi-Fi: [[WIFI_STATUS]]</span>
            </div>
            <div class="status">
                <i class="fas fa-cloud status-icon status-success"></i>
                <span class="status-text">API: [[API_STATUS]]</span>
            </div>
            <div class="status">
                <i class="fas fa-lightbulb status-icon status-success"></i>
                <span class="status-text">LED: [[LED_STATUS]]</span>
            </div>
            <h1>Current Time</h1>
            <p>[[CURRENT_TIME]]</p>
            <h1>Sunrise Time</h1>
            <p>[[SUNRISE_TIME]]</p>
            <h1>Sunset Time</h1>
            <p>[[SUNSET_TIME]]</p>
            <p class="led-message">[[LED_MESSAGE]]</p>
            <button onclick="refreshPage()">Refresh</button>
        </div>
    </body>
    </html>
    )=====";

    // Replace placeholders with actual values
    html.replace("[[CURRENT_TIME]]", currentTimeStr);
    html.replace("[[SUNRISE_TIME]]", sunriseTimeStr);
    html.replace("[[SUNSET_TIME]]", sunsetTimeStr);
    html.replace("[[WIFI_STATUS]]", wifiStatus);
    html.replace("[[API_STATUS]]", apiStatus);
    html.replace("[[LED_STATUS]]", ledStatus);
    html.replace("[[LED_MESSAGE]]", ledMessage);

    server.send(200, "text/html", html);
}
