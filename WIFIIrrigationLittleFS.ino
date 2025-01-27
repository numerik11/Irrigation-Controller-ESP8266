#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LittleFS.h>


const int valvePins[4] = {D0, D3, D5, D6}; // Pins for 4 solenoids
const int ledPin = D4; // LED pin 
const int mainsSolenoidPin = D7; // Mains solenoid pin - Optional
const int tankSolenoidPin = D8; // Water tank solenoid pin - Optional
const int tankLevelPin = A0; // Analog pin for tank level sensor - Optional

// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

LiquidCrystal_I2C lcd(0x27, 16, 2);

WiFiManager wifiManager;
ESP8266WebServer server(80);
WiFiClient client;
String newSsid;
String newPassword;
String apiKey;
String city;
WiFiUDP ntpUDP;
String dstAdjustment;

NTPClient timeClient(ntpUDP);

unsigned long valveStartTime[4] = {0}; 
unsigned long elapsedTime[4] = {0};

bool valveOn[4] = {false};
bool raining = false;
bool weatherCheckRequired = false;
bool wifiConnected = false;

String condition; 
float temperature; 
float humidity; 
float windSpeed; 
float rain; 
unsigned long previousMillis = 0;   
const long interval = 3600000;   

const int numZones = 4; 
int startHour[numZones];
int startMin[numZones];
int startHour2[numZones];
int startMin2[numZones];
int duration[numZones];
bool enableStartTime[numZones] = {true, true, true, true}; // Always enabled for all zones
bool enableStartTime2[numZones];
bool days[4][7] =     {{false, true, false, true, false, true, false},  
                       {false, true, false, true, false, true, false},
                       {false, true, false, true, false, true, false},
                       {false, true, false, true, false, true, false}};
bool prevDays[4][7] = {{false, false, false, false, false, false, false},
                       {false, false, false, false, false, false, false},
                       {false, false, false, false, false, false, false},
                       {false, false, false, false, false, false, false}};

void setup() {
  // Initialize pins
  for (int i = 0; i < 4; i++) {
    pinMode(valvePins[i], OUTPUT);
    digitalWrite(valvePins[i], LOW);
  }

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
  Serial.begin(115200);
  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Smart Irrigation");
  lcd.setCursor(5, 1);
  lcd.print("System");
   
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  loadConfig();
  loadSchedule();

  // Set a timeout for the WiFiManager
  wifiManager.setTimeout(180); // 180 seconds before timeout

  // Attempt to connect using WiFiManager
  if (!wifiManager.autoConnect("ESPIrrigationAP")) {
    Serial.println("Failed to connect to WiFi. Restarting...");
    
    // Display fallback info on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ESPIrrigation");
    lcd.setCursor(0, 1);
    lcd.print("IP: 192.168.4.1");
    
    // Restart ESP after timeout
    ESP.restart();
    }

  // At this point, WiFi is either connected or fallback has occurred
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ESPIrrigation");
    lcd.setCursor(0, 1);
    lcd.print("IP: 192.168.4.1");
  } else {
    Serial.println("WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Optional: Update LCD with connected IP
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP().toString());
 }
  // Fetch weather data
  String weatherData = getWeatherData();

  // Deserialize weather data
  DynamicJsonDocument jsonResponse(1024); // Adjust the size as needed
  deserializeJson(jsonResponse, weatherData);

  // Extract temperature, humidity, wind speed, and weather condition
  float temperature = jsonResponse["main"]["temp"].as<float>();
  int humidity = jsonResponse["main"]["humidity"].as<int>();
  String weatherCondition = jsonResponse["weather"][0]["main"].as<String>();

  // Display weather information on LCD
  lcd.clear();
  int textLength = weatherCondition.substring(0, 10).length();
  int startPos = (textLength < 16) ? (16 - textLength) / 2 : 0;
  lcd.setCursor(startPos, 0);
  lcd.print(weatherCondition.substring(0, 10));
  lcd.setCursor(0, 1);
  lcd.print("Te:");
  lcd.print(temperature);
  lcd.print("C Hu:");
  lcd.print(humidity); 
  lcd.print("%");
  delay(3000); 
  lcd.noBacklight();

  // Set up NTP client
  timeClient.begin();
  timeClient.setTimeOffset(10.5 * 3600); // timezone Set to 10.5 in Web UI

  // Set up OTA
  ArduinoOTA.begin();
  ArduinoOTA.setHostname("ESPIrrigation");
  
  // Set up HTTP routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/setup", HTTP_GET, handleSetupPage);
  server.on("/configure", HTTP_POST, handleConfigure); 

  for (int i = 0; i < 4; i++) {
    server.on("/valve/on" + String(i), HTTP_POST, [i]() { turnOnValveManualy(i); });
    server.on("/valve/off" + String(i), HTTP_POST, [i]() { turnOffValveManualy(i); });
  }

  // Start the server
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  unsigned long currentMillis = millis();

  // Check if it's time to update the weather data and LCD display
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis; // Update previousMillis to the current time
    updateWeatherOnLCD();    
  }

  // Handle watering schedules and other functionalities
  for (int i = 0; i < numZones; i++) {
    checkWateringSchedule(i, dstAdjustment.toInt());
    updateLCD();
  }

  // Handle rain check
  if (weatherCheckRequired && checkForRain()) {
    turnOffAllValves();
    displayRainMessage();
    weatherCheckRequired = false; // Reset the flag after handling
  }

  if (WiFi.status() != WL_CONNECTED) {
  reconnectWiFi(); // Try to reconnect if the connection is lost
  }
}

bool checkForRain() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi is not connected. Weather check failed.");
    return false;
  }

  String weatherData = getWeatherData();
  DynamicJsonDocument jsonResponse(1024);

  DeserializationError error = deserializeJson(jsonResponse, weatherData);
  if (error) {
    Serial.println("Failed to parse weather data.");
    return false;
  }

  String weatherCondition = jsonResponse["weather"][0]["main"].as<String>();

  Serial.print("Weather condition: ");
  Serial.println(weatherCondition);

  bool raining = (weatherCondition.equalsIgnoreCase("Rain") || weatherCondition.equalsIgnoreCase("Drizzle"));

  if (raining) {
    Serial.println("It's raining or drizzling.");
  } else {
    Serial.println("It's not raining or drizzling.");
  }

  return raining;
}

void updateLCD() {
  bool anyValveOn = false;

  for (int i = 0; i < 4; i++) {
    if (valveOn[i]) {
      anyValveOn = true;
      updateLCDForZone(i);
      break;
    }
  }
}

void updateWeatherOnLCD() {
    
  // Fetch weather data
  String weatherData = getWeatherData();

  // Deserialize weather data
  DynamicJsonDocument jsonResponse(1024); // Adjust the size as needed
  deserializeJson(jsonResponse, weatherData);

  // Extract temperature, humidity, wind speed, and weather condition
  float temperature = jsonResponse["main"]["temp"].as<float>();
  int humidity = jsonResponse["main"]["humidity"].as<int>();
  String weatherCondition = jsonResponse["weather"][0]["main"].as<String>();

  lcd.clear();
  int textLength = weatherCondition.substring(0, 10).length();
  int startPos = (textLength < 16) ? (16 - textLength) / 2 : 0;
  lcd.setCursor(startPos, 0);
  lcd.print(weatherCondition.substring(0, 10));
  lcd.setCursor(0, 1);
  lcd.print("Te:");
  lcd.print(temperature);
  lcd.print("C Hu:");
  lcd.print(humidity); 
  lcd.print("%");
}

void updateWeatherVariables(const String& jsonData) {
  DynamicJsonDocument jsonResponse(1024);
  DeserializationError error = deserializeJson(jsonResponse, jsonData);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Assuming the JSON structure is known and consistent
  temperature = jsonResponse["main"]["temp"].as<float>();
  humidity = jsonResponse["main"]["humidity"].as<int>();
  condition = jsonResponse["weather"][0]["main"].as<String>();
  delay(1000);
}

void displayRainMessage() { 
  lcd.clear();
  lcd.setCursor(4, 1);
  lcd.print("Raining");
  delay(15000);
  lcd.noBacklight();
}

void updateLCDForZone(int zone) {
  static unsigned long lastUpdate = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastUpdate < 1000) { // Update the display every 1000 milliseconds
    return;
  }
  lastUpdate = currentTime;

  unsigned long elapsedTime = (currentTime - valveStartTime[zone]) / 1000;
  unsigned long remainingTime = (duration[zone] * 60) - elapsedTime;

  String zoneText = "Zone " + String(zone + 1);
  String elapsedTimeText = String(elapsedTime / 60) + ":" + (elapsedTime % 60 < 10 ? "0" : "") + String(elapsedTime % 60);
  String displayText = zoneText + " - " + elapsedTimeText;

  lcd.clear();
  lcd.setCursor((16 - displayText.length()) / 2, 0); // Center the top row text
  lcd.print(displayText);

  if (elapsedTime < duration[zone] * 60) {
    String remainingTimeText = String(remainingTime / 60) + "m Remaining.";
    lcd.setCursor((16 - remainingTimeText.length()) / 2, 1); // Center the bottom row text
    lcd.print(remainingTimeText);
  } else {
    String completeText = "Complete";
    lcd.clear();
    lcd.setCursor(4, 0); // Center "Complete" in the middle of the screen
    lcd.print(completeText);
    delay(2000); // Delay to show "Complete", consider using non-blocking delay in the future
         }
}

void checkWateringSchedule(int zone, int dstAdjustment) {
  loadSchedule();
  timeClient.update();
  int currentDay = timeClient.getDay();
  int currentHour = timeClient.getHours();
  int currentMin = timeClient.getMinutes();
  int currentHourWithDST = timeClient.getHours() + dstAdjustment;
  if (currentHourWithDST >= 24) currentHourWithDST -= 24; 
  if (shallWater(zone, currentDay, currentHourWithDST, currentMin)) {
    if (!valveOn[zone]) {
      if (!weatherCheckRequired) {
        weatherCheckRequired = true;
      } else {
        bool raining = checkForRain();
        if (raining) {
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

bool shallWater(int zone, int currentDay, int currentHour, int currentMin) {
  // Check if watering is enabled for the current day
  if (days[zone][currentDay]) {
        // Check if Start Time 1 or Start Time 2 matches the current time
    if (enableStartTime[zone] || true) { // Always enable Start Time 1
      if (currentHour == startHour[zone] && currentMin == startMin[zone]) {
         return true;
      }
    }
    if (enableStartTime2[zone]) { // Check for Start Time 2
      if (currentHour == startHour2[zone] && currentMin == startMin2[zone]) {
          return true;
      }
    }
  }

  return false;
}

bool isTankLevelLow() {
  // Read the tank level using the analog pin
 int tankLevel = analogRead(tankLevelPin);
 Serial.print("Tank level: ");
 Serial.println(tankLevel);
  // Adjust the threshold based on your sensor and tank configuration
  int threshold = 500;

  return tankLevel < threshold;
}

void turnOnValve(int zone) {
    // Start the valve and record the start time
    lcd.clear();
    digitalWrite(valvePins[zone], HIGH);  // Turn on the valve
    valveStartTime[zone] = millis();  // Record the time when the valve was turned on
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Zone ");
    lcd.print(zone + 1);
    lcd.print(" On");
    delay(2000);
    lcd.clear();
    if (isTankLevelLow()) {
        digitalWrite(mainsSolenoidPin, HIGH);  // Turn on mains solenoid
        lcd.setCursor(0, 1);
        lcd.print("Source: Mains");
    } else {
        digitalWrite(tankSolenoidPin, HIGH);  // Turn on tank solenoid
        lcd.setCursor(0, 1);
        lcd.print("Source: Tank");
    }

    delay(2000);
    lcd.clear();

    // Keep the valve on until the duration is over or a manual interruption occurs
    while (true) {
        unsigned long elapsedTime = (millis() - valveStartTime[zone]) / 1005;  // Calculate elapsed time in seconds
        if (elapsedTime >= (duration[zone] * 60)) {  // Check if the duration is over
            }
        // Check for manual interruption (via HTTP or button press)
            server.handleClient(); // Process HTTP requests
        if (!valveOn[zone]) { // Valve manually turned off
            Serial.print("Zone ");
            Serial.print(zone + 1);
            Serial.println(" manually turned off.");
            return;  // Exit without turning off the valve again
            break;  // Exit the loop
        }

        // Update the LCD with remaining time
        unsigned long remainingTime = (duration[zone] * 60) - elapsedTime;
        lcd.setCursor(0, 1);
        lcd.print("Remaining: ");
        lcd.print(remainingTime / 60);
        lcd.print("m ");
        lcd.print(remainingTime % 60);
        lcd.print("s");
    }

    // Turn off the valve after the duration
    turnOffValve(zone);
}

bool hasDurationCompleted(int zone) {
  unsigned long elapsedTime = (millis() - valveStartTime[zone]) / 1000;
  unsigned long totalDuration = duration[zone] * 60;
  return valveOn[zone] && (elapsedTime >= totalDuration);
  lcd.noBacklight();
}

void turnOnValveManualy(int zone) {
  digitalWrite(valvePins[zone], HIGH);
  lcd.backlight(); 
  lcd.clear();
  // Print debug information
    lcd.setCursor(0, 0);
    lcd.print("Valve ");
    lcd.print(zone + 1);
    lcd.print("Manual On");
    server.send(200, "text/plain", "Valve " + String(zone + 1) + " turned on");       
}

void turnOffValve(int zone) {
  digitalWrite(valvePins[zone], LOW);
    lcd.setCursor(3, 0);
    lcd.print("Valve ");
    lcd.print(zone + 1);
    lcd.print(" Off");
  valveOn[zone] = false;  
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
  delay(1000);
  lcd.clear();

  // Fetch weather data
  String weatherData = getWeatherData();

  // Deserialize weather data
  DynamicJsonDocument jsonResponse(1024); // Adjust the size as needed
  deserializeJson(jsonResponse, weatherData);

  // Extract temperature, humidity, wind speed, and weather condition
  float temperature = jsonResponse["main"]["temp"].as<float>();
  int humidity = jsonResponse["main"]["humidity"].as<int>();
  String weatherCondition = jsonResponse["weather"][0]["main"].as<String>();

  // Determine the starting position for centering the text
  int textLength = weatherCondition.substring(0, 10).length();
  int startPos = (textLength < 16) ? (16 - textLength) / 2 : 0;

  // Set the cursor position
  lcd.setCursor(startPos, 0);

  // Print the weather condition
  lcd.print(weatherCondition.substring(0, 10));

  lcd.setCursor(0, 1);
  lcd.print("Te:");
  lcd.print(temperature);
  lcd.print("C Hu:");
  lcd.print(humidity); //
  lcd.print("%");
  delay(3000);
  lcd.noBacklight();
  loop();
}

void turnOffValveManualy(int zone) {
  digitalWrite(valvePins[zone], LOW);
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

String getWeatherData() {
    HTTPClient http;
    http.setTimeout(5000); // 5-second timeout
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

void handleRoot() {
  // Extract temperature, humidity, wind speed, and condition from the response
  timeClient.update();
  String currentTime = timeClient.getFormattedTime();
  loadSchedule();

  // Extract weather data
  String weatherData = getWeatherData();
  DynamicJsonDocument jsonResponse(1024); // Adjust the size as needed
  deserializeJson(jsonResponse, weatherData); // Convert String to char*

  // Extract temperature, humidity, wind speed, and condition from the JSON response
  float temperature = jsonResponse["main"]["temp"];
  float humidity = jsonResponse["main"]["humidity"];
  float windSpeed = jsonResponse["wind"]["speed"];
  String condition = jsonResponse["weather"][0]["main"];
  String city = jsonResponse["name"]; 

 if (WiFi.status() == WL_CONNECTED) {
    // Display weather information on LCD
    lcd.clear();
    // Determine the starting position for centering the text
    int textLength = condition.substring(0, 10).length();
    int startPos = (textLength < 16) ? (16 - textLength) / 2 : 0;

    // Set the cursor position
    lcd.setCursor(startPos, 0);

    // Print the weather condition
    lcd.print(condition.substring(0, 10));

    lcd.setCursor(0, 1);
    lcd.print("Te:");
    lcd.print(temperature);
    lcd.print("C Hu:");
    lcd.print(int(humidity)); 
    lcd.print("%");
  }

  String html = "<!DOCTYPE html><html><head><title>Smart Irrigation System</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background-color: #e7f0f8; margin: 0; }";
  html += "header { background-color: #0073e6; color: white; padding: 15px; text-align: center; }";
  html += ".container { max-width: 600px; margin: 20px auto; padding: 20px; background-color: #fff; box-shadow: 0 0 10px rgba(0, 0, 0, 0.1); border-radius: 8px; }";
  html += "h1 { text-align: center; }";
  html += "p { margin-bottom: 10px; }";
  html += ".zone-container { margin-bottom: 20px; padding: 15px; background-color: #f0f8ff; border-radius: 5px; }";
  html += ".days-container { display: flex; margin-bottom: 10px; }";
  html += ".checkbox-container { margin-right: 10px; }";
  html += ".time-duration-container { display: flex; align-items: center; }";
  html += ".time-input, .duration-input { margin-right: 20px; }";
  html += ".manual-control-container { margin-top: 10px; }";
  html += ".turn-on-btn, .turn-off-btn { padding: 10px 15px; margin-right: 10px; background-color: #4caf50; color: #fff; border: none; border-radius: 3px; cursor: pointer; }";
  html += ".turn-off-btn { background-color: #0073e6; }";
  html += "button[type='submit'] { background-color: #2196F3; color: white; padding: 10px 15px; border: none; border-radius: 3px; cursor: pointer; }";
  html += "</style></head>";
  html += "<body>";
  html += "<header><h1>Smart Irrigation System</h1></header>";
  html += "<div class='container'>";
  html += "<p id='clock' style='text-align: center;'>Current Time: " + currentTime + "</p>";
  html += "<p style='text-align: center;'>Location: " + city + "</p>"; // Include city information
  html += "<p style='text-align: center;'>Condition: " + condition + "</p>";
  html += "<p style='text-align: center;'>Temperature: " + String(temperature) + " &#8451;</p>";
  html += "<p style='text-align: center;'>Humidity: " + String(int(humidity)) + " %</p>";
  html += "<p style='text-align: center;'>Wind Speed: " + String(windSpeed) + " m/s</p>";

 // JavaScript to update the clock every second
  html += "<script>";
  html += "function updateClock() {";
  html += "var now = new Date();";
  html += "var hours = now.getHours().toString().padStart(2, '0');";
  html += "var minutes = now.getMinutes().toString().padStart(2, '0');";
  html += "var seconds = now.getSeconds().toString().padStart(2, '0');";
  html += "document.getElementById('clock').textContent = 'Current Time: ' + hours + ':' + minutes + ':' + seconds;";
  html += "}";
  // Update the clock every second
  html += "setInterval(updateClock, 1000);";
  html += "</script>";

  // Form for updating watering schedule
  html += "<form action='/submit' method='POST'>";

  // Display the days of the week, start times, duration, and manual control buttons for each zone
  for (int zone = 0; zone < numZones; zone++) {
  html += "<div class='zone-container'>";
  html += "<p><strong>Zone " + String(zone + 1) + ":</strong></p>";

  // Days checkboxes
  html += "<div class='days-container'>";
  for (int i = 0; i < 7; i++) {
    String dayLabel = getDayName(i);
    String checked = days[zone][i] ? "checked" : ""; // Use `days` array for checked state
    html += "<div class='checkbox-container'>";
    html += "<input type='checkbox' name='day" + String(zone) + "_" + String(i) + "' id='day" + String(zone) + "_" + String(i) + "' " + checked + ">";
    html += "<label for='day" + String(zone) + "_" + String(i) + "'>" + dayLabel + "</label>";
    html += "</div>";
    }
  html += "</div>";

  // Time and duration inputs
  html += "<div class='time-duration-container'>";

  // First start time input
  html += "<div class='time-input'>";
  html += "<label for='startHour" + String(zone) + "'>Start Time 1:</label>";
  html += "<input type='number' name='startHour" + String(zone) + "' id='startHour" + String(zone) + "' min='0' max='12' value='" + String(startHour[zone]) + "' required style='width: 60px;'> : ";
  html += "<input type='number' name='startMin" + String(zone) + "' id='startMin" + String(zone) + "' min='0' max='59' value='" + String(startMin[zone]) + "' required style='width: 60px;'>";
  html += "</div>";

  // Add spacing after Start Time 1
  html += "<div style='margin-bottom: 10px;'></div>";

  // Duration input
  html += "<div class='duration-input'>";
  html += "<label for='duration" + String(zone) + "'>Duration (minutes):</label>";
  html += "<input type='number' name='duration" + String(zone) + "' id='duration" + String(zone) + "' min='0' value='" + String(duration[zone]) + "' required style='width: 60px;'>";
  html += "</div>";
  html += "</div>";

  // Second start time input with enable/disable checkbox
  html += "<div class='time-input second-start-time'>";
  html += "<div>";
  html += "<label for='startHour2" + String(zone) + "'>Start Time 2:</label>";
  html += "<input type='number' name='startHour2" + String(zone) + "' id='startHour2" + String(zone) + "' min='0' max='23' value='" + String(startHour2[zone]) + "' required style='width: 60px;'> : ";
  html += "<input type='number' name='startMin2" + String(zone) + "' id='startMin2" + String(zone) + "' min='0' max='59' value='" + String(startMin2[zone]) + "' required style='width: 60px;'>";
  html += "</div>";

  // Add spacing before enable/disable button
  html += "<div style='margin-top: 10px;'></div>";

  html += "<div class='enable-input'>";
  html += "<input type='checkbox' name='enableStartTime2" + String(zone) + "' id='enableStartTime2" + String(zone) + "'" + (enableStartTime2[zone] ? " checked" : "") + ">";
  html += "<label for='enableStartTime2" + String(zone) + "'>Enable/Disable Start Time 2</label>";
  html += "</div>";
  html += "</div>";

  // Manual control buttons
  html += "<div class='manual-control-container'>";
  html += "<button type='button' class='turn-on-btn' data-zone='" + String(zone) + "'>Turn On</button>";
  html += "<button type='button' class='turn-off-btn' data-zone='" + String(zone) + "'>Turn Off</button>";
  html += "</div>";

  html += "</div>"; // End of zone-container
  }

  // JavaScript to handle manual control buttons
  html += "<script>";
  html += "document.addEventListener('DOMContentLoaded', function() {";
  html += "  var turnOnButtons = document.querySelectorAll('.turn-on-btn');";
  html += "  var turnOffButtons = document.querySelectorAll('.turn-off-btn');";

  html += "  turnOnButtons.forEach(function(button) {";
  html += "    button.addEventListener('click', function() {";
  html += "      var zone = this.getAttribute('data-zone');";
  html += "      sendValveControl('/valve/on' + zone);";
  html += "    });";
  html += "  });";

  html += "  turnOffButtons.forEach(function(button) {";
  html += "    button.addEventListener('click', function() {";
  html += "      var zone = this.getAttribute('data-zone');";
  html += "      sendValveControl('/valve/off' + zone);";
  html += "    });";
  html += "  });";

  // Function to send valve control requests to the server
  html += "  function sendValveControl(route) {";
  html += "    fetch(route, { method: 'POST' })";
  html += "      .then(response => response.text())";
  html += "      .then(data => console.log(data))";
  html += "      .catch(error => console.error('Error:', error));";
  html += "  }";

  html += "});";
  html += "</script>";

  // Submit button
  html += "<button type='submit'>Update Schedule</button></form>";

  // Add a link/button to access the setup page
  html += "<p>Click <a href='/setup'>here</a> to enter API key, City and Daylight savings offset .</p>";

  // Send the HTML response
  server.send(200, "text/html", html);
}

void handleSubmit() {
  // Process irrigation zones
  for (int zone = 0; zone < numZones; zone++) {
    for (int i = 0; i < 7; i++) {
        String dayArg = "day" + String(zone) + "_" + String(i);
        if (server.hasArg(dayArg)) {
            prevDays[zone][i] = days[zone][i];  // Save previous state
            days[zone][i] = true;              // Checkbox present, set `days` to true
        } else {
            prevDays[zone][i] = days[zone][i];  // Save previous state
            days[zone][i] = false;             // Checkbox absent, set `days` to false
        }
      }
      // Validate and update start times
    if (server.hasArg("startHour" + String(zone)) && server.hasArg("startMin" + String(zone))) {
      startHour[zone] = server.arg("startHour" + String(zone)).toInt();
      startMin[zone] = server.arg("startMin" + String(zone)).toInt();
    } else {
      Serial.println("Missing start time for Zone " + String(zone));
    }

    // Validate and update second start time
    if (server.hasArg("startHour2" + String(zone)) && server.hasArg("startMin2" + String(zone))) {
      startHour2[zone] = server.arg("startHour2" + String(zone)).toInt();
      startMin2[zone] = server.arg("startMin2" + String(zone)).toInt();
    } else {
      Serial.println("Missing second start time for Zone " + String(zone));
    }

    // Update duration
    if (server.hasArg("duration" + String(zone))) {
      duration[zone] = server.arg("duration" + String(zone)).toInt();
    } else {
      Serial.println("Missing duration for Zone " + String(zone));
    }

    // Update enable/disable for second start time
    enableStartTime2[zone] = server.arg("enableStartTime2" + String(zone)) == "on";
  }

  // Handle API key, city, and DST adjustment
  if (server.hasArg("apiKey") || server.hasArg("city") || server.hasArg("dstAdjustment")) {
    String apiKey = server.hasArg("apiKey") ? server.arg("apiKey") : "";
    String city = server.hasArg("city") ? server.arg("city") : "";
    int dstAdjustmentValue = server.hasArg("dstAdjustment") ? server.arg("dstAdjustment").toInt() : 0;

    // Save the configuration changes
    Serial.println("Saving configuration...");
    saveConfig(apiKey.c_str(), city.c_str(), dstAdjustmentValue);
  } else {
    Serial.println("No configuration updates provided.");
  }

  // Save the updated schedule to LittleFS
  Serial.println("Saving schedule...");
  saveSchedule();

  // Redirect the client to the root page
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");

  Serial.println("Redirecting to root page.");
}

void handleSetupPage() {
    // Load existing configuration values
    String existingApiKey = apiKey; // Assume apiKey is a global String variable
    String existingCity = city;     // Assume city is a global String variable
    float existingDstOffset = dstAdjustment.toFloat(); // Assume dstAdjustment is a String storing the offset as a float

    // HTML content for setup page
    String html = "<!DOCTYPE html><html><head><title>Setup</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f4f4f9; padding: 20px; }";
    html += "form { max-width: 300px; margin: auto; padding: 20px; background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
    html += "input[type='text'], input[type='number'], input[type='submit'] { width: 100%; padding: 8px; margin: 8px 0; box-sizing: border-box; }";
    html += "label { margin-top: 20px; }";
    html += "</style></head><body>";
    html += "<h1>Setup Page</h1>";
    html += "<form action='/configure' method='POST'>";
    html += "<label for='apiKey'>API Key:</label><br>";
    html += "<input type='text' id='apiKey' name='apiKey' value='" + existingApiKey + "'><br>";
    html += "<label for='city'>City Number:</label><br>";
    html += "<input type='text' id='city' name='city' value='" + existingCity + "'><br>";
    html += "<label for='dstOffset'>Time Zone Offset (hours):</label><br>";
    html += "<input type='number' id='dstOffset' name='dstOffset' min='-12' max='14' step='0.01' value='" + String(existingDstOffset, 2) + "'><br><br>";
    html += "<input type='submit' value='Submit'>";
    html += "</form>";
    html += "</body></html>";

    // Send HTML response
    server.send(200, "text/html", html);
}

void reconnectWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Reconnecting to WiFi...");
        WiFi.disconnect();
        delay(1000); // Small delay to stabilize
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

        // Parse days array from file
        for (int j = 0; j < 7; j++) {
            days[i][j] = line.substring(index, line.indexOf(',', index)).toInt();
            index = line.indexOf(',', index) + 1;
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

    // Save days as a comma-separated string
    for (int j = 0; j < 7; j++) {
        file.print(days[i][j] ? "1" : "0");
        if (j < 6) file.print(','); // Add commas between days, but not at the end
    }
    file.print('\n'); // End of the line for this zone
  }

  file.close();

  // LCD feedback
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(5, 0);
  lcd.print("SAVED!!");
  delay(3000);
  lcd.noBacklight();
}

void saveConfig(const char* apiKey, const char* city, int dstAdjustment) {
  File configFile = LittleFS.open("/config.txt", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }

  // Write configuration to file
  configFile.println(apiKey);
  configFile.println(city);
  configFile.println(dstAdjustment); // Save DST offset
  configFile.close();

    lcd.clear();
    lcd.backlight();
    lcd.setCursor(5, 0);
    lcd.print("SAVED :)");
    delay(1000);
    lcd.noBacklight();

  Serial.println("Configuration saved");
}

void loadConfig() {
    File configFile = LittleFS.open("/config.txt", "r");
    if (!configFile) {
        Serial.println("Failed to open config file for reading");
        return;
    }

    // Read configuration from file and update global variables
    apiKey = configFile.readStringUntil('\n');
    apiKey.trim(); // Remove leading and trailing whitespaces

    city = configFile.readStringUntil('\n');
    city.trim(); // Remove leading and trailing whitespaces

    dstAdjustment = configFile.readStringUntil('\n').toInt(); // Read and assign DST offset
    // Close file
    configFile.close();

    Serial.println("Configuration loaded");
    Serial.print("API Key: ");
    Serial.println(apiKey);
    Serial.print("City: ");
    Serial.println(city);
    Serial.print("DST Adjustment: ");
    Serial.println(dstAdjustment);
}

void handleConfigure() {
    // Handle configuration form submission
    apiKey = server.arg("apiKey");
    city = server.arg("city");
    dstAdjustment = server.arg("dstOffset").toInt(); // Read time zone offset

    int dstAdjustmentValue = dstAdjustment.toInt();
    saveConfig(apiKey.c_str(), city.c_str(), dstAdjustmentValue);
    // Redirect to the setup page
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}
