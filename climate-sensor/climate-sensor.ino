#ifdef ENABLE_DEBUG
 #define DEBUG_ESP_PORT Serial
 #define NODEBUG_WEBSOCKETS
 #define NDEBUG
#endif

#include <Arduino.h>
#ifdef ESP8266 
 #include <ESP8266WiFi.h>
#endif 
#ifdef ESP32   
 #include <WiFi.h>
#endif

#include "SinricPro.h"
#include "SinricProTemperaturesensor.h"
#include "DHT.h" // https://github.com/markruys/arduino-DHT
#include "credentials.h" // Include credentials for WiFi and SinricPro

#define BAUD_RATE         115200              // Change baudrate to your need (used for serial monitor)
#define EVENT_WAIT_TIME   60000               // Send event every 60 seconds

#define DHT_PIN           2

DHT dht;                                      // DHT sensor

float temperature;                            // Actual temperature
float humidity;                               // Actual humidity
float lastTemperature;                        // Last known temperature (for compare)
float lastHumidity;                           // Last known humidity (for compare)
unsigned long lastEvent = (-EVENT_WAIT_TIME); // Last time event has been sent

void handleTemperaturesensor() {
  unsigned long actualMillis = millis();
  if (actualMillis - lastEvent < EVENT_WAIT_TIME) return; // Only check every EVENT_WAIT_TIME milliseconds

  temperature = dht.getTemperature();          // Get actual temperature in °C
  humidity = dht.getHumidity();                // Get actual humidity

  if (isnan(temperature) || isnan(humidity)) { // Reading failed...
    Serial.printf("DHT reading failed!\r\n");  // Print error message
    return;                                    // Try again next time
  } 

  if (temperature == lastTemperature || humidity == lastHumidity) return; // If no values changed do nothing...

  SinricProTemperaturesensor &mySensor = SinricPro[TEMP_SENSOR_ID];  // Get temperaturesensor device
  bool success = mySensor.sendTemperatureEvent(temperature, humidity); // Send event
  if (success) {  // If event was sent successfully, print temperature and humidity to serial
    Serial.printf("Temperature: %2.1f Celsius\tHumidity: %2.1f%%\r\n", temperature, humidity);
  } else {  // If sending event failed, print error message
    Serial.printf("Something went wrong...could not send Event to server!\r\n");
  }

  lastTemperature = temperature;  // Save actual temperature for next compare
  lastHumidity = humidity;        // Save actual humidity for next compare
  lastEvent = actualMillis;       // Save actual time for next compare
}

// Setup function for WiFi connection
void setupWiFi() {
  Serial.printf("\r\n[Wifi]: Connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf(".");
    delay(250);
  }
  IPAddress localIP = WiFi.localIP();
  Serial.printf("Connected!\r\n[WiFi]: IP-Address is %d.%d.%d.%d\r\n", localIP[0], localIP[1], localIP[2], localIP[3]);
}

// Setup function for SinricPro
void setupSinricPro() {
  // Add device to SinricPro
  SinricProTemperaturesensor &mySensor = SinricPro[TEMP_SENSOR_ID];

  // Setup SinricPro
  SinricPro.onConnected([](){ Serial.printf("Connected to SinricPro\r\n"); }); 
  SinricPro.onDisconnected([](){ Serial.printf("Disconnected from SinricPro\r\n"); });
     
  SinricPro.begin(APP_KEY, APP_SECRET);  
}

// Main setup function
void setup() {
  Serial.begin(BAUD_RATE); Serial.printf("\r\n\r\n");
  dht.setup(DHT_PIN);

  setupWiFi();
  setupSinricPro();
}

void loop() {
  SinricPro.handle();
  handleTemperaturesensor();
}
