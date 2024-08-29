#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

const char* ssid = "Wi-Fi";  // WiFi SSID
const char* password = "LetsBak3It!1337";  // WiFi password

String apiURL = "https://api.sunrise-sunset.org/json?lat=51.76625442504883&lng=14.3240385055542&formatted=0";

WiFiClientSecure client; // Use WiFiClientSecure for HTTPS connections

unsigned long previousMillis = 0;  // will store last time API was fetched or LED was updated
const long interval = 200;         // interval at which to blink (milliseconds)
const long apiInterval = 60000;    // interval to refresh API data (milliseconds)
bool isTwinkling = false;          // state to track twinkling status

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);  // Set GPIO 2 as an output for the LED
  connectToWiFi();     // Connect to WiFi network
  configTime("CET-1CEST,M3.5.0,M10.5.0/3", "de.pool.ntp.org"); // Set timezone and NTP server for Germany
  client.setInsecure();  // Skip SSL certificate verification for testing purposes
}

void loop() {
  unsigned long currentMillis = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (currentMillis - previousMillis >= apiInterval) {  // Update every 60 seconds
      previousMillis = currentMillis;
      fetchSunTimes();  // Fetch sunrise and sunset times
      displayCurrentTime();  // Display current time
    }
  } else {
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
    deserializeAndExecute(payload);
  } else {
    Serial.print("Failed to retrieve data. HTTP response code: ");
    Serial.println(httpCode);
  }
  http.end();  // End the connection
}

void displayCurrentTime() {
  time_t now = time(nullptr);
  struct tm *tm_now = localtime(&now);
  char buf[64];
  strftime(buf, sizeof(buf), "%c", tm_now);
  Serial.print("Current time: ");
  Serial.println(buf);
}

void deserializeAndExecute(String json) {
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, json);

  if (!error) {
    String sunrise = doc["results"]["sunrise"].as<String>();
    String sunset = doc["results"]["sunset"].as<String>();

    if (isWithinSunsetSunrise(sunrise, sunset)) {
      Serial.println("It's night time. LED will twinkle.");
      startTwinkling();
    } else {
      Serial.println("Daytime. LED will be off.");
      stopTwinkling();
    }
  } else {
    Serial.print("JSON deserialization failed: ");
    Serial.println(error.c_str());
  }
}

bool isWithinSunsetSunrise(String sunrise, String sunset) {
  struct tm sunr = {};
  struct tm suns = {};
  strptime(sunrise.c_str(), "%I:%M:%S %p", &sunr);
  strptime(sunset.c_str(), "%I:%M:%S %p", &suns);

  time_t now = time(nullptr);
  struct tm *tm_now = localtime(&now);
  sunr.tm_year = suns.tm_year = tm_now->tm_year;
  sunr.tm_mon = suns.tm_mon = tm_now->tm_mon;
  sunr.tm_mday = suns.tm_mday = tm_now->tm_mday;

  time_t sunriseTime = mktime(&sunr);
  time_t sunsetTime = mktime(&suns);

  return now >= sunsetTime || now < sunriseTime;
}

void startTwinkling() {
  isTwinkling = true;
  Serial.println("Starting twinkling effect...");
  previousMillis = millis(); // reset the timer for LED blinking
}

void stopTwinkling() {
  isTwinkling = false;
  Serial.println("Stopping twinkling effect. LED is off.");
  digitalWrite(2, LOW);  // Ensure LED is off when it's not twinkling time
}

void handleTwinkling() {
  if (isTwinkling) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      int ledState = digitalRead(2);
      digitalWrite(2, !ledState);
    }
  }
}