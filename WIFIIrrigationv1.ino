//ESP8266 WIFI Irrigation Controller(Wemos D1 R2)//Beau Kaczmarek
//To acsess WEB UI In the address bar of the web browser, type in Local IP address displayed on LCD.
//Get a free weather API @ https://openweathermap.org/

#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "WIFIUSERNAME";                  //Your network SSID/WIFI NAME here
const char* password = "WIFIPASSWORD";              //Your network Password here

// Weather API variables                                                                                                                      
const char* city = "Happy Valley, AU";              //Replace with your city name
const char* apiKey = "OPENWEATHERAPIKEY";           //Openwaethermap API Number, Get one for free at https://openweathermap.org/

// LCD Address
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Address in EEPROM to start saving data
int addr = 0;

// Web server setup
ESP8266WebServer server(80);

// NTP client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Solenoid valve setup
const int valvePin = 12; //relay for solenoid 
unsigned long valveStartTime = 0; //for duration timer
unsigned long elapsedTime = 0;//for duration timer

bool valveOn = false; //for valve on/off check    
bool raining = false; //for raining/not raining check if true activate solenoid

// Watering schedule variables
int startHour1 = 19; // default start Hour 1: is 7 PM
int startMin1 = 30; // default start Minutes 1: is 30 PM
int startHour2 = 7; // default start Hour 2: is 7 AM
int startMin2 = 30;// default start Minutes 2: is 30 AM
int duration = 10;  // default duration is 10 minutes.

// default watering days
bool days[7] = {false, true, false, true, false, true, false}; //default days
bool prevDays[7] = {false, false, false, false, false, false, false}; //days to saved not touchie 
boolean enableSchedule2 = false; //Start 2 checkbox

void setup() {
 
  // Set up NTP client
  timeClient.begin();
  timeClient.setTimeOffset((9.5 + 1) * 3600); //SET TIMEZONE HERE--Currently set at: UTC+9:30 during standard time and UTC+10:30 during daylight saving time.
  timeClient.update();
    
  // Start serial communication
  Serial.begin(9600);
  
    // Initialize EEPROM
  EEPROM.begin(512);
  
  // Load watering schedule from EEPROM
  loadSchedule();

  //check/initilize for webui 
  bool raining = checkRain();  
 
   // Initialize OTA
  ArduinoOTA.begin(); 
   
  // Set the hostname of the ESP8266 for OTA
  ArduinoOTA.setHostname("ESPIrrigation"); 
  
  // Connect to WiFi network
  WiFi.begin(ssid, password);
  Serial.print("Connecting to ");
  Serial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Initialize LCD screen
  lcd.init();
  lcd.backlight();
  
  // Print welcome message on LCD screen
  lcd.setCursor(0, 0);
  lcd.print("Smart Irrigation");
  lcd.setCursor(4, 1);
  lcd.print("System");
  delay(1000);
  lcd.clear();
  
  // Set up web server
  server.on("/", handleRoot);
  server.on("/submit", handleSubmit);
  server.begin();

  // turn off valve
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, LOW);
  }
  
void loop() {
  ArduinoOTA.handle(); // Handle OTA updates
  unsigned long elapsedTime = (millis() - valveStartTime) / 1000;
  checkWateringSchedule(elapsedTime); // Check if it's time to water
  updateLCD(); // Update the LCD screen
  server.handleClient(); // Handle incoming client requests
  if (valveOn && (elapsedTime / 60 == duration)) {
    turnOffValve();
  }
}

// Function to update the LCD screen with the current time and IP address
void updateLCD() { 
    if (valveOn) {
    lcd.setCursor(0, 0);
    lcd.print("Runtime:");
    unsigned long elapsedTime = (millis() - valveStartTime) / 1000;
    lcd.print(elapsedTime / 60); // Display the minutes
    lcd.print("m ");
    lcd.print(elapsedTime % 60); // Display the seconds
    lcd.print("s ");
    lcd.setCursor(0, 1);
    lcd.print("Duration: ");
    lcd.print(duration);
    lcd.print("m");
  } else if (!valveOn) {
    timeClient.update();
    lcd.setCursor(4, 0);
    lcd.print(timeClient.getFormattedTime());
    lcd.setCursor(1, 1);
    lcd.print("-");
    lcd.print(WiFi.localIP());
  }
}

void checkWateringSchedule(unsigned long elapsedTime) {
  loadSchedule();
  timeClient.update(); // Update the NTP client with the current time
  int currentDay = timeClient.getDay(); // Get the current day of the week (0 = Sunday, 1 = Monday, etc.)
  int currentHour = timeClient.getHours(); // Get the current hour (0-23)
  int currentMin = timeClient.getMinutes();// Get the current minute (0-59)
  
  // Check if it's time to water for the first start time
if (days[currentDay] && currentHour == startHour1 && currentMin == startMin1 && !valveOn) {
  // If it's the right day and time for the first watering schedule, and the valve is not already on, turn on the valve for the specified duration
  bool raining = checkRain();
  delay(1000);
  lcd.clear();
  turnOnValve(); // Turn on the valve by setting the valve pin to HIGH
} 

if (enableSchedule2 && days[currentDay] && currentHour == startHour2 && currentMin == startMin2 && !valveOn) {
  // If the second schedule is enabled, and it's the right day and time for the second watering schedule, and the valve is not already on, turn on the valve for the specified duration
  bool raining = checkRain();
  delay(1000);
  lcd.clear();
  turnOnValve();  
  // Turn on the valve by setting the valve pin to HIGH
}
  if (valveOn && (elapsedTime / 60 >= duration)) {
    // If the valve has been running for longer than the specified duration, turn it off
    turnOffValve(); // Turn off the valve by setting the valve pin to LOW
  }
}
  
// Function to turn on the valve
void turnOnValve() {
  valveStartTime = millis(); // Set the start time of the valve
  digitalWrite(valvePin, HIGH); // Set the valve pin to HIGH
  valveOn = true; // Set the valveOn flag to true
  updateLCD();
  }  

// Function to turn off the valve
void turnOffValve() {
  digitalWrite(valvePin, LOW);// Set the valve pin to LOW
  valveOn = false; // Set the valveOn flag to false
  delay(1000);
  lcd.clear();
  loop();
  }

void handleRoot() {
  // Get the current time from the NTP server
  timeClient.update();
  String temperature = getTemperature();
  String condition = getWeatherCondition();

  String html = "<!DOCTYPE html><html><head><title>WiFi Irrigation Timer</title>";
html += "<script>";
html += "function updateTime() {";
html += "  var now = new Date();";
html += "  var hours = now.getHours().toString().padStart(2, '0');";
html += "  var minutes = now.getMinutes().toString().padStart(2, '0');";
html += "  var seconds = now.getSeconds().toString().padStart(2, '0');";
html += "  var timeString = hours + ':' + minutes + ':' + seconds;";
html += "  document.getElementById('time').textContent = timeString;";
html += "}";
html += "setInterval(updateTime, 1000);";
html += "</script>";
html += "</head><body>";
html += "<h1>Watering Schedule</h1>";
html += "<p>Current time: <span id='time'>" + timeClient.getFormattedTime() + "</span></p>";
html += "<p>Current Weather: " + condition + " -- Temp: " + String(temperature) + "&deg;C</p>";
html += "Watering Days:<br>";
html += "<input type='checkbox' name='day_0' value='0'" + String(days[0] ? " checked" : "") + ">Sunday ";
html += "<input type='checkbox' name='day_1' value='1'" + String(days[1] ? " checked" : "") + ">Monday ";
html += "<input type='checkbox' name='day_2' value='2'" + String(days[2] ? " checked" : "") + ">Tuesday ";
html += "<input type='checkbox' name='day_3' value='3'" + String(days[3] ? " checked" : "") + ">Wednesday ";
html += "<input type='checkbox' name='day_4' value='4'" + String(days[4] ? " checked" : "") + ">Thursday ";
html += "<input type='checkbox' name='day_5' value='5'" + String(days[5] ? " checked" : "") + ">Friday ";
html += "<input type='checkbox' name='day_6' value='6'" + String(days[6] ? " checked" : "") + ">Saturday<br><br>";
html += "Start Time 1:  <input type='number' name='start_hours_1' value='" + String(startHour1) + "' min='0' max='23'>";
html += " <input type='number' name='start_mins_1' value='" + String(startMin1) + "' min='0' max='59'><br><br>";
html += "Start Time 2:  <input type='number' name='start_hours_2' value='" + String(startHour2) + "' min='0' max='23'>";
html += " <input type='number' name='start_mins_2' value='" + String(startMin2) + "' min='0' max='59'>";
html += "<label><input type='checkbox' name='enableSchedule2' value='1'" + String(enableSchedule2 ? " checked" : "") + ">Enable-Disable</label><br><br>";
html += "Duration:  <input type='number' name='duration' value='" + String(duration) + "' min='1' max='60'><br><br>";
html += "<div style='display: inline-block;'>";
html += "<div style='width: 15px; height: 15px; background-color: " + String(valveOn ? "blue" : "red") + ";'></div>";
html += "</div>";
html += "<div style='display: inline-block; margin-left: 20px;'>";
html += "<p>Solenoid control:</p>";
html += "<button name='valve' value='on' " + String(valveOn ? "disabled" : "") + ">Turn On</button> ";
html += "<button name='valve' value='off' " + String(!valveOn ? "disabled" : "") + ">Turn Off</button>";
html += "</div>";
html += "<br><br><input type='submit' value='Submit'><br>";
html += "</form></body></html>";
if (valveOn) {
  html += "<img src='https://cdn.dribbble.com/users/491554/screenshots/1437581/media/0f6e1900ba310477df34651045822f1d.gif'>";
} else {
  html += "<img src='https://i.gifer.com/7cIX.gif'>";
}
server.send(200, "text/html", html);
server.sendHeader("Location", "/", true);
server.send(302, "text/plain", "");
delay(1000);
}

void handleSubmit() {
  // Update watering schedule settings
  startHour1 = server.arg("start_hours_1").toInt();
  startMin1 = server.arg("start_mins_1").toInt();
  startHour2 = server.arg("start_hours_2").toInt();
  startMin2 = server.arg("start_mins_2").toInt();
  duration = server.arg("duration").toInt();
  enableSchedule2 = server.hasArg("enableSchedule2");

  for (int i = 0; i < 7; i++) {
    // save selected days
    prevDays[i] = days[i]; // save previous state
    days[i] = server.hasArg("day_" + String(i)) ? true : false; // update state based on checkbox
  }

  saveSchedule(); // Save values to EEPROM

  // Display updated watering schedule settings on LCD screen
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("S1-" + String(startHour1) + ":" + String(startMin1) + " S2-" + String(startHour2) + ":" + String(startMin2));
  lcd.setCursor(0, 1);
  lcd.print("Duration: " + String(duration) + " Mins");
  delay(2500);
  lcd.clear();

  // Control valve based on form input
  if (server.arg("valve") == "on") {
    turnOnValve();
  } else if (server.arg("valve") == "off") {
    digitalWrite(valvePin, LOW);
    turnOffValve();
  }

  // Send response to client
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void saveSchedule() {
  
   int address = 0;

  // Save the days array
  for (int i = 0; i < 7; i++) {
    EEPROM.write(address, days[i]);
    address++;
  }

  // Save the start times and duration
  EEPROM.write(address, startHour1);
  address++;
  EEPROM.write(address, startMin1);
  address++;
  EEPROM.write(address, startHour2);
  address++;
  EEPROM.write(address, startMin2);
  address++;
  EEPROM.write(address, duration);
  address++;

  // Save the enableSchedule2 flag
  EEPROM.write(address, enableSchedule2);
  address++;

  // Commit the changes to EEPROM
  EEPROM.commit();
}

void loadSchedule() {
  int address = 0;

  // Load the days array
  for (int i = 0; i < 7; i++) {
    days[i] = EEPROM.read(address);
    address++;
  }

  // Load the start times and duration
  startHour1 = EEPROM.read(address);
  address++;
  startMin1 = EEPROM.read(address);
  address++;
  startHour2 = EEPROM.read(address);
  address++;
  startMin2 = EEPROM.read(address);
  address++;
  duration = EEPROM.read(address);
  address++;

  // Load the enableSchedule2 flag
  enableSchedule2 = EEPROM.read(address);
  address++;
}

bool checkRain() {
  // Make a GET request to the OpenWeatherMap API to get the current weather data
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "&appid=" + String(apiKey);
  WiFiClient client;
  if (client.connect("api.openweathermap.org", 80)) {
    client.println("GET " + url + " HTTP/1.1");
    client.println("Host: api.openweathermap.org");
    client.println("Connection: close");
    client.println();
  } else {
    Serial.println("Could not connect to weather API");
    return false;
  }
// Read the response from the API and extract the weather condition
  String response = "";
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      break;
    }
  }
  while (client.available()) {
    response += (char)client.read();
  }
  client.stop();
  String condition = "";
  if (response.length() > 0) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    condition = doc["weather"][0]["main"].as<String>();
  }
    // Print the weather condition to the LCD screen
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("Weather ");
  lcd.setCursor(5, 1);
  lcd.print(condition);
  delay(4000);
  lcd.clear();
    // Check if it's raining and return a boolean value indicating whether it's raining or not
  if (condition == "Rain" || condition == "Drizzle" || condition == "Thunderstorm") {
    turnOffValve();
    lcd.clear();
    lcd.setCursor(4, 1);
    lcd.print("Raining");
    return true;
  } else {
    lcd.clear();
    lcd.setCursor(2, 1);
    lcd.print("Not raining");
    delay(1000);
    lcd.clear();
    return false;
  }
}  
  
String getWeatherCondition() {
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "&appid=" + String(apiKey);
  WiFiClient client;
  if (client.connect("api.openweathermap.org", 80)) {
    client.println("GET " + url + " HTTP/1.1");
    client.println("Host: api.openweathermap.org");
    client.println("Connection: close");
    client.println();
  } else {
    Serial.println("Could not connect to weather API");
    return "Error: Could not connect to weather API";
  }

  // Read the response from the API and extract the weather condition
  String response = "";
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      break;
    }
  }
  while (client.available()) {
    response += (char)client.read();
  }
  client.stop();
  String condition = "";
  if (response.length() > 0) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    condition = doc["weather"][0]["main"].as<String>();
  }
  return condition; // return the weather condition
}

String getTemperature() {
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "&appid=" + String(apiKey);
  WiFiClient client;
  if (client.connect("api.openweathermap.org", 80)) {
    client.println("GET " + url + " HTTP/1.1");
    client.println("Host: api.openweathermap.org");
    client.println("Connection: close");
    client.println();
  } else {
    Serial.println("Could not connect to weather API");
    return "Error: Could not connect to weather API";
  }

  String response = "";
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      break;
    }
  }
  while (client.available()) {
    response += (char)client.read();
  }
  client.stop();

  if (response.length() > 0) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    float temperature = doc["main"]["temp"].as<float>();
    temperature -= 273.15;
    return String(temperature);
  } else {
    Serial.println("Could not connect to weather API");
    return "Error: Could not connect to weather API";
  }
}

//End
