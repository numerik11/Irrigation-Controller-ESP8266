//Im using a D1 R2 Uno style 8266 board. 
//With some variance in boards this pinout may need changing
//Rewritinging to use i2c i/o expansion would make pinout easier

#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <LittleFS.h>

// --- Global Pin Definitions ---
const int valvePins[4] = {D0, D3, D5, D6}; // Pins for 4 solenoids 
const int ledPin = D4;
const int mainsSolenoidPin = D7;           // Mains solenoid pin (Optional)
const int tankSolenoidPin = D8;            // Tank solenoid pin (Optional)
const int tankLevelPin = A0;               // Analog pin for tank level sensor (Optional)

// --- Global Objects ---
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiManager wifiManager;
ESP8266WebServer server(80);
WiFiClient client;

// --- Global Variables ---
String newSsid;
String newPassword;
String apiKey;
String city;
float dstAdjustment;  

unsigned long valveStartTime[4] = {0}; 
unsigned long elapsedTime[4] = {0};

bool valveOn[4] = {false};
bool raining = false;
bool weatherCheckRequired = false;
bool wifiConnected = false;
bool rainDelayEnabled = true;  // Default: Rain delay enabled

// --- NEW GLOBAL VARIABLES for wind Delayion ---
float windSpeedThreshold = 5.0;      // Default wind speed threshold in m/s
bool windCancelEnabled = false;      // Default: wind Delayion disabled

String condition; 
float temperature; 
float humidity; 
float windSpeed; 
float rain; 
unsigned long previousMillis = 0;   
const long interval = 10000;    // 10-second LCD update interval
String cachedWeatherData = "";
unsigned long lastWeatherUpdateTime = 0;
const unsigned long weatherUpdateInterval = 3600000; // 1 hour in milliseconds

const int numZones = 4; 
int startHour[numZones] = {0, 0, 0, 0};
int startMin[numZones] = {0, 0, 0, 0};
int startHour2[numZones] = {0, 0, 0, 0};
int startMin2[numZones] = {0, 0, 0, 0};
int duration[numZones] = {0, 0, 0, 0};
bool enableStartTime[numZones] = {true, true, true, true};  // Always enabled
bool enableStartTime2[numZones] = {false, false, false, false};
bool days[4][7] = {
  {false, true, false, true, false, true, false},  
  {false, true, false, true, false, true, false},
  {false, true, false, true, false, true, false},
  {false, true, false, true, false, true, false}
};
bool prevDays[4][7] = {
  {false, false, false, false, false, false, false},
  {false, false, false, false, false, false, false},
  {false, false, false, false, false, false, false},
  {false, false, false, false, false, false, false}
};

void setup() {
  Serial.begin(115200);
  
  // Initialize valve pins
  for (int i = 0; i < 4; i++) {
    pinMode(valvePins[i], OUTPUT);
    digitalWrite(valvePins[i], LOW);
  }
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  // Initialize solenoid source pins as outputs
  pinMode(mainsSolenoidPin, OUTPUT);
  digitalWrite(mainsSolenoidPin, LOW);
  pinMode(tankSolenoidPin, OUTPUT);
  digitalWrite(tankSolenoidPin, LOW);

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Irrigation");
  lcd.setCursor(5, 1);
  lcd.print("System");
  
  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }
      
  // Fallback LCD message
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("Connect to");
  lcd.setCursor(2, 1);
  lcd.print("ESPIRRIGATION");

  // Load saved configuration and schedule
  loadConfig();
  loadSchedule();

  // Set a timeout for WiFiManager (180 seconds)
  wifiManager.setTimeout(180);
  if (!wifiManager.autoConnect("ESPIrrigationAP")) {
    Serial.println("Failed to connect to WiFi. Restarting...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ESPIrrigation");
    lcd.setCursor(0, 1);
    lcd.print("IP: 192.168.4.1");
    ESP.restart();
  }

  // WiFi is connected at this point
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
    delay(3000);
  }

  // Fetch and display initial weather data
  String weatherData = getWeatherData();
  DynamicJsonDocument jsonResponse(1024);
  deserializeJson(jsonResponse, weatherData);
  float temp = jsonResponse["main"]["temp"].as<float>();
  int hum = jsonResponse["main"]["humidity"].as<int>();
  String weatherCondition = jsonResponse["weather"][0]["main"].as<String>();
  lcd.clear();
  int textLength = weatherCondition.substring(0, 10).length();
  int startPos = (textLength < 16) ? (16 - textLength) / 2 : 0;
  lcd.setCursor(startPos, 0);
  lcd.print(weatherCondition.substring(0, 10));
  lcd.setCursor(0, 1);
  lcd.print("Te:");
  lcd.print(temp);
  lcd.print("C Hu:");
  lcd.print(hum); 
  lcd.print("%");
  delay(3000); 
  lcd.noBacklight();

  // --- Setup time using configTime ---
  long timeOffsetSec = (long)(dstAdjustment * 3600);
  configTime(timeOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  while(now < 1000000000) {
    delay(500);
    now = time(nullptr);
  }

  // Setup OTA and HTTP routes
  ArduinoOTA.begin();
  ArduinoOTA.setHostname("ESPIrrigation");
  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/setup", HTTP_GET, handleSetupPage);
  server.on("/configure", HTTP_POST, handleConfigure); 

  // Setup valve manual control routes
  for (int i = 0; i < 4; i++) {
    server.on(("/valve/on" + String(i)).c_str(), HTTP_POST, [i]() { turnOnValveManual(i); });
    server.on(("/valve/off" + String(i)).c_str(), HTTP_POST, [i]() { turnOffValveManual(i); });
  }
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  unsigned long currentMillis = millis();

  bool valveActive = false;
  for (int i = 0; i < 4; i++) {
    if (valveOn[i]) {
      updateLCDForZone(i);
      valveActive = true;
      break;
    }
  }

  if (!valveActive) {
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;
      updateDetailsOnLCD();
    }
  }

  for (int i = 0; i < numZones; i++) {
    checkWateringSchedule(i);
    updateLCD();
  }

  if (WiFi.status() != WL_CONNECTED) {
    reconnectWiFi();
  }
  
  for (int i = 0; i < numZones; i++) { 
  if (valveOn[i]) {
  cancelWateringForWind();
  }
 }
}

bool checkForWind() {
  if (!windCancelEnabled) {
    return false;
  }
  updateCachedWeatherData();
  DynamicJsonDocument jsonResponse(1024);
  DeserializationError error = deserializeJson(jsonResponse, cachedWeatherData);
  if (error) {
    Serial.println("Failed to parse weather data (wind check).");
    return false;
  }
  float currentWindSpeed = jsonResponse["wind"]["speed"].as<float>();
  Serial.print("Wind Speed: ");
  Serial.println(currentWindSpeed);
  if (currentWindSpeed >= windSpeedThreshold) {
    Serial.println("High wind speed detected. Cancelling watering.");
    displayWindCancelMessage();
    return true;
  }
  return false;
}

void displayWindCancelMessage() {
  for (int i = 0; i < numZones; i++) {
    if (valveOn[i]) {
      turnOffValve(i);
      lcd.backlight();
      lcd.clear();
      lcd.setCursor(2, 0);
      lcd.print("High Wind");
      lcd.setCursor(1, 1);
      lcd.print("Watering Off");
      delay(60000); // Delay for 60 seconds (adjust as needed)
      lcd.noBacklight();
    }
  }
}

void cancelWateringForWind() {
  if (!windCancelEnabled) return;
  updateCachedWeatherData();
  DynamicJsonDocument jsonResponse(1024);
  DeserializationError error = deserializeJson(jsonResponse, cachedWeatherData);
  if (error) return;
  float currentWindSpeed = jsonResponse["wind"]["speed"].as<float>();
  if (currentWindSpeed >= windSpeedThreshold) {
    Serial.println("High wind detected during watering. Cancelling all active watering.");
    for (int i = 0; i < numZones; i++) {
      if (valveOn[i]) {
        turnOffValve(i);
        valveOn[i] = false;
      }
    }
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("Wind Delay");
    lcd.setCursor(4, 1);
    lcd.print("Water Off");
    delay(60000);
    lcd.noBacklight();
  }
}

bool checkForRain() {
  if (!rainDelayEnabled) {
    return false;
  }
  updateCachedWeatherData();
  DynamicJsonDocument jsonResponse(1024);
  DeserializationError error = deserializeJson(jsonResponse, cachedWeatherData);
  if (error) {
    Serial.println("Failed to parse weather data (rain check).");
    return false;
  }
  String weatherCondition = jsonResponse["weather"][0]["main"].as<String>();
  Serial.print("Weather condition: ");
  Serial.println(weatherCondition);
  bool isRaining = (weatherCondition.equalsIgnoreCase("Rain") || weatherCondition.equalsIgnoreCase("Drizzle"));
  if (isRaining) {
    Serial.println("Rain delay active. Skipping watering.");
    displayRainMessage();
  }
  return isRaining;
}

void displayRainMessage() { 
  for (int i = 0; i < numZones; i++) {
    if (valveOn[i]) {
      turnOffValve(i);
      lcd.backlight();
      lcd.clear();
      lcd.setCursor(4, 0);
      lcd.print("Rain Delay");
      lcd.setCursor(6, 1);
      lcd.print("Enabled");
      delay(60000); // Delay for 60 seconds
      lcd.noBacklight();
    }   
  }
}

String getWeatherData() {
  HTTPClient http;
  http.setTimeout(5000);
  String url = "http://api.openweathermap.org/data/2.5/weather?id=" + city + "&appid=" + apiKey + "&units=metric";
  http.begin(client, url);
  int httpResponseCode = http.GET();
  String payload = "{}";
  if (httpResponseCode > 0) {
    payload = http.getString();
  } else {
    Serial.println("Error: Unable to fetch weather data.");
  }
  http.end();
  return payload;
}

void updateCachedWeatherData() {
  unsigned long currentMillis = millis();
  if (cachedWeatherData == "" || (currentMillis - lastWeatherUpdateTime >= weatherUpdateInterval)) {
    cachedWeatherData = getWeatherData();
    lastWeatherUpdateTime = currentMillis;
  }
}

void updateDetailsOnLCD() {
  updateCachedWeatherData();
  static unsigned long lastToggleTime = 0;
  static bool showWeatherScreen = true;
  unsigned long currentMillis = millis();
  if (currentMillis - lastToggleTime >= 10000) {
    lastToggleTime = currentMillis;
    showWeatherScreen = !showWeatherScreen;
  }

  lcd.clear();
  
  if (showWeatherScreen) {
    DynamicJsonDocument jsonResponse(1024);
    DeserializationError error = deserializeJson(jsonResponse, cachedWeatherData);
    if (!error) {
      float temp = jsonResponse["main"]["temp"].as<float>();
      int hum = jsonResponse["main"]["humidity"].as<int>();
      String weatherCondition = jsonResponse["weather"][0]["main"].as<String>();
      int textLength = weatherCondition.substring(0, 10).length();
      int startPos = (textLength < 16) ? (16 - textLength) / 2 : 0;
      lcd.setCursor(startPos, 0);
      lcd.print(weatherCondition.substring(0, 10));
      lcd.setCursor(0, 1);
      lcd.print("Te:");
      lcd.print(temp, 1);
      lcd.print("C  Hu:");
      lcd.print(hum , 1);
      lcd.print("%");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Weather error");
    }
  } else {
    // New display: wind speed, time, and tank level
    // Get current time formatted as HH:MM:
    time_t nowTime = time(nullptr);
    struct tm * timeInfo = localtime(&nowTime);
    char timeStr[6]; // Format: "HH:MM"
    strftime(timeStr, sizeof(timeStr), "%H:%M", timeInfo);

    // Read tank level and compute percentage
    int tankRaw = analogRead(tankLevelPin);
    int tankPercentage = map(tankRaw, 0, 1023, 0, 100);

    // Retrieve wind speed from cached weather data:
    DynamicJsonDocument jsonDoc(1024);
    DeserializationError error = deserializeJson(jsonDoc, cachedWeatherData);
    float currentWind = 0.0;
    if (!error) {
      currentWind = jsonDoc["wind"]["speed"].as<float>();
    }

    String line2 = String(timeStr);
    String line1 = "Ta:" + String(tankPercentage) + "% " + " Wind:" + String(currentWind, 2);

    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print(line1);
    lcd.setCursor(5, 0);
    lcd.print(line2);
  }
}

void updateLCDForZone(int zone) {
  static unsigned long lastUpdate = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastUpdate < 1000) {
    return;
  }
  lastUpdate = currentTime;
  unsigned long elapsed = (currentTime - valveStartTime[zone]) / 1000;
  unsigned long totalDuration = duration[zone] * 60;
  unsigned long remainingTime = (totalDuration > elapsed) ? totalDuration - elapsed : 0;
  String zoneText = "Zone " + String(zone + 1);
  String elapsedTimeText = String(elapsed / 60) + ":" + (elapsed % 60 < 10 ? "0" : "") + String(elapsed % 60);
  String displayText = zoneText + " - " + elapsedTimeText;
  lcd.clear();
  lcd.setCursor((16 - displayText.length()) / 2, 0);
  lcd.print(displayText);
  if (elapsed < totalDuration) {
    String remainingTimeText = String(remainingTime / 60) + "m Remaining.";
    lcd.setCursor((16 - remainingTimeText.length()) / 2, 1);
    lcd.print(remainingTimeText);
  } else {
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("Complete");
    delay(2000);
  }
}

void updateLCD() {
  for (int i = 0; i < 4; i++) {
    if (valveOn[i]) {
      updateLCDForZone(i);
      break;
    }
  }
}

void updateWeatherVariables(const String& jsonData) {
  DynamicJsonDocument jsonResponse(1024);
  DeserializationError error = deserializeJson(jsonResponse, jsonData);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  temperature = jsonResponse["main"]["temp"].as<float>();
  humidity = jsonResponse["main"]["humidity"].as<int>();
  condition = jsonResponse["weather"][0]["main"].as<String>();
  delay(1000);
}

void checkWateringSchedule(int zone) {
  loadSchedule();
  
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  int currentDay = timeinfo->tm_wday;
  int currentHour = timeinfo->tm_hour;
  int currentMin = timeinfo->tm_min;
  
  if (shallWater(zone, currentDay, currentHour, currentMin)) {
    if (!valveOn[zone]) {
      if (!weatherCheckRequired) {
        weatherCheckRequired = true;
      } else {
        if (checkForRain()) {
          Serial.println("It's raining. Valve will not be turned on.");
          // displayRainMessage() already called in checkForRain()
          return;
        }
        if (checkForWind()) {
          Serial.println("High wind speed. Valve will not be turned on.");
          return;
        }
      }
      turnOnValve(zone);
      valveOn[zone] = true;
    }
  } else {
    if (valveOn[zone] && hasDurationCompleted(zone)) {
      turnOffValve(zone);
      valveOn[zone] = false;
      weatherCheckRequired = false;
    }
  }
}

bool shallWater(int zone, int currentDay, int currentHour, int currentMin) {
  if (days[zone][currentDay]) {
    if (currentHour == startHour[zone] && currentMin == startMin[zone]) {
      return true;
    }
    if (enableStartTime2[zone]) {
      if (currentHour == startHour2[zone] && currentMin == startMin2[zone]) {
        return true;
      }
    }
  }
  return false;
}

bool isTankLevelLow() {
  int tankLevel = analogRead(tankLevelPin);
  Serial.print("Tank level: ");
  Serial.println(tankLevel);
  int threshold = 500;
  return tankLevel < threshold;
}

void turnOnValve(int zone) {
  lcd.clear();
  digitalWrite(valvePins[zone], HIGH);
  valveStartTime[zone] = millis();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Zone ");
  lcd.print(zone + 1);
  lcd.print(" On");
  delay(2000);
  lcd.clear();
  
  if (isTankLevelLow()) {
    digitalWrite(mainsSolenoidPin, HIGH);
    lcd.setCursor(3, 1);
    lcd.print("Source: Mains");
  } else {
    digitalWrite(tankSolenoidPin, HIGH);
    lcd.setCursor(4, 1);
    lcd.print("Source: Tank");
  }
  delay(2000);
  lcd.clear();
  updateLCDForZone(zone);
}

bool hasDurationCompleted(int zone) {
  unsigned long elapsed = (millis() - valveStartTime[zone]) / 1000;
  unsigned long totalDuration = duration[zone] * 60;
  return (elapsed >= totalDuration);
}

void turnOnValveManual(int zone) {
  digitalWrite(valvePins[zone], HIGH);
  valveStartTime[zone] = millis();
  lcd.backlight(); 
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("Valve ");
  lcd.print(zone + 1);
  lcd.print(" On");
  if (isTankLevelLow()) {
    digitalWrite(mainsSolenoidPin, HIGH);
    lcd.setCursor(0, 1);
    lcd.print("Source: Mains");
  } else {
    digitalWrite(tankSolenoidPin, HIGH);
    lcd.setCursor(0, 1);
    lcd.print("Source: Tank");
  }
  server.send(200, "text/plain", "Valve " + String(zone + 1) + " turned on");       
}

void turnOffValve(int zone) {
  digitalWrite(valvePins[zone], LOW);
  digitalWrite(mainsSolenoidPin, LOW);
  digitalWrite(tankSolenoidPin, LOW);
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("Valve ");
  lcd.print(zone + 1);
  lcd.print(" Off");
  valveOn[zone] = false;  
  delay(1000);
  lcd.clear();

  String weatherData = getWeatherData();
  DynamicJsonDocument jsonResponse(1024);
  deserializeJson(jsonResponse, weatherData);
  float temp = jsonResponse["main"]["temp"].as<float>();
  int hum = jsonResponse["main"]["humidity"].as<int>();
  String weatherCondition = jsonResponse["weather"][0]["main"].as<String>();
  int textLength = weatherCondition.substring(0, 10).length();
  int startPos = (textLength < 16) ? (16 - textLength) / 2 : 0;
  lcd.setCursor(startPos, 0);
  lcd.print(weatherCondition.substring(0, 10));
  lcd.setCursor(0, 1);
  lcd.print("Te:");
  lcd.print(temp);
  lcd.print("C Hu:");
  lcd.print(hum);
  lcd.print("%");
  delay(3000);
  lcd.noBacklight();
}

void turnOffValveManual(int zone) {
  digitalWrite(valvePins[zone], LOW);
  digitalWrite(mainsSolenoidPin, LOW);
  digitalWrite(tankSolenoidPin, LOW);
  valveOn[zone] = false;
  lcd.clear(); 
  lcd.setCursor(3, 0);
  lcd.print("Valve ");
  lcd.print(zone + 1);
  lcd.print(" Off");
  server.send(200, "text/plain", "Valve " + String(zone + 1) + " turned off");
  delay(1200);
  lcd.noBacklight();
  updateDetailsOnLCD();
}

void turnOffAllValves() {
  for (int i = 0; i < 4; i++) {
    if (valveOn[i]) {
      turnOffValve(i);
      lcd.noBacklight();
    }
  }
}

String getDayName(int dayIndex) {
  const char* daysOfWeek[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  if (dayIndex >= 0 && dayIndex < 7) {
    return daysOfWeek[dayIndex];
  }
  return "Invalid Day";
}

void handleRoot() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  char timeStr[9];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
  String currentTime = String(timeStr);
  
  loadSchedule();
  
  String weatherData = getWeatherData();
  DynamicJsonDocument jsonResponse(1024);
  deserializeJson(jsonResponse, weatherData);
  float temp = jsonResponse["main"]["temp"];
  float hum = jsonResponse["main"]["humidity"];
  float ws = jsonResponse["wind"]["speed"];
  String cond = jsonResponse["weather"][0]["main"];
  String cityName = jsonResponse["name"];

  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    int textLength = cond.substring(0, 10).length();
    int startPos = (textLength < 16) ? (16 - textLength) / 2 : 0;
    lcd.setCursor(startPos, 0);
    lcd.print(cond.substring(0, 10));
    lcd.setCursor(0, 1);
    lcd.print("Te:");
    lcd.print(temp);
    lcd.print("C Hu:");
    lcd.print(int(hum));
    lcd.print("%");
  }

  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Smart Irrigation System</title>";
  html += "<link href='https://fonts.googleapis.com/css?family=Roboto:400,500&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += "body { font-family: 'Roboto', sans-serif; background: linear-gradient(135deg, #e7f0f8, #ffffff); margin: 0; padding: 0; }";
  html += "header { background: linear-gradient(90deg, #0073e6, #00aaff); color: #fff; padding: 20px; text-align: center; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += ".container { max-width: 800px; margin: 20px auto; background: #fff; border-radius: 10px; box-shadow: 0 4px 12px rgba(0,0,0,0.15); padding: 20px; }";
  html += "h1 { margin: 0 0 10px; font-size: 2em; }";
  html += "p { margin: 10px 0; text-align: center; }";
  html += ".zone-container { background: #f9fbfd; padding: 15px; border-radius: 8px; margin-bottom: 20px; border: 1px solid #e0e0e0; }";
  html += ".days-container { display: flex; flex-wrap: wrap; justify-content: center; margin-bottom: 10px; }";
  html += ".checkbox-container { margin: 5px; }";
  html += ".time-duration-container { display: flex; flex-wrap: wrap; align-items: center; justify-content: center; margin-bottom: 15px; }";
  html += ".time-input, .duration-input { margin: 0 10px 10px; }";
  html += ".time-input label, .duration-input label { display: block; font-size: 0.9em; margin-bottom: 5px; }";
  html += "input[type='number'] { padding: 5px; border: 1px solid #ccc; border-radius: 4px; }";
  html += ".enable-input { margin: 10px; }";
  html += ".manual-control-container { text-align: center; margin-top: 10px; }";
  html += "button { padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; transition: background 0.3s ease; }";
  html += ".turn-on-btn { background: #4caf50; color: #fff; }";
  html += ".turn-on-btn:hover { background: #45a044; }";
  html += ".turn-off-btn { background: #0073e6; color: #fff; }";
  html += ".turn-off-btn:hover { background: #0061c2; }";
  html += "button[type='submit'] { background: #2196F3; color: #fff; }";
  html += "button[type='submit']:hover { background: #1976d2; }";
  html += "a { color: #0073e6; text-decoration: none; }";
  html += "a:hover { text-decoration: underline; }";
  html += "@media (max-width: 600px) { .container { width: 100%; padding: 10px; } .zone-container { margin-bottom: 15px; } .time-duration-container { flex-direction: column; align-items: flex-start; } .time-input, .duration-input { width: 100%; } }";
  html += "</style></head><body>";

  html += "<header><h1>Smart Irrigation System</h1></header>";
  html += "<div class='container'>";
  html += "<p id='clock'>Current Time: " + currentTime + "</p>";
  html += "<p>Location: " + cityName + "</p>";
  html += "<p id='weather-condition'>Condition: " + cond + "</p>";
  html += "<p id='temperature'>Temperature: " + String(temp) + " &#8451;</p>";
  html += "<p id='humidity'>Humidity: " + String(int(hum)) + " %</p>";
  html += "<p id='wind-speed'>Wind Speed: " + String(ws) + " m/s</p>";
  int tankRaw = analogRead(tankLevelPin);
  int tankPercentage = map(tankRaw, 0, 1023, 0, 100);
  String tankStatus = (tankRaw < 250) ? "Low - Using Main" : "Normal - Using Tank";
  html += "<p>Tank Level: <progress id='tankLevel' value='" +  String(tankPercentage) + "' max='100'></progress>" + String(tankPercentage) + "% (" + tankStatus + ")</p>";

  html += "<script>";
  html += "function updateClock() {";
  html += "  var now = new Date();";
  html += "  var hours = now.getHours().toString().padStart(2, '0');";
  html += "  var minutes = now.getMinutes().toString().padStart(2, '0');";
  html += "  var seconds = now.getSeconds().toString().padStart(2, '0');";
  html += "  document.getElementById('clock').textContent = 'Current Time: ' + hours + ':' + minutes + ':' + seconds;";
  html += "} setInterval(updateClock, 1000);";
  html += "function fetchWeatherData() {";
  html += "  fetch('/weather-data').then(response => response.json()).then(data => {";
  html += "    document.getElementById('weather-condition').textContent = 'Condition: ' + data.condition;";
  html += "    document.getElementById('temperature').textContent = 'Temperature: ' + data.temp + ' &#8451;';";
  html += "    document.getElementById('humidity').textContent = 'Humidity: ' + data.humidity + ' %';";
  html += "    document.getElementById('wind-speed').textContent = 'Wind Speed: ' + data.windSpeed + ' m/s';";
  html += "  }).catch(error => console.error('Error fetching weather data:', error));";
  html += "} setInterval(fetchWeatherData, 60000);";
  html += "</script>";
  
  html += "<form action='/submit' method='POST'>";
  for (int zone = 0; zone < numZones; zone++) {
    html += "<div class='zone-container'>";
    html += "<p><strong>Zone " + String(zone + 1) + ":</strong></p>";
    html += "<div class='days-container'>";
    for (int i = 0; i < 7; i++) {
      String dayLabel = getDayName(i);
      String checked = days[zone][i] ? "checked" : "";
      html += "<div class='checkbox-container'>";
      html += "<input type='checkbox' name='day" + String(zone) + "_" + String(i) + "' id='day" + String(zone) + "_" + String(i) + "' " + checked + ">";
      html += "<label for='day" + String(zone) + "_" + String(i) + "'>" + dayLabel + "</label>";
      html += "</div>";
    }
    html += "</div>";

    html += "<div class='time-duration-container'>";
    html += "<div class='time-input'>";
    html += "<label for='startHour" + String(zone) + "'>Start Time 1:</label>";
    html += "<input type='number' name='startHour" + String(zone) + "' id='startHour" + String(zone) + "' min='0' max='23' value='" + String(startHour[zone]) + "' required>";
    html += "<input type='number' name='startMin" + String(zone) + "' id='startMin" + String(zone) + "' min='0' max='59' value='" + String(startMin[zone]) + "' required>";
    html += "</div>";
    html += "<div class='duration-input'>";
    html += "<label for='duration" + String(zone) + "'>Duration (min):</label>";
    html += "<input type='number' name='duration" + String(zone) + "' id='duration" + String(zone) + "' min='0' value='" + String(duration[zone]) + "' required>";
    html += "</div>";
    html += "</div>";

    html += "<div class='time-duration-container'>";
    html += "<div class='time-input'>";
    html += "<label for='startHour2" + String(zone) + "'>Start Time 2:</label>";
    html += "<input type='number' name='startHour2" + String(zone) + "' id='startHour2" + String(zone) + "' min='0' max='23' value='" + String(startHour2[zone]) + "' required>";
    html += "<input type='number' name='startMin2" + String(zone) + "' id='startMin2" + String(zone) + "' min='0' max='59' value='" + String(startMin2[zone]) + "' required>";
    html += "</div>";
    html += "<div class='enable-input'>";
    html += "<input type='checkbox' name='enableStartTime2" + String(zone) + "' id='enableStartTime2" + String(zone) + "'" + (enableStartTime2[zone] ? " checked" : "") + ">";
    html += "<label for='enableStartTime2" + String(zone) + "'>Enable Start Time 2</label>";
    html += "</div>";
    html += "</div>";

    html += "<div class='manual-control-container'>";
    html += "<button type='button' class='turn-on-btn' data-zone='" + String(zone) + "'>Turn On</button>";
    html += "<button type='button' class='turn-off-btn' data-zone='" + String(zone) + "' disabled>Turn Off</button>";
    html += "</div>";

    html += "</div>";
  }
  
  html += "<button type='submit'>Update Schedule</button>";
  html += "</form>";
  html += "<p style='text-align: center;'>Click <a href='/setup'>HERE</a> to enter API key, City, Time Zone offset, and Wind Settings.</p>";
  html += "<p style='text-align: center;'><a href='https://openweathermap.org/city/" + city + "' target='_blank'>View Weather Details on OpenWeatherMap</a></p>";

    
  html += "<script>";
  html += "document.addEventListener('DOMContentLoaded', function() {";
  html += "  var turnOnButtons = document.querySelectorAll('.turn-on-btn');";
  html += "  var turnOffButtons = document.querySelectorAll('.turn-off-btn');";
  html += "  turnOnButtons.forEach(function(button) {";
  html += "    button.addEventListener('click', function() {";
  html += "      var zone = this.getAttribute('data-zone');";
  html += "      fetch('/valve/on' + zone, { method: 'POST' })";
  html += "        .then(response => response.text())";
  html += "        .then(data => {";
  html += "          console.log(data);";
  html += "          this.textContent = 'Turned On';";
  html += "          var turnOffButton = document.querySelector('.turn-off-btn[data-zone=\"' + zone + '\"]');";
  html += "          turnOffButton.textContent = 'Turn Off';";
  html += "          turnOffButton.disabled = false;";
  html += "        })";
  html += "        .catch(error => console.error('Error:', error));";
  html += "    });";
  html += "  });";
  html += "  turnOffButtons.forEach(function(button) {";
  html += "    button.addEventListener('click', function() {";
  html += "      var zone = this.getAttribute('data-zone');";
  html += "      fetch('/valve/off' + zone, { method: 'POST' })";
  html += "        .then(response => response.text())";
  html += "        .then(data => {";
  html += "          console.log(data);";
  html += "          this.textContent = 'Turned Off';";
  html += "          var turnOnButton = document.querySelector('.turn-on-btn[data-zone=\"' + zone + '\"]');";
  html += "          turnOnButton.textContent = 'Turn On';";
  html += "          this.disabled = true;";
  html += "        })";
  html += "        .catch(error => console.error('Error:', error));";
  html += "    });";
  html += "  });";
  html += "});";
  html += "</script>";
  
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleSubmit() {
  for (int zone = 0; zone < numZones; zone++) {
    for (int i = 0; i < 7; i++) {
      String dayArg = "day" + String(zone) + "_" + String(i);
      if (server.hasArg(dayArg)) {
        prevDays[zone][i] = days[zone][i];
        days[zone][i] = true;
      } else {
        prevDays[zone][i] = days[zone][i];
        days[zone][i] = false;
      }
    }
    if (server.hasArg("startHour" + String(zone)) && server.hasArg("startMin" + String(zone))) {
      startHour[zone] = server.arg("startHour" + String(zone)).toInt();
      startMin[zone] = server.arg("startMin" + String(zone)).toInt();
    } else {
      Serial.println("Missing start time for Zone " + String(zone));
    }
    if (server.hasArg("startHour2" + String(zone)) && server.hasArg("startMin2" + String(zone))) {
      startHour2[zone] = server.arg("startHour2" + String(zone)).toInt();
      startMin2[zone] = server.arg("startMin2" + String(zone)).toInt();
    } else {
      Serial.println("Missing second start time for Zone " + String(zone));
    }
    if (server.hasArg("duration" + String(zone))) {
      duration[zone] = server.arg("duration" + String(zone)).toInt();
    } else {
      Serial.println("Missing duration for Zone " + String(zone));
    }
    enableStartTime2[zone] = (server.arg("enableStartTime2" + String(zone)) == "on");
  }

  if (server.hasArg("apiKey") || server.hasArg("city") || server.hasArg("dstOffset")) {
    String newApiKey = server.hasArg("apiKey") ? server.arg("apiKey") : "";
    String newCity = server.hasArg("city") ? server.arg("city") : "";
    int dstAdjustmentValue = server.hasArg("dstOffset") ? server.arg("dstOffset").toInt() : 0;
    Serial.println("Saving configuration...");
    saveConfig(newApiKey.c_str(), newCity.c_str(), dstAdjustmentValue);
  } else {
    Serial.println("No configuration updates provided.");
  }
  Serial.println("Saving schedule...");
  saveSchedule();
  updateCachedWeatherData();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
  Serial.println("Redirecting to root page.");
}

void handleSetupPage() {
  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Setup</title>";
  html += "<link href='https://fonts.googleapis.com/css?family=Roboto:400,500&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += "body { font-family: 'Roboto', sans-serif; background: #f7f9fc; margin: 0; padding: 0; display: flex; align-items: center; justify-content: center; height: 100vh; }";
  html += "form { background: #ffffff; padding: 30px; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); width: 100%; max-width: 400px; }";
  html += "h1 { text-align: center; color: #333333; margin-bottom: 20px; }";
  html += "label { display: block; margin-bottom: 5px; color: #555555; }";
  html += "input[type='text'], input[type='number'], select { width: 100%; padding: 10px; margin-bottom: 15px; border: 1px solid #cccccc; border-radius: 5px; font-size: 14px; }";
  html += "input[type='checkbox'] { margin-right: 10px; }";
  html += "input[type='submit'] { width: 100%; padding: 10px; background: #007BFF; border: none; border-radius: 5px; color: #ffffff; font-size: 16px; cursor: pointer; }";
  html += "input[type='submit']:hover { background: #0056b3; }";
  html += "</style>";
  html += "</head><body>";
  html += "<form action='/configure' method='POST'>";
  html += "<h1>Setup</h1>";
  html += "<label for='apiKey'>API Key:</label>";
  html += "<input type='text' id='apiKey' name='apiKey' value='" + apiKey + "'>";
  html += "<label for='city'>City Number:</label>";
  html += "<input type='text' id='city' name='city' value='" + city + "'>";
  html += "<label for='timezone'>City Timezone Offset (hours):</label>";
  html += "<input type='number' id='timezone' name='dstOffset' min='-12' max='14' step='0.50' value='" + String(dstAdjustment) + "'>";
  html += "<label for='dstEnabled'>Daylight Saving Time:</label>";
  html += "<select id='dstEnabled' name='dstEnabled'>";
  html += "<option value='yes'>Yes</option>";
  html += "<option value='no' selected>No</option>";
  html += "</select>";
  html += "<label for='windSpeedThreshold'>Wind Speed Threshold (m/s):</label>";
  html += "<input type='number' id='windSpeedThreshold' name='windSpeedThreshold' min='0' step='0.1' value='" + String(windSpeedThreshold) + "'>";
  html += String("<label for='windCancelEnabled'><input type='checkbox' id='windCancelEnabled' name='windCancelEnabled'") + (windCancelEnabled ? " checked" : "") + "> Enable Wind Delay</label>";
  html += "<label for='rainDelay'><input type='checkbox' id='rainDelay' name='rainDelay' " + String(rainDelayEnabled ? "checked" : "") + "> Enable Rain Delay</label>";
  html += "<input type='submit' value='Submit'>";
  html += "<p style='text-align: center;'><a href='https://openweathermap.org/city/" + city + "' target='_blank'>View Weather Details on OpenWeatherMap</a></p>";
  html += "</form>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleConfigure() {
  apiKey = server.arg("apiKey");
  city = server.arg("city");
  float baseTimezone = server.arg("dstOffset").toFloat();
  String dstChoice = server.arg("dstEnabled");
  float overallOffset = baseTimezone + ((dstChoice == "yes") ? 1.0 : 0.0);
  bool rainDelayOption = server.hasArg("rainDelay");
  
  // --- Process wind configuration ---
  if (server.hasArg("windSpeedThreshold")) {
    windSpeedThreshold = server.arg("windSpeedThreshold").toFloat();
  } else {
    windSpeedThreshold = 5.0;
  }
  windCancelEnabled = server.hasArg("windCancelEnabled");

  dstAdjustment = overallOffset;
  rainDelayEnabled = rainDelayOption;
  
  saveConfig(apiKey.c_str(), city.c_str(), overallOffset);
  
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void reconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting to WiFi...");
    WiFi.disconnect();
    delay(1000);
    if (!wifiManager.autoConnect("ESPIrrigationAP")) {
      Serial.println("Reconnection failed.");
    } else {
      Serial.println("Reconnected successfully.");
    }
  } else {
    Serial.println("WiFi already connected.");
  }
}

void loadSchedule() {
  File file = LittleFS.open("/schedule.txt", "r");
  if (!file) {
    Serial.println("Failed to open schedule file for reading");
    return;
  }
  for (int i = 0; i < numZones; i++) {
    String line = file.readStringUntil('\n');
    if (line.length() > 0) {
      int index = 0;
      startHour[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      startMin[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      startHour2[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      startMin2[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      duration[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      enableStartTime2[i] = (line.substring(index, line.indexOf(',', index)).toInt() == 1);
      index = line.indexOf(',', index) + 1;
      for (int j = 0; j < 7; j++) {
        char delim = (j < 6) ? ',' : '\n';
        String dayVal = line.substring(index, line.indexOf(delim, index));
        days[i][j] = (dayVal.toInt() == 1);
        index = line.indexOf(delim, index) + 1;
      }
    }
  }
  file.close();
}

void saveSchedule() {
  File file = LittleFS.open("/schedule.txt", "w");
  if (!file) {
    Serial.println("Failed to open schedule file for writing");
    return;
  }
  for (int i = 0; i < numZones; i++) {
    file.print(startHour[i]);
    file.print(',');
    file.print(startMin[i]);
    file.print(',');
    file.print(startHour2[i]);
    file.print(',');
    file.print(startMin2[i]);
    file.print(',');
    file.print(duration[i]);
    file.print(',');
    file.print(enableStartTime2[i] ? "1" : "0");
    file.print(',');
    for (int j = 0; j < 7; j++) {
      file.print(days[i][j] ? "1" : "0");
      if (j < 6)
        file.print(',');
    }
    file.print('\n');
  }
  file.close();
  
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Schedule Updated");
  lcd.setCursor(6, 1);
  lcd.print(":-)");
  delay(3000);
  lcd.noBacklight();
}

void saveConfig(const char* apiKey, const char* city, float dstOffsetHours) {
  File configFile = LittleFS.open("/config.txt", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  configFile.println(apiKey);
  configFile.println(city);
  configFile.println(dstAdjustment, 2);
  configFile.println(rainDelayEnabled ? "1" : "0");
  // --- Save wind configuration ---
  configFile.println(windSpeedThreshold, 1);
  configFile.println(windCancelEnabled ? "1" : "0");
  configFile.close();

  lcd.clear();
  lcd.backlight();
  lcd.setCursor(5, 0);
  lcd.print("SAVED :)");
  delay(1000);
  lcd.noBacklight();

  Serial.println("Configuration saved");
  dstAdjustment = dstOffsetHours;
}

void loadConfig() {
  File configFile = LittleFS.open("/config.txt", "r");
  if (!configFile) {
    Serial.println("Failed to open config file for reading");
    return;
  }
  apiKey = configFile.readStringUntil('\n');
  apiKey.trim();
  city = configFile.readStringUntil('\n');
  city.trim();
  dstAdjustment = configFile.readStringUntil('\n').toFloat();
  String rainDelayLine = configFile.readStringUntil('\n');
  if (rainDelayLine.length() > 0) {
    rainDelayEnabled = (rainDelayLine.toInt() == 1);
  } else {
    rainDelayEnabled = true;
  }
  // --- Load wind configuration if available ---
  String windSpeedLine = configFile.readStringUntil('\n');
  if (windSpeedLine.length() > 0) {
    windSpeedThreshold = windSpeedLine.toFloat();
  } else {
    windSpeedThreshold = 5.0;
  }
  String windCancelLine = configFile.readStringUntil('\n');
  if (windCancelLine.length() > 0) {
    windCancelEnabled = (windCancelLine.toInt() == 1);
  } else {
    windCancelEnabled = false;
  }
  configFile.close();

  Serial.println("Configuration loaded");
  Serial.print("API Key: ");
  Serial.println(apiKey);
  Serial.print("City: ");
  Serial.println(city);
  Serial.print("DST Adjustment (hours): ");
  Serial.println(dstAdjustment);
  Serial.print("Rain Delay Enabled: ");
  Serial.println(rainDelayEnabled ? "Yes" : "No");
  Serial.print("Wind Speed Threshold: ");
  Serial.println(windSpeedThreshold);
  Serial.print("Wind Delayabled: ");
  Serial.println(windCancelEnabled ? "Yes" : "No");
}




