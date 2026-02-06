#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <LittleFS.h>
#include <AD9833.h>
#include <Adafruit_NeoPixel.h>

// --- WiFi (Access Point) ---
const char *AP_SSID = "AD9833-AP";
const char *AP_PASS = "12345678";

// --- AD9833 pins ---
const uint8_t PIN_FSYNC = 7;   // CS (ESP32-C3 default SS)

// --- SPI default pins (ESP32-C3) ---
const int PIN_SCLK = 4;  // SCK
const int PIN_MISO = 5;  // MISO (unused by AD9833)
const int PIN_MOSI = 6;  // MOSI

// --- Onboard NeoPixel ---
const uint8_t NEOPIXEL_PIN = 8; // ESP32-C3 DevKitM-1 onboard RGB LED
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

AD9833 AD(PIN_FSYNC);
WebServer server(80);
DNSServer dnsServer;

// --- AD9833 clock and output limits ---
static constexpr float AD9833_MCLK_HZ = 25000000.0f;
static constexpr float AD9833_MAX_OUT_HZ = AD9833_MCLK_HZ / 2.0f;
static constexpr uint32_t SWEEP_STEP_MS = 25;

static bool outputEnabled = true;
static float currentPhaseDeg = 0.0f;

struct SweepState {
  bool active = false;
  float startHz = 0.0f;
  float stopHz = 0.0f;
  uint32_t durationMs = 0;
  uint32_t startMs = 0;
  uint32_t lastUpdateMs = 0;
};

static SweepState sweep;

static void applyOutputMode() {
  AD.setPowerMode(outputEnabled ? AD9833_PWR_ON : AD9833_PWR_DISABLE_ALL);
}

static void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(500, "text/plain", "Missing /index.html");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

static void handlePortal() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

static void handleSet() {
  String freqStr = server.arg("freq");
  String wave = server.arg("wave");
  float freq = freqStr.toFloat();

  bool waveOk = true;
  if (wave == "sine") {
    AD.setWave(AD9833_SINE);
  } else if (wave == "triangle") {
    AD.setWave(AD9833_TRIANGLE);
  } else if (wave == "square") {
    AD.setWave(AD9833_SQUARE1);
  } else {
    waveOk = false;
  }

  bool freqOk = isfinite(freq) && freq >= 0.0f && freq <= AD9833_MAX_OUT_HZ;
  if (freqOk) {
    AD.setFrequency(freq);
    sweep.active = false;
  }

  if (!waveOk || !freqOk) {
    String msg = "Invalid parameters. freq=0.." + String(AD9833_MAX_OUT_HZ, 0) + ", wave=sine|triangle|square";
    server.send(400, "text/plain", msg);
    return;
  }

  applyOutputMode();

  String msg = "OK: " + String(freq, 2) + " Hz, " + wave;
  server.send(200, "text/plain", msg);
}

static void handleOutput() {
  String stateStr = server.arg("state");
  if (stateStr.length() == 0) {
    server.send(400, "text/plain", "Missing state (0 or 1)");
    return;
  }
  int state = stateStr.toInt();
  if (state != 0 && state != 1) {
    server.send(400, "text/plain", "Invalid state (0 or 1)");
    return;
  }
  outputEnabled = (state == 1);
  applyOutputMode();
  String msg = outputEnabled ? "OK: Output ON" : "OK: Output OFF";
  server.send(200, "text/plain", msg);
}

static void handlePhase() {
  String degStr = server.arg("deg");
  float deg = degStr.toFloat();
  bool phaseOk = isfinite(deg) && deg >= 0.0f && deg <= 360.0f;
  if (!phaseOk) {
    server.send(400, "text/plain", "Invalid phase. deg=0..360");
    return;
  }
  AD.setPhase(deg);
  currentPhaseDeg = deg;
  String msg = "OK: Phase " + String(deg, 0) + " deg";
  server.send(200, "text/plain", msg);
}

static void handleSweep() {
  String startStr = server.arg("start");
  String stopStr = server.arg("stop");
  String durStr = server.arg("dur");
  float startHz = startStr.toFloat();
  float stopHz = stopStr.toFloat();
  uint32_t durMs = (uint32_t)durStr.toInt();

  bool startOk = isfinite(startHz) && startHz >= 0.0f && startHz <= AD9833_MAX_OUT_HZ;
  bool stopOk = isfinite(stopHz) && stopHz >= 0.0f && stopHz <= AD9833_MAX_OUT_HZ;
  bool durOk = durMs > 0;
  if (!startOk || !stopOk || !durOk) {
    server.send(400, "text/plain", "Invalid sweep. start/stop=0..max, dur>0");
    return;
  }

  sweep.active = true;
  sweep.startHz = startHz;
  sweep.stopHz = stopHz;
  sweep.durationMs = durMs;
  sweep.startMs = millis();
  sweep.lastUpdateMs = 0;

  AD.setFrequency(startHz);
  applyOutputMode();
  String msg = "OK: Sweep " + String(startHz, 2) + "->" + String(stopHz, 2) + " Hz in " + String(durMs) + " ms";
  server.send(200, "text/plain", msg);
}

static void updateSweep() {
  if (!sweep.active) {
    return;
  }
  uint32_t now = millis();
  uint32_t elapsed = now - sweep.startMs;
  if (elapsed >= sweep.durationMs) {
    AD.setFrequency(sweep.stopHz);
    applyOutputMode();
    sweep.active = false;
    return;
  }
  if (now - sweep.lastUpdateMs < SWEEP_STEP_MS) {
    return;
  }
  sweep.lastUpdateMs = now;
  float t = (float)elapsed / (float)sweep.durationMs;
  float freq = sweep.startHz + (sweep.stopHz - sweep.startHz) * t;
  AD.setFrequency(freq);
  applyOutputMode();
}

void setup() {
  Serial.begin(115200);

  pixel.begin();
  pixel.setBrightness(40);
  pixel.show();

  SPI.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_FSYNC);
  AD.begin();

  AD.setWave(AD9833_SINE);
  AD.setFrequency(1000);
  applyOutputMode();
  AD.writePhaseRegister(0, 0);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());
  pixel.setPixelColor(0, pixel.Color(0, 150, 0));
  pixel.show();
  Serial.println();
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/output", handleOutput);
  server.on("/phase", handlePhase);
  server.on("/sweep", handleSweep);
  server.on("/generate_204", handlePortal);
  server.on("/hotspot-detect.html", handlePortal);
  server.on("/redirect", handlePortal);
  server.onNotFound(handlePortal);
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  updateSweep();
}
