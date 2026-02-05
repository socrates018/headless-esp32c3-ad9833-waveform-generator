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
const uint8_t PIN_FSYNC = 5;   // CS

// --- SPI default pins (ESP32 VSPI) ---
const int PIN_SCLK = 18;  // SCK
const int PIN_MISO = 19;  // MISO (unused by AD9833)
const int PIN_MOSI = 23;  // MOSI

// --- Onboard NeoPixel ---
const uint8_t NEOPIXEL_PIN = 8; // ESP32-C3 DevKitM-1 onboard RGB LED
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

AD9833 AD(PIN_FSYNC);
WebServer server(80);
DNSServer dnsServer;

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

  bool freqOk = isfinite(freq) && freq >= 0.0f && freq <= 10000000.0f;
  if (freqOk) {
    AD.setFrequency(freq);
  }

  if (!waveOk || !freqOk) {
    String msg = "Invalid parameters. freq=0..10000000, wave=sine|triangle|square";
    server.send(400, "text/plain", msg);
    return;
  }

  String msg = "OK: " + String(freq, 2) + " Hz, " + wave;
  server.send(200, "text/plain", msg);
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
  server.on("/generate_204", handlePortal);
  server.on("/hotspot-detect.html", handlePortal);
  server.on("/redirect", handlePortal);
  server.onNotFound(handlePortal);
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}
