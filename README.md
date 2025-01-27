Irrigation Controller For ESP8266
-----------------------------------
The controller can be set up with a watering schedule, it can check the local weather forecast
to determine if it should turn on or not. It also includes features i2c 16x2 LCD screen to display weather, 
runtime duration, rain staus and Local IP. Access WEB UI In the address bar of the web browser,
type in Local IP address displayed on the LCD on start up.
Add a 1N4007 or diode between + and - terminals on the solenoids to prevent flyback voltage that will crash the esp8266.
Values are stored in LittleFS and will remain if there is reset or power loss. 

--Initial Setup--

1. Connect to ESPIrrigationAP in WiFi list with phone or computer WiFi. Enter Your Wifi router Username and Password Details.

2. Use a browser and enter IP ADDRESS in browser address bar displayed when powering up.

3. At the bottom of the WEB User Interface Add these details:

You will need an APIKEY that can be obtained for free at: (https://openweathermap.org/api)

Enter your area details at the bottom Of WEB User Iterface EG: 

CityID Number, eg. : "2078025"  

DST offset eg. : "-1" for daylight savings "0" for no daylight savings

API KEY eg : "345e1asdfdeaabdc5adgs918a9cfsuaa" 

APIKEY can be obtained for free at: (https://openweathermap.org/api)


Wiring Diagram
![wiring diag](https://github.com/numerik11/Irrigation-Controller-ESP8266/assets/72150418/e8b8f33b-ee8f-476f-b984-d1b4457ea578)

Web User Interface
![WebInterface](https://github.com/user-attachments/assets/858b17a5-7700-4d20-b997-f0eafc985ca5)

Wiring Diagram
![irrigation wiring](https://github.com/numerik11/Irrigation-Controller-ESP8266/assets/72150418/36ed754a-8750-4896-b58e-b252a472d5aa)

Default Pin Configs
--------------------
valvePins[4] = {D0, D3, D5, D6}; // Pins for 4 solenoids

mainsSolenoidPin = D7; // Mains solenoid pin (Optional)

tankSolenoidPin = D8; // Water tank solenoid pin (optional)

tankLevelPin = A0; // Analog pin for tank level sensor (Untested) (optional)
