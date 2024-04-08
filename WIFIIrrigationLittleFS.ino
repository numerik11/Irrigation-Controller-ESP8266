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

// Constants for pins
const int valvePins[4] = {D0, D3, D5, D6}; // Pins for 4 solenoids
const int ledPin = D4; // Onboard LED pin
const int mainsSolenoidPin = D7; // Mains solenoid pin
const int tankSolenoidPin = D8; // Water tank solenoid pin
const int tankLevelPin = A0; // Analog pin for tank level sensor

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

unsigned long valveStartTime[4] = {0}; // Start times for 4 zones
unsigned long elapsedTime[4] = {0};
bool valveOn[4] = {false};
bool raining = false;
bool weatherCheckRequired = false;
bool wifiConnected = false;
String condition; // Declare condition globally
float temperature; // Declare temperature globally
float humidity; // Declare humidity globally
float windSpeed; // Declare windSpeed globally
float rain; // Declare windSpeed globally

// Define the number of zones
const int numZones = 4; // Adjust as needed

// Declare arrays for each parameter

int startHour[numZones];
int startMin[numZones];
int duration[numZones];
bool enableStartTime[numZones];
bool days[4][7] =   {{false, true, false, true, false, true, false}, // Days for each zone
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
  lcd.setCursor(4, 1);
  lcd.print("System");
  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ESPIrrigation");
  lcd.setCursor(0, 1);
  lcd.print("IP: 192.168.4.1");
  
  // Begin LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  // Load schedule from LittleFS
  loadSchedule();
  loadConfig();

  wifiManager.autoConnect("ESPIrrigationAP");
  // Connected to WiFi
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("Signal strength (RSSI): ");
  Serial.println(WiFi.RSSI());
  // Display connection status and details on LCD
   
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(WiFi.SSID());
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(10000); 

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

  lcd.setCursor(5, 0); // Adjust the position as needed
  lcd.print(weatherCondition.substring(0, 10)); // Display a substring of the condition to fit within 16 chars

  lcd.setCursor(0, 1);
  lcd.print("T: ");
  lcd.print(temperature);
  lcd.print("C H: ");
  lcd.print(humidity); // Depending on the range of humidity, you may need to abbreviate or omit parts
  lcd.print("%");

  // Set up NTP client
  timeClient.begin();
  timeClient.setTimeOffset(10.5 * 3600); // Set timezone offset (adjust as needed)

  // Set up OTA
  ArduinoOTA.begin();
  ArduinoOTA.setHostname("ESPIrrigation");
  
  // Set up HTTP routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/setup", HTTP_GET, handleSetupPage);
  server.on("/configure", HTTP_POST, handleConfigure); 

  for (int i = 0; i < 4; i++) {
    server.on("/valve/on" + String(i), HTTP_POST, [i]() { turnOnValveMan(i); });
    server.on("/valve/off" + String(i), HTTP_POST, [i]() { turnOffValveMan(i); });
  }

  // Start the server
  server.begin();
}

void loop() {
  ArduinoOTA.handle();

  for (int i = 0; i < numZones; i++) {
    checkWateringSchedule(i, dstAdjustment.toInt());
  }

  updateLCD();
  server.handleClient();

  // Check for rain only when submitting a watering request
  if (weatherCheckRequired) {
    if (checkForRain()) {
      turnOffAllValves();
      displayRainMessage();
    }
    weatherCheckRequired = false; // Reset the flag
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

void displayRainMessage() {
  lcd.clear();
  lcd.setCursor(4, 1);
  lcd.print("Raining");
  delay(60000);
  lcd.clear();
}

void updateLCDForZone(int zone) {  
  lcd.setCursor(0, 0);
  lcd.print("Zone ");
  lcd.print(zone + 1); // Increment zone by 1 to match human-readable numbering
  lcd.print(" - ");
  
  unsigned long elapsedTime = (millis() - valveStartTime[zone]) / 1000;
  lcd.print("Run:");
  lcd.print(elapsedTime / 60); // Display minutes
  lcd.print("m ");
  lcd.print(elapsedTime % 60); // Display seconds

  // Calculate and print the remaining time if there's enough space
  lcd.setCursor(0, 1);
  lcd.print("Remaining: ");
  if (elapsedTime < duration[zone] * 60) {
    unsigned long remainingTime = duration[zone] * 60 - elapsedTime; // Calculate remaining time in seconds
    lcd.print(remainingTime / 60); // Display remaining minutes
    lcd.print("m ");
    lcd.print(remainingTime % 60); // Display remaining seconds
    lcd.print("s");
  } else {
    lcd.clear();
    lcd.print("Complete");
    delay(2000);
  }
}

void checkWateringSchedule(int zone, int dstAdjustment) {
  loadSchedule();
  timeClient.update();
  int currentDay = timeClient.getDay();
  int currentHour = timeClient.getHours();
  int currentMin = timeClient.getMinutes();

  int currentHourWithDST = currentHour + dstAdjustment;
  if (currentHourWithDST >= 24) {
    currentHourWithDST -= 24;
  }

  if (shouldWater(zone, currentDay, currentHourWithDST, currentMin)) {
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
    if (valveOn[zone] && hasValveReachedDuration(zone)) {
      turnOffValve(zone);
      valveOn[zone] = false;
      weatherCheckRequired = false;
    }
  }
}

bool hasValveReachedDuration(int zone) {
  unsigned long elapsedTime = (millis() - valveStartTime[zone]) / 1000;
  unsigned long totalDuration = duration[zone] * 60;
  return valveOn[zone] && (elapsedTime >= totalDuration);
}

bool shouldWater(int zone, int currentDay, int currentHour, int currentMin) {
  if (days[zone][currentDay]) {
    if (currentHour == startHour[zone] && currentMin == startMin[zone]) {
      return true;
    }
  }
  return false;
}

bool isTankLevelLow() {
  // Read the tank level using the analog pin
  int tankLevel = analogRead(tankLevelPin);

  // Adjust the threshold based on your sensor and tank configuration
  int threshold = 500;

  return tankLevel < threshold;
}

void turnOnValve(int zone) {
  delay(500);
  valveStartTime[zone] = millis();

  if (isTankLevelLow()) {
    // If tank level is low, use mainsSolenoidPin solenoid
    digitalWrite(mainsSolenoidPin, HIGH);
    Serial.println("Using mainsSolenoidPin solenoid (master)");
  } else {
    // If tank level is above low, use water tank solenoid
    digitalWrite(tankSolenoidPin, HIGH);
    Serial.println("Using water tank solenoid (slave)");
  }

  if (!checkForRain()) { // Check weather only when turning on the solenoid
    digitalWrite(valvePins[zone], HIGH);
    Serial.println("Valve " + String(zone + 1) + " On");
    Serial.print("Duration: ");
    Serial.print(duration[zone]);
    Serial.println(" Mins");
    updateLCD();
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    Serial.println("It's raining. Valve will not be turned on.");
    turnOffValve(zone); // Turn off the valve immediately if it's raining
  }
}

void turnOnValveMan(int zone) {
  digitalWrite(valvePins[zone], HIGH);

  // Print debug information
  Serial.print("Valve ");
  Serial.print(zone + 1);
  Serial.println(" On");

  Serial.print("Duration: ");
  Serial.print(duration[zone]);
  Serial.println(" Mins");

  updateLCD();
  server.send(200, "text/plain", "Valve " + String(zone + 1) + " turned on");
}

void turnOffValve(int zone) {
  digitalWrite(valvePins[zone], LOW);
  Serial.println("Valve " + String(zone + 1) + " Off");
  valveOn[zone] = false;
  delay(800);
  lcd.clear();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
   
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

  lcd.setCursor(5, 0); // Adjust the position as needed
  lcd.print(weatherCondition.substring(0, 10)); // Display a substring of the condition to fit within 16 chars

  lcd.setCursor(0, 1);
  lcd.print("T: ");
  lcd.print(temperature);
  lcd.print("C H: ");
  lcd.print(humidity); // Depending on the range of humidity, you may need to abbreviate or omit parts
  lcd.print("%");
  loop();
}

void turnOffValveMan(int zone) {
  digitalWrite(valvePins[zone], LOW);
  Serial.println("Valve " + String(zone + 1) + " Off");
  updateLCD();
  server.send(200, "text/plain", "Valve " + String(zone + 1) + " turned off");
}

void turnOffAllValves() {
  for (int i = 0; i < 4; i++) {
    if (valveOn[i]) {
      turnOffValve(i);
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
  loadConfig();
  HTTPClient http;
   // Create the URL for the OpenWeather API request
  String url = "http://api.openweathermap.org/data/2.5/weather?id=" + city + "&appid=" + apiKey + "&units=metric";
 
  // Send the GET request to the OpenWeather API
  http.begin(client, url); // Pass the WiFiClient object by reference
  int httpResponseCode = http.GET();

  String payload = "{}"; // Default empty JSON string

  if (httpResponseCode > 0) {
    // Read the JSON response
    payload = http.getString();
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
    Serial.println(payload);
  }

  http.end(); // Close the connection

  return payload;
}

void handleRoot() {
  // Extract temperature, humidity, wind speed, and condition from the response
  timeClient.update();
  String currentTime = timeClient.getFormattedTime();

 // Extract weather data
  String weatherData = getWeatherData();
  DynamicJsonDocument jsonResponse(1024); // Adjust the size as needed
  deserializeJson(jsonResponse, weatherData); // Convert String to char*

  // Extract temperature, humidity, wind speed, and condition from the JSON response
  float temperature = jsonResponse["main"]["temp"];
  float humidity = jsonResponse["main"]["humidity"];
  float windSpeed = jsonResponse["wind"]["speed"];
  String condition = jsonResponse["weather"][0]["main"];


 if (WiFi.status() == WL_CONNECTED) {
    // Display weather information on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Cond: " + condition); // Update to display condition
    lcd.setCursor(0, 1);
    lcd.print("Temp: " + String(temperature) + "C");  } else {
    // Display connection failure on LCD
    lcd.clear();
    lcd.print("WiFi failed");
  }

  String html = "<!DOCTYPE html><html><head><title>Smart Irrigation System</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background-color: #f2f2f2; margin: 0; }";
  html += "header { background-color: #4CAF50; color: white; padding: 15px; text-align: center; }";
  html += ".container { max-width: 600px; margin: 20px auto; padding: 20px; background-color: #fff; box-shadow: 0 0 10px rgba(0, 0, 0, 0.1); border-radius: 8px; }";
  html += "h1 { text-align: center; }";
  html += "p { margin-bottom: 10px; }";
  html += ".zone-container { margin-bottom: 20px; padding: 15px; background-color: #f9f9f9; border-radius: 5px; }";
  html += ".days-container { display: flex; margin-bottom: 10px; }";
  html += ".checkbox-container { margin-right: 10px; }";
  html += ".time-duration-container { display: flex; align-items: center; }";
  html += ".time-input, .duration-input { margin-right: 20px; }";
  html += ".manual-control-container { margin-top: 10px; }";
  html += ".turn-on-btn, .turn-off-btn { padding: 10px 15px; margin-right: 10px; background-color: #4caf50; color: #fff; border: none; border-radius: 3px; cursor: pointer; }";
  html += ".turn-off-btn { background-color: #f44336; }";
  html += "button[type='submit'] { background-color: #2196F3; color: white; padding: 10px 15px; border: none; border-radius: 3px; cursor: pointer; }";
  html += "</style></head>";
  html += "<body>";
  html += "<header><h1>Smart Irrigation System</h1></header>";
  html += "<div class='container'>";
  html += "<p id='clock' style='text-align: center;'>Current Time: " + currentTime + "</p>";
  html += "<p style='text-align: center;'>Condition: " + condition + "</p>";
  html += "<p style='text-align: center;'>Temperature: " + String(temperature) + " &#8451;</p>";
  html += "<p style='text-align: center;'>Humidity: " + String(humidity) + " %</p>";
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
      String checked = days[zone][i] ? "checked" : "";
      html += "<div class='checkbox-container'>";
      html += "<input type='checkbox' name='day" + String(zone) + "_" + String(i) + "' id='day" + String(zone) + "_" + String(i) + "' " + checked + ">";
      html += "<label for='day" + String(zone) + "_" + String(i) + "'>" + dayLabel + "</label>";
      html += "</div>";
    }
    html += "</div>";

 // Start times and duration
    html += "<div class='time-duration-container'>";
    html += "<div class='time-input'>";
    html += "<label for='startHour" + String(zone) + "'>Start Time:</label>";
    html += "<input type='number' name='startHour" + String(zone) + "' id='startHour" + String(zone) + "' min='0' max='23' value='" + String(startHour[zone]) + "' required style='width: 30px;'> :";
    html += "<input type='number' name='startMin" + String(zone) + "' id='startMin" + String(zone) + "' min='0' max='59' value='" + String(startMin[zone]) + "' required style='width: 30px;'>";
    html += "</div>";

    html += "<div class='duration-input'>";
    html += "<label for='duration" + String(zone) + "'>Duration (minutes):</label>";
    html += "<input type='number' name='duration" + String(zone) + "' id='duration" + String(zone) + "' min='0' value='" + String(duration[zone]) + "' required style='width: 30px;'>";
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
  for (int zone = 0; zone < numZones; zone++) {
    for (int i = 0; i < 7; i++) {
      prevDays[zone][i] = days[zone][i];
      days[zone][i] = server.arg("day" + String(zone) + "_" + String(i)) == "on";
    }

    startHour[zone] = server.arg("startHour" + String(zone)).toInt();
    startMin[zone] = server.arg("startMin" + String(zone)).toInt();
    duration[zone] = server.arg("duration" + String(zone)).toInt();
    enableStartTime[zone] = server.arg("enableStartTime" + String(zone)) == "on";
  }

  // Check if API key and city parameters are not empty
  if (server.hasArg("apiKey") && server.hasArg("city")) {
    String apiKey = server.arg("apiKey");
    String city = server.arg("city");
    String dstAdjustmentStr = String(dstAdjustment);
    
    // Save configuration to LittleFS
   saveConfig(apiKey.c_str(), city.c_str(), dstAdjustmentStr.toInt());
  }

  saveSchedule(); // Save the schedule to EEPROM
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleSetupPage() {
  // HTML content for setup page
  String html = "<!DOCTYPE html><html><head><title>Setup</title></head><body>";
  html += "<h1>Setup Page</h1>";
  html += "<form action='/configure' method='POST'>";
  html += "<label for='apiKey'>API Key:</label><br>";
  html += "<input type='text' id='apiKey' name='apiKey'><br>";
  html += "<label for='city'>City:</label><br>";
  html += "<input type='text' id='city' name='city'><br>"; 
  html += "<label for='dstOffset'>Time Zone Offset (hours):</label><br>";
  html += "<input type='number' id='dstOffset' name='dstOffset' min='-12' max='14'><br><br>";
  html += "<input type='submit' value='Submit'>";
  html += "</form>";
  html += "</body></html>";

  // Send HTML response
  server.send(200, "text/html", html);
}

void handleConnect() {
  wifiManager.setDebugOutput(false);
  wifiManager.autoConnect("ESPIrrigationAP");
  newSsid = WiFi.SSID();
  newPassword = WiFi.psk();
  Serial.print("Connecting to WiFi...");
  if (WiFi.begin(newSsid.c_str(), newPassword.c_str()) == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("Signal strength (RSSI): ");
    Serial.println(WiFi.RSSI());
    // Display connection status and details on LCD
    lcd.setCursor(0, 0);
    lcd.print("WiFi connected");
    lcd.setCursor(0, 1);
    lcd.print("IP:");
    lcd.print(WiFi.localIP());
    lcd.setCursor(0, 2);
    lcd.print("SSID:");
    lcd.print(WiFi.SSID());
    lcd.setCursor(0, 3);
    lcd.print("RSSI:");
    lcd.print(WiFi.RSSI());
  } else {
    Serial.println("Failed to connect to WiFi");
    // Display connection failure on LCD
    lcd.setCursor(0, 0);
    lcd.print("WiFi connection");
    lcd.setCursor(0, 1);
    lcd.print("failed");
  }
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
    file.print(duration[i]);
    file.print(',');
    file.print(enableStartTime[i]);
    file.print('\n');
  }

  for (int i = 0; i < 7; i++) {
    file.print(days[0][i] ? '1' : '0');
  }
  file.print('\n');

  file.close();
}

void loadSchedule() {
   File file = LittleFS.open("/schedule.txt", "r");
  if (!file) {
    Serial.println("Failed to open schedule file for reading");
    return;
  }

  for (int i = 0; i < numZones; i++) {
    startHour[i] = file.parseInt();
    file.read(); // Read ','
    startMin[i] = file.parseInt();
    file.read(); // Read ','
    duration[i] = file.parseInt();
    file.read(); // Read ','
    enableStartTime[i] = file.parseInt();
    file.read(); // Read '\n'
  }

  for (int i = 0; i < 7; i++) {
    days[0][i] = (file.read() == '1');
  }
  file.read(); // Read '\n'

  file.close();
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
