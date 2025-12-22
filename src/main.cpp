#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <MQUnifiedsensor.h>
#include <ArduinoJson.h>

#include "config.h"
#include "utils.h"
#include "index_html.h"
#include "result_html.h"

// ================== MQ-135 CONFIG ==================
#define Board                   ("ESP-32")
#define Pin                     (34)
#define Type                    ("MQ-135")
#define Voltage_Resolution      (3.3)
#define ADC_Bit_Resolution      (12)
#define RatioMQ135CleanAir      (3.6)

// ⚠️ ВСТАВ СВІЙ R0
#define MQ135_R0                100.22


#define RELAY_PIN 25
#define REQUEST_DELAY 5000

MQUnifiedsensor MQ135(Board, Voltage_Resolution, ADC_Bit_Resolution, Pin, Type);

// ================== GLOBALS ==================
WebServer webServer(80);
DNSServer dnsServer;
IPAddress apIP(192, 168, 4, 1);

Adafruit_AHTX0 aht;

unsigned long lastReadTime = 0;
unsigned long lastDataSend = 0;

const unsigned long READ_INTERVAL = 5000;
const unsigned long DATA_SEND_INTERVAL = 10 * 60 * 1000UL;
unsigned long lastAlertCheck = 0;

String deviceId = "";
String lastApiResult = "";

float currentTemp = NAN;
float currentHum  = NAN;
float currentCO2  = NAN;


// ================== HTTP HANDLERS ==================
void handleRoot() {
  Serial.println("🌐 Root page requested");
  webServer.send_P(200, "text/html", index_html);
}

void handleNotFound() {
  Serial.println("↩️ Redirect to captive portal");
  webServer.sendHeader("Location", "http://" + apIP.toString(), true);
  webServer.send(302, "text/plain", "");
}

// ================== DEVICE REGISTRATION ==================
void handleConnect() {
  Serial.println("\n🔗 /connect called");

  if (!webServer.hasArg("ssid") || !webServer.hasArg("password")) {
    Serial.println("❌ Missing SSID or password");
    webServer.send(400, "text/plain", "Missing credentials");
    return;
  }

  String ssid = webServer.arg("ssid");
  String password = webServer.arg("password");
  deviceId = generateIdentifier();

  Serial.printf("📶 Connecting to WiFi: %s\n", ssid.c_str());

  WiFi.begin(ssid.c_str(), password.c_str());

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi connection failed");
    lastApiResult = "WiFi failed";
    webServer.send(200, "text/html", getResultPage(deviceId, lastApiResult));
    return;
  }

  Serial.println("✅ WiFi connected");
  Serial.print("📡 IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("🌐 DNS: ");
  Serial.println(WiFi.dnsIP());

  // ---------- HTTPS ----------
  WiFiClientSecure client;
  client.setInsecure();          // 🔥 ОБОВʼЯЗКОВО
  client.setTimeout(15000);

  HTTPClient https;
  https.setTimeout(15000);

  Serial.print("🔐 API URL: ");
  Serial.println(REGISTRATION_API_URL);

  if (!https.begin(client, REGISTRATION_API_URL)) {
    Serial.println("❌ HTTPS begin failed");
    lastApiResult = "HTTPS begin failed";
    webServer.send(200, "text/html", getResultPage(deviceId, lastApiResult));
    return;
  }

  https.addHeader("Content-Type", "application/json");
  https.addHeader("Accept", "application/json");

  String payload =
    "{\"deviceId\":\"" + deviceId +
    "\",\"mac\":\"" + WiFi.macAddress() + "\"}";

  Serial.println("📤 Payload:");
  Serial.println(payload);

  int code = https.POST(payload);

  Serial.print("📥 HTTP code: ");
  Serial.println(code);

  if (code > 0) {
    lastApiResult = https.getString();
    Serial.println("📥 Response:");
    Serial.println(lastApiResult);
  } else {
    lastApiResult = "POST failed: " + String(code);
    Serial.println("❌ POST failed");
  }

  https.end();

  webServer.send(200, "text/html", getResultPage(deviceId, lastApiResult));
}

// ================== SEND SENSOR DATA ==================
void sendSensorData(float t, float h, float co2) {
  Serial.println("📡 Sending sensor data...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected");
    return;
  }

  if (deviceId.isEmpty()) {
    Serial.println("❌ deviceId empty");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient https;
  https.setTimeout(15000);

  String url = String(PARAM_API_URL) + deviceId + "/parameterspost";
  Serial.println(url);

  if (!https.begin(client, url)) {
    Serial.println("❌ HTTPS begin failed");
    return;
  }

  https.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["temperature"] = isnan(t) ? 0 : round(t * 10) / 10.0;
  doc["humidity"]    = isnan(h) ? 0 : round(h * 10) / 10.0;
  doc["co2"]         = (co2 < 400) ? 400 : (int)round(co2);

  String payload;
  serializeJson(doc, payload);

  Serial.println("📤 Payload:");
  Serial.println(payload);

  int code = https.POST(payload);
  Serial.print("📥 HTTP code: ");
  Serial.println(code);

  if (code > 0) {
    Serial.println("📥 Response:");
    Serial.println(https.getString());
  }

  https.end();
}


bool GetAlert(){
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://safepoint-bei0.onrender.com/api/device/" + deviceId + "/doorstatus";

  http.begin(client, url);
  int code = http.GET();

  if (code <= 0) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload)) return false;

  return doc["status"];   // ← ОДИН return
}


void SetRelay(bool alert){
  if (alert) {
    digitalWrite(RELAY_PIN, HIGH);   
    Serial.println("🚨 ALERT → Relay Off");
  } else {
    digitalWrite(RELAY_PIN, LOW);  
    Serial.println("✅ NO ALERT → Relay On");
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n🚀 ESP32 Booting...");
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  Serial.println("🔌 Relay initialized");

  Wire.begin(14, 27);

  if (!aht.begin()) {
    Serial.println("❌ AHT sensor not found");
  } else {
    Serial.println("✅ AHT sensor ready");
  }

  // MQ-135
    MQ135.setRegressionMethod(1);
    MQ135.setA(110.47);
    MQ135.setB(-2.862);
    MQ135.init();
    MQ135.setR0(MQ135_R0);

  Serial.println("🔥 MQ-135 initialized");

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  dnsServer.start(53, "*", apIP);

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/connect", HTTP_POST, handleConnect);
  webServer.onNotFound(handleNotFound);
  webServer.begin();

  Serial.println("🌐 Captive portal ready");
  
}

// ================== LOOP ==================
void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();

  unsigned long now = millis();

  // ---- READ SENSORS ----
  if (now - lastReadTime >= READ_INTERVAL) {
    sensors_event_t humEvent, tempEvent;
    aht.getEvent(&humEvent, &tempEvent);

    currentTemp = tempEvent.temperature;
    currentHum  = humEvent.relative_humidity;

    MQ135.update();
    float ppmRaw = MQ135.readSensor();
    currentCO2 = ppmRaw + 400.0;

    Serial.printf(
      "🌡 %.1f°C | 💧 %.1f%% | 🟢 CO2: %.0f ppm\n",
      currentTemp, currentHum, currentCO2
    );

    lastReadTime = now;
  }

  // ---- SEND DATA ----
  if (now - lastDataSend >= DATA_SEND_INTERVAL) {
    sendSensorData(currentTemp, currentHum, currentCO2);
    lastDataSend = now;
  }
  if (now - lastAlertCheck >= 5000 && !deviceId.isEmpty()) {
    bool alert = GetAlert();
    Serial.println(alert ? "ALERT = TRUE" : "ALERT = FALSE");
    SetRelay(alert);
    lastAlertCheck = now;
  }

  // ---- AUTO WIFI RECONNECT ----
  if (WiFi.status() != WL_CONNECTED && !deviceId.isEmpty() && (now % 60000 < 50)) {
    Serial.println("🔄 Reconnecting WiFi...");
    WiFi.reconnect();
  }

  delay(10);
}
