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

const int valvePins[4] = {D0, D3, D5, D6}; // Pins for 4 solenoids
const int ledPin = D4;                     // LED pin 
const int mainsSolenoidPin = D7;           // Mains solenoid pin (Optional)
const int tankSolenoidPin = D8;            // Tank solenoid pin (Optional)
const int tankLevelPin = A0;               // Analog pin for tank level sensor (Optional)

LiquidCrystal_I2C lcd(0x27, 16, 2);

WiFiManager wifiManager;
ESP8266WebServer server(80);
WiFiClient client;

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

String condition; 
float temperature; 
float humidity; 
float windSpeed; 
float rain; 
unsigned long previousMillis = 0;   
const long interval = 10000;    // 10-second LCD update interval (comment updated)
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

//
// Setup: Initialize hardware, WiFi, OTA, and time (via configTime)
//
void setup() {
  Serial.begin(115200);
  
  // Initialize valve pins
  for (int i = 0; i < 4; i++) {
    pinMode(valvePins[i], OUTPUT);
    digitalWrite(valvePins[i], LOW);
  }
  pinMode(ledPin, LOW);
  digitalWrite(ledPin, HIGH);

  // *** ADDED: Initialize solenoid source pins as outputs ***
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

  // --- Rewritten Section: Setup time using configTime ---
  long timeOffsetSec = (long)(dstAdjustment * 3600);  // Convert hours to seconds
  configTime(timeOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
  // Wait until time is set (roughly after a few seconds)
  time_t now = time(nullptr);
  while(now < 1000000000) { // arbitrary threshold
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

//
// Main loop: Handle OTA, HTTP requests, scheduling, and weather updates
//
void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  unsigned long currentMillis = millis();

 bool valveActive = false;
 for (int i = 0; i < 4; i++) {
  if (valveOn[i]) {              // Check if the valve is on for zone i
    updateLCDForZone(i);         // Update the LCD for the active zone
    valveActive = true;          // Mark that we found an active valve
    break;                       // Exit after updating the first active valve
  }
 }

 // If no valves are active, then check if it's time to update the weather info
 if (!valveActive) {
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis; // Reset the timer
    updateWeatherOnLCD();           // Update the weather info on the LCD
  }
 }

  // Check watering schedule for each zone
  for (int i = 0; i < numZones; i++) {
    checkWateringSchedule(i);
    updateLCD();
  }

  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    reconnectWiFi();
  }
}

//
// Check for rain by fetching weather data
//
bool checkForRain() {
  // Bypass rain delay if it is disabled in the setup
  if (!rainDelayEnabled) {
    return false;
  }
  
  updateCachedWeatherData();

  DynamicJsonDocument jsonResponse(1024);
  DeserializationError error = deserializeJson(jsonResponse, cachedWeatherData);
  if (error) {
    Serial.println("Failed to parse weather data.");
    return false;
  }

  String weatherCondition = jsonResponse["weather"][0]["main"].as<String>();
  Serial.print("Weather condition: ");
  Serial.println(weatherCondition);

  bool isRaining = (weatherCondition.equalsIgnoreCase("Rain") || 
                    weatherCondition.equalsIgnoreCase("Drizzle"));

  // If it's raining and rain delay is enabled, display the rain message.
  if (isRaining) {
    Serial.println("Rain delay active. Skipping watering.");
    displayRainMessage();
  }

  return isRaining;
}

//
// Update LCD display if any valve is active
//
void updateLCD() {
  for (int i = 0; i < 4; i++) {
    if (valveOn[i]) {
      updateLCDForZone(i);
      break;
    }
  }
}

//
// Update weather information on the LCD
//
void updateWeatherOnLCD() {
  // Ensure we have updated weather data based on our cache interval.
  updateCachedWeatherData();

  // Toggle display every 10 seconds.
  static unsigned long lastToggleTime = 0;
  static bool showWeatherScreen = true;
  unsigned long currentMillis = millis();
  if (currentMillis - lastToggleTime >= 10000) { // 10 seconds
    lastToggleTime = currentMillis;
    showWeatherScreen = !showWeatherScreen;
  }

  lcd.clear();
  
  if (showWeatherScreen) {
    // --- Weather Screen ---
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
      lcd.print(temp);
      lcd.print("C Hu:");
      lcd.print(hum);
      lcd.print("%");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Weather error");
    }
  } else {
    // --- Time and Tank Level Screen ---
    time_t nowTime = time(nullptr);
    struct tm * timeInfo = localtime(&nowTime);
    char timeStr[9]; // Format: HH:MM:SS
    strftime(timeStr, sizeof(timeStr), "%H:%M", timeInfo);
    
    int tankLevel = analogRead(tankLevelPin);
    
    lcd.setCursor(6, 0);
    lcd.print(timeStr);
    lcd.setCursor(1, 1);
    lcd.print("Tank Level: ");
    lcd.print(tankLevel);
    lcd.print("%");
  }
}

//
// Update global weather variables from JSON data
//
void updateWeatherVariables(const String& jsonData) {
  StaticJsonDocument<512> jsonResponse; // Lower memory footprint
  DeserializationError error = deserializeJson(jsonResponse, jsonData);
  
  if (error) {
    Serial.print("JSON Parse Failed: ");
    Serial.println(error.c_str());
    return;
  }

  temperature = jsonResponse["main"]["temp"].as<float>();
  humidity = jsonResponse["main"]["humidity"].as<int>();
  condition = jsonResponse["weather"][0]["main"].as<String>();
}


//
//Read .json from API address
//
String getWeatherData() {
  HTTPClient http;
  String payload = "{}";
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Skipping weather fetch.");
    return payload;
  }
  
  http.setTimeout(5000);
  String url = "http://api.openweathermap.org/data/2.5/weather?id=" + city + "&appid=" + apiKey + "&units=metric";
  http.begin(client, url);
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == 200) {
    payload = http.getString();
  } else {
    Serial.print("Weather API Error: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
  return payload;
}


//
// Rewritten Section: Display a "Raining" message on the LCD (Fixed version)
//
void displayRainMessage() { 
  for (int i = 0; i < 4; i++) {
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

//
//cache for weather data for scrolling screen update every hour
//

void updateCachedWeatherData() {
  unsigned long currentMillis = millis();
  if (cachedWeatherData == "" || (currentMillis - lastWeatherUpdateTime >= weatherUpdateInterval)) {
    cachedWeatherData = getWeatherData();
    lastWeatherUpdateTime = currentMillis;
  }
}

//
// Update LCD for a specific zone with elapsed/remaining time
//
void updateLCDForZone(int zone) {
}

//
// Check the watering schedule for a given zone using the current time
//
void checkWateringSchedule(int zone) {
  loadSchedule();  // Reload schedule in case of changes
  
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  int currentDay = timeinfo->tm_wday;   // 0 = Sunday, 1 = Monday, etc.
  int currentHour = timeinfo->tm_hour;
  int currentMin = timeinfo->tm_min;
  
  if (checkSchedule(zone, currentDay, currentHour, currentMin)) {
    if (!valveOn[zone]) {
      if (!weatherCheckRequired) {
        weatherCheckRequired = true;
      } else {
        if (checkForRain()) {
          Serial.println("It's raining. Valve will not be turned on.");
          displayRainMessage();
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

//
// Decide if watering should occur based on the schedule and days enabled
//
bool checkSchedule(int zone, int currentDay, int currentHour, int currentMin) {
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

//
// Check if the water tank level is low
//
bool getTankLevel() {
  int tankLevel = analogRead(tankLevelPin);
  Serial.print("Tank level: ");
  Serial.println(tankLevel);
  int threshold = 500;  // Adjust based on sensor calibration
  return tankLevel < threshold;
}

//
// Turn on a valve for a given zone (non-blocking version)
//
void turnOnValve(int zone) {
  lcd.clear();
  digitalWrite(valvePins[zone], HIGH);  // Turn on the valve
  valveStartTime[zone] = millis();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Zone ");
  lcd.print(zone + 1);
  lcd.print(" On");
  delay(2000);
  lcd.clear();
  
  // Determine water source based on tank level
  if (getTankLevel()) {
    digitalWrite(mainsSolenoidPin, HIGH);
    lcd.setCursor(0, 1);
    lcd.print("Source: Mains");
  } else {
    digitalWrite(tankSolenoidPin, HIGH);
    lcd.setCursor(0, 1);
    lcd.print("Source: Tank");
  }
  delay(2000);
  lcd.clear();
}

//
// Determine if the watering duration for a zone has completed
//
bool hasDurationCompleted(int zone) {
  unsigned long elapsed = (millis() - valveStartTime[zone]) / 1000;
  unsigned long totalDuration = duration[zone] * 60;
  return (elapsed >= totalDuration);
}

//
// Manual control: Turn on valve for a given zone via HTTP request
//
void turnOnValveManual(int zone) {
  digitalWrite(valvePins[zone], HIGH);
  valveStartTime[zone] = millis();
  lcd.backlight(); 
  lcd.clear();
  lcd.setCursor(3, 0);
  lcd.print("Valve ");
  lcd.print(zone + 1);
  lcd.print(" On");
  // *** Added water source selection similar to scheduled function ***
  if (getTankLevel()) {
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

//
// Turn off a valve for a given zone (scheduled version)
//
void turnOffValve(int zone) {
  digitalWrite(valvePins[zone], LOW);
  // *** Ensure water source solenoids are turned off ***
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

  // Update weather info on LCD after turning off the valve
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

//
// Manual control: Turn off valve for a given zone via HTTP request
//
void turnOffValveManual(int zone) {
  digitalWrite(valvePins[zone], LOW);
  // *** Ensure water source solenoids are turned off ***
  digitalWrite(mainsSolenoidPin, LOW);
  digitalWrite(tankSolenoidPin, LOW);
  lcd.clear(); 
  lcd.setCursor(3, 0);
  lcd.print("Valve ");
  lcd.print(zone + 1);
  lcd.print(" Off");
  server.send(200, "text/plain", "Valve " + String(zone + 1) + " turned off");
  delay(1200);
  lcd.noBacklight();
  updateWeatherOnLCD();
}

//
// Turn off all valves (used when rain is detected)
//
void turnOffAllValves() {
  for (int i = 0; i < 4; i++) {
    if (valveOn[i]) {
      turnOffValve(i);
      lcd.noBacklight();
    }
  }
}

//
// Return the day name for a given index (0 = Sunday)
//
String getDayName(int dayIndex) {
  const char* daysOfWeek[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  if (dayIndex >= 0 && dayIndex < 7) {
    return daysOfWeek[dayIndex];
  }
  return "Invalid Day";
}

//
// HTTP handler for the root page – displays weather info and schedule controls
//
void handleRoot() {
  // Get current time as a formatted string
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  char timeStr[9];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
  String currentTime = String(timeStr);
  
  // Load schedule settings
  loadSchedule();
  
  // Fetch weather data and parse JSON response
  String weatherData = getWeatherData();
  DynamicJsonDocument jsonResponse(1024);
  deserializeJson(jsonResponse, weatherData);
  float temp = jsonResponse["main"]["temp"];
  float hum = jsonResponse["main"]["humidity"];
  float ws = jsonResponse["wind"]["speed"];
  String cond = jsonResponse["weather"][0]["main"];
  String cityName = jsonResponse["name"];

  // Read tank level and compute percentage/status
  int tankRaw = analogRead(tankLevelPin);
  int tankPercentage = map(tankRaw, 0, 1023, 0, 100);
  String tankStatus = (tankRaw < 250) ? "Low" : "Normal";
  
  // Update LCD when WiFi is connected
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

  // Build modern HTML page with enhanced CSS styling
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
  html += "<p>Tank Level: <progress id='tankLevel' value='" + String(tankPercentage) + "' max='100'></progress>" + String(tankPercentage) + "% (" + tankStatus + ")</p>";

  // Existing JS for clock and weather updates
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
  html += "} setInterval(fetchWeatherData, 60000);"; // Fetch weather data every 60 seconds
  html += "</script>";
  
  // Form with schedule controls and manual valve buttons
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

    // First Start Time and Duration
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

    // Second Start Time and Enable checkbox
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

    // Manual control buttons for each zone
    html += "<div class='manual-control-container'>";
    html += "<button type='button' class='turn-on-btn' data-zone='" + String(zone) + "'>Turn On</button>";
    html += "<button type='button' class='turn-off-btn' data-zone='" + String(zone) + "' disabled>Turn Off</button>";
    html += "</div>";

    html += "</div>"; // Close zone container
  }
  
  html += "<button type='submit'>Update Schedule</button>";
  html += "</form>";
  html += "<p style='text-align: center;'>Click <a href='/setup'>HERE</a> to enter API key, City, and Time Zone offset.</p>";
  html += "<p style='text-align: center;'><a href='https://openweathermap.org/city/" + cityName + "' target='_blank'>View Weather Details on OpenWeatherMap</a></p>";

  // --- Added: JavaScript for Manual Control Button Actions ---
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



//
// Process schedule and configuration updates from the HTTP form submission
//
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

//
// Display the setup page for entering API key, city, and time zone offset
//
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
      html += "<input type='number' id='timezone' name='timezone' min='-12' max='14' step='0.50' value='" + String(dstAdjustment) + "'>";
      html += "<label for='dstEnabled'>Daylight Saving Time:</label>";
      html += "<select id='dstEnabled' name='dstEnabled'>";
      html += "<option value='yes'>Yes</option>";
      html += "<option value='no' selected>No</option>";
      html += "</select>";
      html += "<label for='rainDelay'><input type='checkbox' id='rainDelay' name='rainDelay' " + String(rainDelayEnabled ? "checked" : "") + "> Enable Rain Delay</label>";
      html += "<input type='submit' value='Submit'>";
      html += "<p style='text-align: center;'><a href='https://openweathermap.org/city/" +city+ "' target='_blank'>View Weather Details on OpenWeatherMap</a></p>";
      html += "</form>";
      html += "</body></html>";
  server.send(200, "text/html", html);
}

//
// Reconnect WiFi if disconnected
//
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

//
// Load watering schedule from LittleFS
//
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
      // Read first start time (hour and minute)
      startHour[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      startMin[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      
      // Read second start time (hour and minute)
      startHour2[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      startMin2[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      
      // Read duration (in minutes)
      duration[i] = line.substring(index, line.indexOf(',', index)).toInt();
      index = line.indexOf(',', index) + 1;
      
      // Read enable/disable flag for Start Time 2 (1 = enabled, 0 = disabled)
      enableStartTime2[i] = (line.substring(index, line.indexOf(',', index)).toInt() == 1);
      index = line.indexOf(',', index) + 1;
      
      // Read the 7 day flags
      for (int j = 0; j < 7; j++) {
        // For the last value, use '\n' as delimiter; otherwise, use ','
        char delim = (j < 6) ? ',' : '\n';
        String dayVal = line.substring(index, line.indexOf(delim, index));
        days[i][j] = (dayVal.toInt() == 1);
        index = line.indexOf(delim, index) + 1;
      }
    }
  }
  file.close();
}

//
// Save the watering schedule (including enableStartTime2) to LittleFS
//
void saveSchedule() {
  File file = LittleFS.open("/schedule.txt", "w");
  if (!file) {
    Serial.println("Failed to open schedule file for writing");
    return;
  }
  for (int i = 0; i < numZones; i++) {
    // Save first start time, second start time, duration, and enable flag for Start Time 2
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
    
    // Save each day's enabled flag (1 = enabled, 0 = disabled)
    for (int j = 0; j < 7; j++) {
      file.print(days[i][j] ? "1" : "0");
      if (j < 6)
        file.print(',');
    }
    file.print('\n');
  }
  file.close();
  
  // Provide LCD feedback that schedule was saved
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Schedule Updated");
  lcd.setCursor(6, 1);
  lcd.print(":-)");
  delay(3000);
  lcd.noBacklight();
}

//
// Save configuration (API key, city, and time offset) to LittleFS
//
void saveConfig(const char* apiKey, const char* city, float dstOffsetHours) {
  File configFile = LittleFS.open("/config.txt", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  configFile.println(apiKey);
  configFile.println(city);
  configFile.println(dstAdjustment, 2); // Save DST offset (in hours)
  configFile.println(rainDelayEnabled ? "1" : "0"); // Save rain delay setting (1 = enabled, 0 = disabled)
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

//
// Load configuration (API key, city, and time offset) to LittleFS
//
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
  
  // Attempt to read rain delay setting; default to true if not found
  String rainDelayLine = configFile.readStringUntil('\n');
  if (rainDelayLine.length() > 0) {
    rainDelayEnabled = (rainDelayLine.toInt() == 1);
  } else {
    rainDelayEnabled = true;
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
}

//
// Handle configuration form submission
//
void handleConfigure() {
  // Retrieve API key and city from the form
  apiKey = server.arg("apiKey");
  city = server.arg("city");

  // Retrieve the base timezone offset (in hours)
  float baseTimezone = server.arg("timezone").toFloat();

  // Retrieve the DST choice ("yes" or "no")
  String dstChoice = server.arg("dstEnabled");
  
  // Calculate the overall offset:
  // If DST is enabled, add 1 hour to the base timezone offset.
  float overallOffset = baseTimezone + ((dstChoice == "yes") ? 1.0 : 0.0);

  // Retrieve rain delay option: if the checkbox is present, rain delay is enabled.
  bool rainDelayOption = server.hasArg("rainDelay");
  
  // Update global settings
  dstAdjustment = overallOffset;
  rainDelayEnabled = rainDelayOption;
  
  // Save the configuration
  saveConfig(apiKey.c_str(), city.c_str(), overallOffset);
  
  // Redirect back to the root page
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

