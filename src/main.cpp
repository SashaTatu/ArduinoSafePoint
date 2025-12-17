#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

#include "AHT10.h"
#include "utils.h"
#include "config.h"
#include "index_html.h"
#include "result_html.h"

// ================== MQ-135 ==================
#define MQ135_PIN 34
#define ADC_MAX 4095.0
#define VREF 3.3

#define CO2_A 116.6020682
#define CO2_B -2.769034857

float R0 = 23.08;

// ================== GLOBALS ==================
WebServer webServer(HTTP_PORT);
DNSServer dnsServer;
IPAddress apIP(192, 168, 4, 1);

AHT10 aht;

unsigned long lastAHTRead = 0;
unsigned long lastCO2Read = 0;
unsigned long lastDataSend = 0;

const unsigned long DATA_SEND_INTERVAL = 10 * 60 * 1000UL;

String deviceId = "";
String lastApiResult = "";

// ================== HTTP ==================
void handleRoot() {
  webServer.send_P(200, "text/html", index_html);
}

void handleNotFound() {
  webServer.sendHeader("Location", "http://" + apIP.toString(), true);
  webServer.send(302, "text/plain", "");
}

void handleConnect() {
  if (!webServer.hasArg("ssid") || !webServer.hasArg("password")) {
    webServer.send(400, "text/plain", "Missing SSID or password");
    return;
  }

  String ssid = webServer.arg("ssid");
  String password = webServer.arg("password");

  deviceId = generateIdentifier();
  String mac = WiFi.macAddress();

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Connecting to WiFi");

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(300);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    lastApiResult = "WiFi failed";
    webServer.send(200, "text/html", getResultPage(deviceId, lastApiResult));
    return;
  }

  WiFiClientSecure client;
  client.setCACert(root_ca);
  HTTPClient https;

  if (https.begin(client, REGISTRATION_API_URL)) {
    https.addHeader("Content-Type", "application/json");

    String payload = "{\"deviceId\":\"" + deviceId + "\",\"mac\":\"" + mac + "\"}";
    int code = https.POST(payload);

    lastApiResult = (code > 0) ? https.getString() : "HTTP error";
    https.end();
  }

  webServer.send(200, "text/html", getResultPage(deviceId, lastApiResult));
}

// ================== MQ-135 ==================
float getResistance(int adc) {
  float voltage = adc * (VREF / ADC_MAX);
  if (voltage <= 0.01) return -1;
  return (VREF - voltage) / voltage;
}

float getCO2ppm() {
  int adc = analogRead(MQ135_PIN);
  float Rs = getResistance(adc);
  if (Rs < 0) return -1;

  float ratio = Rs / R0;
  return CO2_A * pow(ratio, CO2_B);
}

// ================== API SEND ==================
void sendSensorData(float t, float h, float co2) {
  if (WiFi.status() != WL_CONNECTED || deviceId == "") return;

  WiFiClientSecure client;
  client.setCACert(root_ca);
  HTTPClient https;

  String url = PARAM_API_URL + deviceId + "/parameterspost";
  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;
    doc["temperature"] = round(t * 100) / 100.0;
    doc["humidity"] = round(h * 100) / 100.0;

    String payload;
    serializeJson(doc, payload);

    https.POST(payload);
    https.end();
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Booting...");

  // I2C
  Wire.begin(16, 17);
  if (!aht.begin()) {
    Serial.println("AHT10 not detected!");
  }

  // MQ-135 ADC
  analogReadResolution(12);
  analogSetPinAttenuation(MQ135_PIN, ADC_11db);

  // WiFi AP
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  dnsServer.start(DNS_PORT, "*", apIP);

  // Web
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/connect", HTTP_POST, handleConnect);
  webServer.onNotFound(handleNotFound);

  webServer.begin();
  Serial.println("AP started: " + WiFi.softAPIP().toString());
}

// ================== LOOP ==================


void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();

  unsigned long now = millis();

  static float lastTemp = NAN;
  static float lastHum = NAN;
  static float lastCO2 = NAN;

  // AHT10
  if (now - lastAHTRead > 1000) {
    aht.measure(&lastTemp, &lastHum);
    lastAHTRead = now;
  }

  // MQ-135
  if (now - lastCO2Read > 2000) {
    lastCO2 = getCO2ppm();
    if (lastCO2 > 0) {
      Serial.printf("CO2: %.0f ppm\n", lastCO2);
    }
    lastCO2Read = now;
  }

  // Send to API
  if (now - lastDataSend > DATA_SEND_INTERVAL &&
      !isnan(lastTemp) && !isnan(lastHum) && !isnan(lastCO2)) {

    sendSensorData(lastTemp, lastHum, lastCO2);
    lastDataSend = now;
  }
}



