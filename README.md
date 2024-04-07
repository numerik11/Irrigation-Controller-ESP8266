 # Irrigation-Controller-ESP8266

1.Use a browser to Enter Your Wifi Details VIA IP:192.168.4.1 

2.Use a browser to go to the IP ADDRESS displayed when powering up.

3.At the bottom of the WEB User Interface Add these details:

CityID Number, eg. : "2078025"  
DST offset eg. : "-1" for daylight savings, 
API KEY eg : "345e1asdfdeaabdc5adgs918a9cfsuaa" which can be obtained from [](https://openweathermap.org/api)

The controller can be set up with a watering schedule, it can check the local weather forecast to determine if it should turn on or not. It also includes features i2c 16x2 LCD screen to display weather, runtime duration, rain staus and Local IP. 
Access WEB UI In the address bar of the web browser, type in Local IP address displayed on the LCD on start up. Values are stored in LittleFS and will remain if there is reset/power loss. 

Web UI

![Untitled](https://github.com/numerik11/Irrigation-Controller-ESP8266/assets/72150418/7566a5aa-3720-4856-a2ba-c94b09ff411b)

Wiring Diagram

![irrigation wiring](https://github.com/numerik11/Irrigation-Controller-ESP8266/assets/72150418/36ed754a-8750-4896-b58e-b252a472d5aa)

Default pin configs
--------------------
valvePins[4] = {D0, D3, D5, D6}; // Pins for 4 solenoids
ledPin = D4; // Onboard LED pin
mainsSolenoidPin = D7; // Mains solenoid pin
tankSolenoidPin = D8; // Water tank solenoid pin
tankLevelPin = A0; // Analog pin for tank level sensor (Untested)
