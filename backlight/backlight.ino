/**************************************************************
 * ESP8266 (NodeMCU) + Adafruit_NeoPixel (СТАТИКА) + WS2812FX (ЭФФЕКТЫ)
 * + SinricPro + Async HTTP server
 * - Плавные переходы (blocking for + delay) для статики
 * - /state (GET/POST)
 * - /effects (GET) — фикс
 * - Старт: белый 255,255,255 с плавным включением
 **************************************************************/

#include <Arduino.h>
#include <math.h>
#include <functional>
#include <queue>
#include <pgmspace.h>

#include <Adafruit_NeoPixel.h>
#include <WS2812FX.h>

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

#include "SinricPro.h"
#include "SinricProLight.h"

#include "credentials.h"   // WIFI_SSID, WIFI_PASS, APP_KEY, APP_SECRET, LIGHT_ID

#define LED_PIN          D4
#define NUM_LEDS         210
#define BAUD_RATE        115200

// Параметры плавности
#define FADE_STEPS       30
#define FADE_STEP_MS     10

// ---------- Рендеры ----------
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);   // статический рендер
WS2812FX fx(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);               // эффекты

// ---------- WEB ----------
AsyncWebServer server(80);

// ---------- Очередь событий ----------
std::queue<std::function<void()>> eventQueue;

// ---------- Типы ----------
struct ColorRGB { uint8_t r, g, b; };

enum class Source { SINRIC, HTTP };
enum class EffectMode { STATIC, WS2812FX_MODE };

struct DeviceState {
  bool power = true;
  int brightness = 100;            // 0..100
  ColorRGB color = {255, 255, 255};
  int colorTemperature = 2700;     // K
  EffectMode effectMode = EffectMode::STATIC;

  uint8_t  ws2812fxMode  = FX_MODE_FIRE_FLICKER_SOFT;
  uint16_t ws2812fxSpeed = 1000;

  Source lastSource = Source::SINRIC;
} device_state;

struct LastReportedToSinric {
  bool power = true;
  int brightness = 100;
  ColorRGB color = {255,255,255};
  int colorTemperature = 2700;
} lastReported;

// ==========================================================

uint32_t toColor(const ColorRGB &c) { return strip.Color(c.r, c.g, c.b); }

ColorRGB kelvinToRGB(int kelvin) {
  float temperature = kelvin / 100.0f;
  float r, g, b;

  if (temperature <= 66.0f) {
    r = 255.0f;
    g = 99.4708025861f * log(temperature) - 161.1195681661f;
    if (temperature <= 19.0f) b = 0.0f;
    else                      b = 138.5177312231f * log(temperature - 10.0f) - 305.0447927307f;
  } else {
    r = 329.698727446f * pow(temperature - 60.0f, -0.1332047592f);
    g = 288.1221695283f * pow(temperature - 60.0f, -0.0755148492f);
    b = 255.0f;
  }

  r = constrain(r, 0, 255);
  g = constrain(g, 0, 255);
  b = constrain(b, 0, 255);
  return { (uint8_t)r, (uint8_t)g, (uint8_t)b };
}

inline void serviceEverything() {
  fx.service();          // если эффект запущен — пусть живёт
  SinricPro.handle();    // чтобы синрик не отваливался
  delay(1);
  yield();
}

// --- helpers for static render ---
void applyStaticInstant(ColorRGB c, int brightnessPercentage) {
  uint8_t b = map(brightnessPercentage, 0, 100, 0, 255);
  strip.setBrightness(b);
  uint32_t col = toColor(c);
  strip.fill(col, 0, NUM_LEDS);
  strip.show();
}

// --- эффект ON/OFF ---
void enableEffect(uint8_t mode, uint16_t speed) {
  // глушим статический рендер (strip ничего “не крутит”, но гасим)
  strip.setBrightness(0);
  strip.show();

  device_state.effectMode = EffectMode::WS2812FX_MODE;

  fx.setMode(mode);
  fx.setSpeed(speed);
  fx.setColor(strip.Color(device_state.color.r, device_state.color.g, device_state.color.b));
  fx.setBrightness(map(device_state.brightness, 0, 100, 0, 255));
  fx.start();
}

void disableEffectToStatic() {
  fx.stop();
  device_state.effectMode = EffectMode::STATIC;
  applyStaticInstant(device_state.color, device_state.brightness);
}

void ensureRendererForCurrentMode() {
  if (!device_state.power) {
    strip.setBrightness(0);
    strip.show();
    fx.stop();
    return;
  }

  if (device_state.effectMode == EffectMode::STATIC) {
    disableEffectToStatic(); // гарантированно в статике
  } else {
    enableEffect(device_state.ws2812fxMode, device_state.ws2812fxSpeed);
  }
}

// ====== Плавные переходы для статики ======
void smoothTransitionToColor(ColorRGB targetColor, int brightnessPercentage, int steps = FADE_STEPS, int stepDelay = FADE_STEP_MS) {
  ColorRGB currentColor = device_state.color;
  for (int step = 0; step <= steps; step++) {
    ColorRGB cur = {
      (uint8_t)(currentColor.r + ((targetColor.r - currentColor.r) * step) / steps),
      (uint8_t)(currentColor.g + ((targetColor.g - currentColor.g) * step) / steps),
      (uint8_t)(currentColor.b + ((targetColor.b - currentColor.b) * step) / steps)
    };

    applyStaticInstant(cur, brightnessPercentage);

    uint32_t start = millis();
    while (millis() - start < (uint32_t)stepDelay) serviceEverything();
  }
  device_state.color = targetColor;
}

void smoothTransitionToBrightness(int startBrightness, int targetBrightness, int steps = FADE_STEPS, int stepDelay = FADE_STEP_MS) {
  for (int step = 0; step <= steps; step++) {
    int br = startBrightness + ((targetBrightness - startBrightness) * step) / steps;
    applyStaticInstant(device_state.color, br);
    uint32_t start = millis();
    while (millis() - start < (uint32_t)stepDelay) serviceEverything();
  }
  device_state.brightness = targetBrightness;
}

// ================== SinricPro ==================
SinricProLight& light() { return SinricPro[LIGHT_ID]; }

void sendStateToSinricIfChanged() {
  auto &dev = light();

  if (device_state.power != lastReported.power) {
    dev.sendPowerStateEvent(device_state.power);
    lastReported.power = device_state.power;
  }

  if (device_state.brightness != lastReported.brightness) {
    dev.sendBrightnessEvent(device_state.brightness);
    lastReported.brightness = device_state.brightness;
  }

  if (device_state.color.r != lastReported.color.r ||
      device_state.color.g != lastReported.color.g ||
      device_state.color.b != lastReported.color.b) {
    dev.sendColorEvent(device_state.color.r, device_state.color.g, device_state.color.b);
    lastReported.color = device_state.color;
  }

  if (device_state.colorTemperature != lastReported.colorTemperature) {
    dev.sendColorTemperatureEvent(device_state.colorTemperature);
    lastReported.colorTemperature = device_state.colorTemperature;
  }
}

// ================== Очередь ==================
void processEventQueue() {
  if (!eventQueue.empty()) {
    auto fn = eventQueue.front();
    eventQueue.pop();
    fn();
  }
}

// ================== SINRIC CALLBACKS ==================
bool onPowerState(const String &deviceId, bool &state) {
  bool target = state;
  eventQueue.push([=](){
    device_state.lastSource = Source::SINRIC;

    if (target == device_state.power) return;

    if (target) {
      device_state.power = true;
      // всегда включаемся в статике с плавным fade-in
      disableEffectToStatic();
      smoothTransitionToBrightness(0, device_state.brightness);
    } else {
      // выключаем: плавно brightness -> 0, затем гасим всё
      if (device_state.effectMode == EffectMode::WS2812FX_MODE) {
        fx.stop();
        applyStaticInstant(device_state.color, device_state.brightness);
      }
      smoothTransitionToBrightness(device_state.brightness, 0);
      device_state.power = false;
      strip.setBrightness(0);
      strip.show();
      fx.stop();
    }

    sendStateToSinricIfChanged();
  });
  return true;
}

bool onBrightness(const String &deviceId, int &brightness) {
  int target = constrain(brightness, 0, 100);
  eventQueue.push([=](){
    device_state.lastSource = Source::SINRIC;

    if (!device_state.power) {
      device_state.brightness = target;
    } else {
      if (device_state.effectMode == EffectMode::WS2812FX_MODE) {
        // при команде яркости из Sinric — вырубаем эффект и делаем плавно статику
        disableEffectToStatic();
      }
      smoothTransitionToBrightness(device_state.brightness, target);
    }

    sendStateToSinricIfChanged();
  });
  return true;
}

bool onColor(const String &deviceId, byte &r, byte &g, byte &b) {
  ColorRGB target = {r, g, b};
  eventQueue.push([=](){
    device_state.lastSource = Source::SINRIC;

    if (device_state.effectMode == EffectMode::WS2812FX_MODE) {
      disableEffectToStatic();
    }

    if (device_state.power) {
      smoothTransitionToColor(target, device_state.brightness);
    } else {
      device_state.color = target;
    }

    sendStateToSinricIfChanged();
  });
  return true;
}

bool onColorTemperature(const String &deviceId, int &colorTemperature) {
  int targetCt = colorTemperature;
  eventQueue.push([=](){
    device_state.lastSource = Source::SINRIC;

    if (device_state.effectMode == EffectMode::WS2812FX_MODE) {
      disableEffectToStatic();
    }

    ColorRGB targetColor = kelvinToRGB(targetCt);
    device_state.colorTemperature = targetCt;

    if (device_state.power) {
      smoothTransitionToColor(targetColor, device_state.brightness);
    } else {
      device_state.color = targetColor;
    }

    sendStateToSinricIfChanged();
  });
  return true;
}

// ================== HTTP helpers ==================
void jsonState(JsonDocument &doc) {
  doc["power"]      = device_state.power;
  doc["brightness"] = device_state.brightness;

  JsonObject color  = doc.createNestedObject("color");
  color["r"] = device_state.color.r;
  color["g"] = device_state.color.g;
  color["b"] = device_state.color.b;

  doc["ct"] = device_state.colorTemperature;

  JsonObject effect = doc.createNestedObject("effect");
  effect["enabled"] = (device_state.effectMode == EffectMode::WS2812FX_MODE);
  effect["mode"]    = device_state.ws2812fxMode;
  effect["speed"]   = device_state.ws2812fxSpeed;

  doc["source"] = (device_state.lastSource == Source::SINRIC) ? "SINRIC" : "HTTP";
}

void httpRespondState(AsyncWebServerRequest *request) {
  StaticJsonDocument<768> doc;
  jsonState(doc);
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

void patchStateDeferred(const JsonVariantConst &root) {
  DeviceState newState = device_state;

  bool wantEffect = false;

  if (root.containsKey("power")) {
    newState.power = root["power"].as<bool>();
  }

  if (root.containsKey("brightness")) {
    newState.brightness = constrain(root["brightness"].as<int>(), 0, 100);
  }

  if (root.containsKey("color")) {
    auto c = root["color"];
    newState.color = {
      (uint8_t)c["r"].as<uint8_t>(),
      (uint8_t)c["g"].as<uint8_t>(),
      (uint8_t)c["b"].as<uint8_t>()
    };
    newState.effectMode = EffectMode::STATIC;
  }

  if (root.containsKey("ct")) {
    int newCt = root["ct"].as<int>();
    newState.colorTemperature = newCt;
    newState.color = kelvinToRGB(newCt);
    newState.effectMode = EffectMode::STATIC;
  }

  if (root.containsKey("effect")) {
    auto e = root["effect"];
    wantEffect = e["enabled"] | false;
    if (wantEffect) {
      newState.effectMode = EffectMode::WS2812FX_MODE;
      if (e.containsKey("mode"))  newState.ws2812fxMode = e["mode"].as<uint8_t>();
      if (e.containsKey("speed")) newState.ws2812fxSpeed = e["speed"].as<uint16_t>();
    } else {
      newState.effectMode = EffectMode::STATIC;
    }
  }

  eventQueue.push([=](){
    device_state.lastSource = Source::HTTP;

    DeviceState from = device_state;
    device_state = newState;

    if (!from.power && newState.power) {
      // включаемся: если сразу эффект — просто включаем эффект,
      // иначе — плавно статику
      if (newState.effectMode == EffectMode::WS2812FX_MODE) {
        enableEffect(newState.ws2812fxMode, newState.ws2812fxSpeed);
      } else {
        disableEffectToStatic();
        smoothTransitionToBrightness(0, newState.brightness);
        smoothTransitionToColor(newState.color, newState.brightness);
      }
    } else if (from.power && !newState.power) {
      // выключаем: плавно гасим
      if (from.effectMode == EffectMode::WS2812FX_MODE) {
        fx.stop();
        applyStaticInstant(from.color, from.brightness);
      }
      smoothTransitionToBrightness(from.brightness, 0);
      strip.setBrightness(0);
      strip.show();
      fx.stop();
    } else {
      // оба включены
      if (newState.effectMode == EffectMode::WS2812FX_MODE) {
        enableEffect(newState.ws2812fxMode, newState.ws2812fxSpeed);
      } else {
        // статический плавный переход
        disableEffectToStatic();
        if (from.brightness != newState.brightness)
          smoothTransitionToBrightness(from.brightness, newState.brightness);
        if (from.color.r != newState.color.r || from.color.g != newState.color.g || from.color.b != newState.color.b)
          smoothTransitionToColor(newState.color, newState.brightness);
      }
    }

    sendStateToSinricIfChanged();
  });
}

// ================== WIFI / SINRIC / HTTP INIT ==================
void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

void setupSinric() {
  SinricProLight &myLight = light();
  myLight.onPowerState(onPowerState);
  myLight.onBrightness(onBrightness);
  myLight.onColor(onColor);
  myLight.onColorTemperature(onColorTemperature);

  SinricPro.onConnected([](){ Serial.println("SinricPro connected"); });
  SinricPro.onDisconnected([](){ Serial.println("SinricPro disconnected"); });

  SinricPro.begin(APP_KEY, APP_SECRET);
}

void setupHttp() {
  // CORS
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "content-type");

  // GET /state
  server.on("/state", HTTP_GET, [](AsyncWebServerRequest *request){
    httpRespondState(request);
  });

  // POST /state
  auto stateHandler = new AsyncCallbackJsonWebHandler("/state",
    [](AsyncWebServerRequest *request, JsonVariant &json){
      const JsonVariantConst root = json.as<JsonVariantConst>();
      patchStateDeferred(root);
      httpRespondState(request);
    }
  );
  server.addHandler(stateHandler);

  // GET /effects  -> список всех эффектов
  server.on("/effects", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncResponseStream *response = request->beginResponseStream("application/json");

    uint8_t count = fx.getModeCount();
    response->print("{\"effects\":[");

    for (uint8_t i = 0; i < count; i++) {
      char name[48];
      strncpy_P(name, (PGM_P)fx.getModeName(i), sizeof(name));
      name[sizeof(name)-1] = 0;

      response->printf("{\"id\":%u,\"name\":\"%s\"}", i, name);
      if (i < count - 1) response->print(",");
    }

    response->print("]}");
    request->send(response);
  });

  // OPTIONS fallback
  server.onNotFound([](AsyncWebServerRequest *request){
    if (request->method() == HTTP_OPTIONS) request->send(200);
    else request->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("HTTP server started");
}

// ================== SETUP / LOOP ==================
void setup() {
  Serial.begin(BAUD_RATE);
  delay(200);

  setupWiFi();
  setupSinric();

  strip.begin();
  strip.show(); // гасим

  fx.init();
  fx.stop();

  setupHttp();

  // старт: белый и плавно включаемся в статике
  device_state.power = true;
  device_state.effectMode = EffectMode::STATIC;
  disableEffectToStatic();
  smoothTransitionToBrightness(0, device_state.brightness);

  Serial.println("Setup done");
}

void loop() {
  fx.service();        // если эффект включён
  processEventQueue(); // события
  SinricPro.handle();  // синрик
}
