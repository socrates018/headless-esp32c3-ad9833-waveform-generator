#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <LittleFS.h>
#include <AD9833.h>
#include <ArduinoJson.h>

#define PIN_FSYNC 7
#define PIN_SCLK 4
#define PIN_MISO 5
#define PIN_MOSI 6

#define AP_SSID "AD9833-Controller"

AD9833 AD(PIN_FSYNC);
WebServer webserver;
DNSServer dnsserver;
IPAddress apIP;

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  webserver.streamFile(file, "text/html");
  file.close();
}

void handleApiConfig() {
  if (webserver.method() == HTTP_POST) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, webserver.arg("plain"));
    
    AD.setFrequency(doc["frequency"]);
    AD.setWave(doc["waveform"]);
    
    webserver.send(200, "application/json", "{\"ok\":true}");
  }
}

void handleNotFound() {
  webserver.sendHeader("Location", String("http://") + apIP.toString() + "/");
  webserver.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  SPI.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_FSYNC);
  AD.begin();
  
  LittleFS.begin(true);
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, nullptr, 1, false, 1);
  
  apIP = WiFi.softAPIP();
  dnsserver.start(53, "*", apIP);
  
  webserver.on("/", HTTP_GET, handleRoot);
  webserver.on("/api/config", HTTP_POST, handleApiConfig);
  webserver.onNotFound(handleNotFound);
  
  webserver.begin();
}

void loop() {
  dnsserver.processNextRequest();
  webserver.handleClient();
  delay(10);
}