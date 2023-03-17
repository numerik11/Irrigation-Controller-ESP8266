//ESP8266 WIFI Irrigation Controller(Wemos D1 R2)//Beau Kaczmarek
//To acsess WEB UI In the address bar of the web browser, type in Local IP address displayed on LCD.
//Get a free weather API @ https://openweathermap.org/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

// WiFi credentials
const char* ssid = "WIFIUSERNAME";                  //Your network SSID/WIFI NAME here
const char* password = "WIFIPASSWORD";              //Your network Password here

// Weather API variables                                                                                                                      
const char* city = "Happy Valley, AU";              //Replace with your city name
const char* apiKey = "OPENWEATHERAPIKEY";           //Openwaethermap API Get one for free at https://openweathermap.org/
// LCD screen setup


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
