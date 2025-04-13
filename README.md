Irrigation Controller For ESP8266
-----------------------------------
The controller can be set up with a watering schedule, days and two start times, it can check openweathermap.org for rain and windspeed
to determine if it should turn on or not. It also includes the very affordable i2c 16x2 LCD screen to display weather, 
runtime duration, tank level and Local IP. Access WEB UI In the address bar of the web browser,
type in Local IP address displayed on the LCD on start up.

Weather Integration: The controller checks local weather conditions via the OpenWeatherMap API
to decide whether to water or skip a cycle—helping to prevent overwatering during rain or high wind conditions.

LCD Display: A built-in I2C 16×2 LCD screen shows real-time weather information, runtime duration, rain status, and the local IP address.
Web User Interface: Easily access and manage the system through a web UI. Simply type the local IP address (displayed on the LCD at startup) into your web browser.
Data Persistence: All configuration settings (schedule, weather parameters, etc.) are stored in LittleFS, ensuring they remain intact even after a reset or power loss.

**Add a 1N4007 or diode between + and - terminals on the solenoids to prevent flyback voltage that will crash the esp8266. 

--Initial Setup--

Connect to WiFi:

1. From your phone or computer, connect to the WiFi network named "ESPIrrigationAP".
Enter your home WiFi router’s username and password as prompted.

2. Access the Web UI:

3. Open a web browser and type in the IP address displayed on the LCD during startup
you may need to restart to see it after youve entered your router details.
Enter Your Area Details:

4. At the bottom of the web interface in the setup section, input the following:

City ID Number from openweathermap.org: eg, "2078025".

Add your citys TimeZone eg, "9.5".

For daylight savings use "Yes", or "No" if no daylight savings is applicable it adds an hour to your timezone if yes.

API Key: Enter your OpenWeatherMap API key (get a free key from OpenWeatherMap.org).

5. Once these details are submitted reset again. 

The controller will use your area settings to obtain up-to-date weather information and adjust the watering schedule accordingly. 

6. Set up a watering schedule to automatically activate solenoid valves at predetermined times.

Materials
---------

ESP8266 D1 R2 OR similar microcontroller with Wi-Fi

Relay Module (6-channel)	To switch solenoids (5V logic compatible)

DC12V Solenoid Valves	4	For zone control 1 for Tank 1 for Main

DC12V 1.5 A Power Supply To drive solenoid valves and power controller

1N4007 (Flyback protection)

Optional

Analog Tank Level Sensor	1	0.5–4.5V output (e.g., 1.6 MPa pressure transducer)

----

Setup Page
![setup](https://github.com/user-attachments/assets/0625732d-a173-475c-963e-c30714bc7aaf)



Web User Interface
![main](https://github.com/user-attachments/assets/262912bb-05b4-40ee-a69f-adebeff35236)



Wiring Diagram 
![284591751-36ed754a-8750-4896-b58e-b252a472d5aa](https://github.com/user-attachments/assets/2e560554-f027-4a23-9937-c324d0a798c6)




