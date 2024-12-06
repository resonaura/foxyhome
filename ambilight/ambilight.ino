#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <functional>
#include <queue>
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
  #include <WiFi.h>
#endif

#include "SinricPro.h"
#include "SinricProLight.h"
#include "credentials.h" // Include credentials for WiFi and SinricPro

#define LED_PIN    D4      
#define NUM_LEDS   54     
#define BAUD_RATE  115200                

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

std::queue<std::function<void()>> eventQueue;

struct ColorRGB {
  byte r, g, b;
};

// Device state
struct {
  bool powerState = false;
  int brightnessPercentage = 100; // Brightness percentage from Sinric
  int lastBrightnessPercentage = 100; // For brightness animation
  ColorRGB color = {0, 0, 0}; // Initial color
  ColorRGB lastColor = {0, 0, 0}; // Save color before turning off
  int colorTemperature = 2700;
} device_state; 

int colorTemperatureArray[] = {2200, 2700, 4000, 5500, 7000};  
int max_color_temperatures = sizeof(colorTemperatureArray) / sizeof(colorTemperatureArray[0]); 
std::map<int, int> colorTemperatureIndex;

void setupColorTemperatureIndex() {
  for (int i = 0; i < max_color_temperatures; i++) {
    colorTemperatureIndex[colorTemperatureArray[i]] = i;
  }
}

ColorRGB kelvinToRGB(int kelvin) {
  const float temperature = kelvin / 100.0;
  float r, g, b;

  if (temperature <= 66) {
    r = 255;
    g = fmax(99.4708025861 * log(temperature) - 161.1195681661, 0);
    b = (temperature <= 19) ? 0 : fmax(138.5177312231 * log(temperature - 10) - 305.0447927307, 0);
  } else {
    r = fmax(329.698727446 * pow(temperature - 60, -0.1332047592), 0);
    g = fmax(288.1221695283 * pow(temperature - 60, -0.0755148492), 0);
    b = 255;
  }
  
  return { byte(r), byte(g), byte(b) };
}

void applyColorAndBrightness(ColorRGB color, int brightnessPercentage) {
  int adjustedR = (color.r * brightnessPercentage) / 100;
  int adjustedG = (color.g * brightnessPercentage) / 100;
  int adjustedB = (color.b * brightnessPercentage) / 100;

  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(adjustedR, adjustedG, adjustedB));
  }
  strip.show();
}

void smoothTransitionToColor(ColorRGB targetColor, int brightnessPercentage, int steps = 30, int delayTime = 10) {
  ColorRGB currentColor = device_state.color;
  for (int step = 0; step <= steps; step++) {
    int r = currentColor.r + ((targetColor.r - currentColor.r) * step) / steps;
    int g = currentColor.g + ((targetColor.g - currentColor.g) * step) / steps;
    int b = currentColor.b + ((targetColor.b - currentColor.b) * step) / steps;

    applyColorAndBrightness({ byte(r), byte(g), byte(b) }, brightnessPercentage);
    delay(delayTime); 
  }
  device_state.color = targetColor;
}

void smoothTransitionToBrightness(int startBrightness, int targetBrightness, int steps = 30, int delayTime = 10) {
  for (int step = 0; step <= steps; step++) {
    int brightness = startBrightness + ((targetBrightness - startBrightness) * step) / steps;
    applyColorAndBrightness(device_state.color, brightness);
    delay(delayTime);
  }
  device_state.brightnessPercentage = targetBrightness;
}

bool onPowerState(const String &deviceId, bool &state) {
  eventQueue.push([=]() {
    if (state) {
      smoothTransitionToColor(device_state.lastColor, device_state.brightnessPercentage, 30, 10); 
    } else {
      device_state.lastColor = device_state.color; 
      smoothTransitionToColor({0, 0, 0}, device_state.brightnessPercentage, 30, 10); 
    }
    device_state.powerState = state;
  });
  return true;
}

bool onBrightness(const String &deviceId, int &brightnessPercentage) {
  eventQueue.push([=]() {
    int previousBrightness = device_state.brightnessPercentage; 
    device_state.brightnessPercentage = brightnessPercentage; 
    
    if (device_state.powerState) {
      smoothTransitionToBrightness(previousBrightness, brightnessPercentage, 30, 10); 
    }
  });
  return true;
}

bool onColor(const String &deviceId, byte &r, byte &g, byte &b) {
  if (r == -1 && g == -1 && b == -1) return true; 
  eventQueue.push([=]() {
    smoothTransitionToColor({r, g, b}, device_state.brightnessPercentage, 30, 10); 
    device_state.color = {r, g, b};
    device_state.lastColor = device_state.color; 
  });
  return true;
}

bool onColorTemperature(const String &deviceId, int &colorTemperature) {
  eventQueue.push([=]() {
    ColorRGB color = kelvinToRGB(colorTemperature);
    smoothTransitionToColor(color, device_state.brightnessPercentage, 30, 10); 
    device_state.colorTemperature = colorTemperature;
    device_state.color = color;
    device_state.lastColor = color; 
  });
  return true;
}

void processQueue() {
  if (!eventQueue.empty()) {
    eventQueue.front()();
    eventQueue.pop();
  }
}

void reconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
  }
}

void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS); 
  WiFi.setAutoReconnect(true);  
  WiFi.persistent(true);        
  WiFi.setSleep(false);         

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void setupSinricPro() {
  SinricProLight &myLight = SinricPro[LIGHT_ID];
  myLight.onPowerState(onPowerState);
  myLight.onBrightness(onBrightness);
  myLight.onColor(onColor);
  myLight.onColorTemperature(onColorTemperature);
  
  SinricPro.begin(APP_KEY, APP_SECRET);
}

void setup() {
  Serial.begin(BAUD_RATE);
  setupColorTemperatureIndex();
  setupWiFi();
  setupSinricPro();
  
  strip.begin();
  strip.show();

  smoothTransitionToColor({255, 255, 255}, device_state.brightnessPercentage, 30, 10);
  device_state.color = {255, 255, 255};
  device_state.lastColor = device_state.color; 
}

void loop() {
  SinricPro.handle();
  processQueue();
  reconnectWiFi();
}
