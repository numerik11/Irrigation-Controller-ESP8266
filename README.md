Irrigation Controller For ESP8266
-----------------------------------
The controller can be set up with a watering schedule, days and two start times, it can check openweathermap.org for rain and windspeep
to determine if it should turn on or not. It also includes the very affordable i2c 16x2 LCD screen to display weather, 
runtime duration, tank level and Local IP. Access WEB UI In the address bar of the web browser,
type in Local IP address displayed on the LCD on start up.

**Add a 1N4007 or diode between + and - terminals on the solenoids to prevent flyback voltage that will crash the esp8266. 

--Initial Setup--

Connect to WiFi:

1. From your phone or computer, connect to the WiFi network named "ESPIrrigationAP".
Enter your home WiFi router’s username and password as prompted.

2. Access the Web UI:

Open a web browser and type in the IP address displayed on the LCD during startup
you may need to restart to see it after youve entered your router details.
Enter Your Area Details:

At the bottom of the web interface in the setup section, input the following:
City ID Number from openweathermap.org: eg, "2078025".
Add your citys TimeZone eg, "9.5".
For daylight savings use "Yes", or "No" if no daylight savings is applicable it adds an hour to your timezone if yes.
API Key: Enter your OpenWeatherMap API key (get a free key from OpenWeatherMap.org).
Once these details are submitted reset again, 
The controller will use your area settings to obtain up-to-date weather information and adjust the watering schedule accordingly. 

Scheduled Watering: Set up a watering schedule to automatically activate solenoid valves at predetermined times.
Weather Integration: The controller checks local weather conditions via the OpenWeatherMap API to decide whether to water or skip a cycle—helping to prevent overwatering during rain or high wind conditions.
LCD Display: A built-in I2C 16×2 LCD screen shows real-time weather information, runtime duration, rain status, and the local IP address.
Web User Interface: Easily access and manage the system through a web UI. Simply type the local IP address (displayed on the LCD at startup) into your web browser.
Data Persistence: All configuration settings (schedule, weather parameters, etc.) are stored in LittleFS, ensuring they remain intact even after a reset or power loss.

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
