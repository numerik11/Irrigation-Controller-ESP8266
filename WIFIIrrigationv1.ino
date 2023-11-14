#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <Wire.h>

const int valvePins[] = {D0, D3, D5, D6}; // Pins for 4 solenoids
const int ledPin = D4;
const int mainsSolenoidPin = D7; // MainsSolenoidPin solenoid
const int tankSolenoidPin = D8; // Water tank solenoid
const int tankLevelPin = A0; // Analog pin for tank level sensor

const char* ssid = "DESKTOPHOTSPOT";
const char* password = "speeduino11";

String newSsid;
String newPassword;

WiFiManager wifiManager;

String city = "Eden hills, AU";
String apiKey = "2fe1ead3deaabdc5ae4a918a9c676aa0";

LiquidCrystal_I2C lcd(0x27, 16, 2);
int addr = 0;

ESP8266WebServer server(80);
WiFiUDP ntpUDP;

NTPClient timeClient(ntpUDP);

unsigned long valveStartTime[4] = {0}; // Start times for 4 zones
unsigned long elapsedTime[4] = {0};
bool valveOn[4] = {false};
bool raining = false;
bool weatherCheckRequired = false;
bool wifiConnected = false;

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
  for (int i = 0; i < 4; i++) {
    pinMode(valvePins[i], OUTPUT);
    digitalWrite(valvePins[i], LOW);
  }

    for (int i = 0; i < numZones; i++) {
    startHour[i] = 0;
    startMin[i] = 0;
    duration[i] = 10; // Default duration in minutes
    enableStartTime[i] = false; // Default to disabled
  }
  
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);

  timeClient.begin();
  timeClient.setTimeOffset(10.5 * 3600);
  timeClient.update();

  Serial.begin(9600);
  EEPROM.begin(512);

  loadSchedule(); // Load the schedule from EEPROM

  ArduinoOTA.begin();
  ArduinoOTA.setHostname("ESPIrrigation");

  unsigned long startMillis = millis();
  WiFi.begin(ssid, password);
  Serial.print("Connecting to ");
  Serial.print(ssid);

  while (WiFi.status() != WL_CONNECTED && millis() - startMillis < 30000) {
    delay(500);
    Serial.print(".");
    digitalWrite(ledPin, HIGH);
    delay(500);
    Serial.print(".");
    digitalWrite(ledPin, LOW);
  }

  if (WiFi.status() != WL_CONNECTED) {
    // If not connected after 30 seconds, start AP mode
    digitalWrite(ledPin, HIGH);
    startAPMode();
  } else {
    // Wi-Fi connected
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(ledPin, LOW);
    loadSchedule();
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Smart Irrigation");
    lcd.setCursor(4, 1);
    lcd.print("System");
    delay(1000);
    lcd.clear();


server.on("/", HTTP_GET, handleRoot);
server.on("/submit", HTTP_POST, handleSubmit);
server.on("/wifi", HTTP_GET, handleWifi);
server.on("/connect", HTTP_POST, handleConnect);

// Set up valve control routes
for (int i = 0; i < 4; i++) {
  server.on("/valve/on" + String(i), HTTP_POST, [i]() { turnOnValveMan(i); });
  server.on("/valve/off" + String(i), HTTP_POST, [i]() { turnOffValveMan(i); });
}
    server.begin();
  }
}

void loop() {
  ArduinoOTA.handle();

  for (int i = 0; i < 4; i++) {
    unsigned long elapsedTime = (millis() - valveStartTime[i]) / 1000;
    checkWateringSchedule(i, elapsedTime);
  }

  updateLCD();
  server.handleClient();

  // Check for rain only when submitting a watering request
  if (weatherCheckRequired) {
    if (weatherCheck()) {
      turnOffAllValves();
      displayRainMessage();
    }
    weatherCheckRequired = false; // Reset the flag
  }
}

void updateLCD() {
  for (int i = 0; i < 4; i++) {
    if (valveOn[i]) {
      lcd.setCursor(0, 0);
      lcd.print("Zone " + String(i + 1) + " - Runtime:");
      unsigned long elapsedTime = (millis() - valveStartTime[i]) / 1000;
      lcd.print(elapsedTime / 60);
      lcd.print("m ");
      lcd.print(elapsedTime % 60);
      lcd.print("s ");
      lcd.setCursor(0, 1);
      lcd.print("Duration: ");
      lcd.print(duration[i]);
      lcd.print("m ");
    }
  }
}

void checkWateringSchedule(int zone, unsigned long elapsedTime) {
  loadSchedule();
  timeClient.update();
  int currentDay = timeClient.getDay();
  int currentHour = timeClient.getHours();
  int currentMin = timeClient.getMinutes();

  if (shouldWater(zone, currentDay, currentHour, currentMin, startHour[zone], startMin[zone])) {
    if (!valveOn[zone]) {
      if (weatherCheck() && !weatherCheckRequired) {
        Serial.println("It's raining. Valve will not be turned on.");
        weatherCheckRequired = true;
      } else {
        turnOnValve(zone);
        valveOn[zone] = true;
      }
    }
  } else if (enableStartTime[zone] && shouldWater(zone, currentDay, currentHour, currentMin, startHour[zone + 2], startMin[zone + 2])) {
    if (!valveOn[zone]) {
      if (weatherCheck() && !weatherCheckRequired) {
        Serial.println("It's raining. Valve will not be turned on.");
        weatherCheckRequired = true;
      } else {
        turnOnValve(zone);
        valveOn[zone] = true;
      }
    }
  }

  if (valveOn[zone] && hasValveReachedDuration(zone, elapsedTime)) {
    turnOffValve(zone);
    valveOn[zone] = false;
    weatherCheckRequired = false;
  }
}

bool hasValveReachedDuration(int zone, unsigned long elapsedTime) {
  unsigned long totalDuration = valveStartTime[zone] + (duration[zone] * 60 * 1000);
  return valveOn[zone] && (millis() >= totalDuration);
}

bool shouldWater(int zone, int currentDay, int currentHour, int currentMin, int targetHour, int targetMin) {
  if (days[zone][currentDay]) {
    if (currentHour == targetHour && currentMin == targetMin) {
      return true;
    }
  }
  return false;
}

void displayRainMessage() {
  lcd.clear();
  lcd.setCursor(4, 1);
  lcd.print("Raining");
  delay(60000);
  lcd.clear();
  loop();
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

  if (!weatherCheck()) { // Check weather only when turning on the solenoid
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

// Function to turn on a specific valve manually
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
  loop();
}

// Function to turn off a specific valve manually
void turnOffValveMan(int zone) {
  digitalWrite(valvePins[zone], LOW);
  Serial.println("Valve " + String(zone + 1) + " Off");
  updateLCD();
  server.send(200, "text/plain", "Valve " + String(zone + 1) + " turned off");
}

// Function to turn off all valves
void turnOffAllValves() {
  for (int i = 0; i < numZones; ++i) {
    digitalWrite(valvePins[i], LOW);
  }
}

// Function to get the day name based on the day index
String getDayName(int dayIndex) {
  const char* daysOfWeek[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  if (dayIndex >= 0 && dayIndex < 7) {
    return daysOfWeek[dayIndex];
  }
  return "Invalid Day";
}

String makeApiRequest(String endpoint) {
  WiFiClient client;

  if (client.connect("api.openweathermap.org", 80)) {
    client.print("GET " + endpoint + "\r\n");
    client.print("Host: api.openweathermap.org\r\n");
    client.print("Connection: close\r\n\r\n");
    client.flush();

    String response = "";
    unsigned long timeout = millis() + 10000;  // Set a timeout of 10 seconds

    while (millis() < timeout) {
      if (client.available()) {
        char c = client.read();
        response += c;
        timeout = millis() + 10000;  // Reset the timeout on receiving data
      }
      delay(10);  // Small delay to prevent flooding the API
    }

    client.stop();

    if (response.length() > 0) {
      return response;
    } else {
      Serial.println("No response from the API");
    }
  } else {
    Serial.println("Failed to connect to OpenWeatherMap API");
  }

  return "N/A";
}

String extractData(String response, String key) {
  int keyIndex = response.indexOf("\"" + key + "\":") + key.length() + 3;
  int endIndex = min(response.indexOf(',', keyIndex), response.indexOf('}', keyIndex));
  return response.substring(keyIndex, endIndex);
}

String getWeatherData(String dataName) {
  unsigned long startTime = millis();
  String response;

  while (millis() - startTime < 10000) { // Retry for up to 10 seconds
    delay(1000); // Optional delay to prevent flooding the API with requests
    String endpoint = "/data/2.5/weather?q=" + city + "&appid=" + apiKey + "&units=metric";
    response = makeApiRequest(endpoint);

    if (response != "N/A") {
      if (dataName == "temperature") {
        return extractData(response, "temp");
      } else if (dataName == "humidity") {
        return extractData(response, "humidity");
      } else if (dataName == "windSpeed") {
        return extractData(response, "speed");
      } else if (dataName == "condition") {
        return extractData(response, "main");
      }
    }
  }

  return "N/A"; // Return error message
}

bool weatherCheck() {
  Serial.println("Checking Rain..");
  WiFiClient client;

  if (client.connect("api.openweathermap.org", 80)) {
    String request = "GET /data/2.5/weather?q=" + city + "&appid=" + apiKey + "&units=metric\r\n";
    request += "Host: api.openweathermap.org\r\n";
    request += "Connection: close\r\n\r\n";

    client.print(request);
    client.flush();

    DynamicJsonDocument jsonBuffer(1024); // Create a JSON buffer to store the API response
    DeserializationError error = deserializeJson(jsonBuffer, client);

    if (error) {
      Serial.println("Failed to parse JSON");
      client.stop();
      return false; // Unable to parse JSON, consider it as no rain
    }

    const char* weatherCondition = jsonBuffer["weather"][0]["main"];
    Serial.print("Weather Condition: ");
    Serial.println(weatherCondition);

    // Check if the weather condition indicates rain or drizzle
    bool isRain = (strcmp(weatherCondition, "Rain") == 0 || strcmp(weatherCondition, "Drizzle") == 0);

    client.stop();
    return isRain;
  } else {
    Serial.println("Failed to connect to OpenWeatherMap API");
    return false; // Connection to OpenWeatherMap failed
  }
}

void handleRoot() {
  timeClient.update();
  String currentTime = timeClient.getFormattedTime();
  String temperature = getWeatherData("temperature");
  String humidity = getWeatherData("humidity");
  String windSpeed = getWeatherData("windSpeed");
  String condition = getWeatherData("condition");
  // String tankLevelInfo = readTankLevel(); // Modify this line

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
// html += "<p>" + tankLevelPin + "</p>";
  html += "<p id='clock' style='text-align: center;'>Current Time: " + currentTime + "</p>";
  html += "<p style='text-align: center;'>Temperature: " + temperature + " &#8451;</p>";
  html += "<p style='text-align: center;'>Condition: " + condition + "</p>";
  html += "<p style='text-align: center;'>Humidity: " + humidity + " %</p>";
  html += "<p style='text-align: center;'>Wind Speed: " + windSpeed + " m/s</p>";

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

  html += "</div></body></html>";

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

  saveSchedule(); // Save the schedule to EEPROM
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleWifi() {
  // Initialize WiFiManager
  WiFiManager wifiManager;
  
  // Create a custom HTML form with WiFi Manager
  wifiManager.setConfigPortalTimeout(180); // Set configuration portal timeout to 3 minutes
  wifiManager.setMinimumSignalQuality(-1); // Set minimum signal quality for autoconnect
  
  if (!wifiManager.startConfigPortal("ESPIrrigationAP")) {
    // Failed to connect or configure, restart the ESP
    ESP.restart();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    // WiFi connected
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    loop();
  }
}
  
void startAPMode() {
  Serial.println("Failed to connect to Wi-Fi. Starting AP mode.");
  WiFiManager wifiManager;
  wifiManager.autoConnect("ESPIrrigationAP");
  // After configuring Wi-Fi, check the connection again
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    // Initialize your other components here...
  } else {
    Serial.println("Still not connected to Wi-Fi. Retrying...");
    digitalWrite(ledPin, LOW);
    delay(1000);
    digitalWrite(ledPin, HIGH);
    delay(4000); // Wait for 5 seconds before retrying
    startAPMode(); // Retry AP mode
  }
} 

void handleConnect() {
  newSsid = server.arg("ssid");
  newPassword = server.arg("password");

  // Create an instance of WiFiManager
  WiFiManager wifiManager;

  // Disconnect from any previous connections
  WiFi.disconnect();
  delay(1000);

  // Use the autoConnect method of WiFiManager to connect to the new network
  if(wifiManager.autoConnect(newSsid.c_str(), newPassword.c_str())) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    wifiConnected = true;
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    Serial.println("Failed to connect to new WiFi...");
  }
}

void saveSchedule() {
  // Store the schedule in EEPROM
  addr = 0;
  for (int i = 0; i < numZones; i++) {
    EEPROM.write(addr, startHour[i]);
    EEPROM.write(addr + 1, startMin[i]);
    EEPROM.write(addr + 2, duration[i]);
    EEPROM.write(addr + 3, enableStartTime[i]);
    addr += 4; // Move to the next zone's starting address
  }
  for (int i = 0; i < 7; i++) {
    EEPROM.write(addr, days[0][i] ? 1 : 0);
    addr++;
  }
  EEPROM.commit();
}

void loadSchedule() {
  // Load the schedule from EEPROM
  addr = 0;
  // Load variables for each zone
  for (int i = 0; i < numZones; i++) {
    startHour[i] = EEPROM.read(addr);
    startMin[i] = EEPROM.read(addr + 1);
    duration[i] = EEPROM.read(addr + 2);
    enableStartTime[i] = EEPROM.read(addr + 3);
    addr += 4; // Move to the next zone's starting address
  }

  // Load days array
  for (int i = 0; i < 7; i++) {
    days[0][i] = EEPROM.read(addr) == 1;
    addr++;
  }
    enableStartTime[0] = EEPROM.read(addr);
  addr++;
}



