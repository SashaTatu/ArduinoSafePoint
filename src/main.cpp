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

// Встановіть ваше перевірене R0
#define MQ135_R0                100.22   

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

String deviceId = "";
String lastApiResult = "";

float currentTemp = 20.0;
float currentHum = 50.0;
float currentCO2 = 400.0;

// ================== HTTP HANDLERS ==================
void handleRoot() {
  webServer.send_P(200, "text/html", index_html);
}

void handleNotFound() {
  webServer.sendHeader("Location", "http://" + apIP.toString(), true);
  webServer.send(302, "text/plain", "");
}

void handleConnect() {
  if (!webServer.hasArg("ssid") || !webServer.hasArg("password")) {
    webServer.send(400, "text/plain", "Missing SSID");
    return;
  }

  String ssid = webServer.arg("ssid");
  String password = webServer.arg("password");
  deviceId = generateIdentifier();

  WiFi.begin(ssid.c_str(), password.c_str());

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    yield();
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setCACert(root_ca);
    HTTPClient https;
    if (https.begin(client, REGISTRATION_API_URL)) {
      https.addHeader("Content-Type", "application/json");
      String payload = "{\"deviceId\":\"" + deviceId + "\",\"mac\":\"" + WiFi.macAddress() + "\"}";
      int code = https.POST(payload);
      lastApiResult = (code > 0) ? https.getString() : "HTTP error";
      https.end();
    }
  } else {
    lastApiResult = "WiFi failed";
  }
  webServer.send(200, "text/html", getResultPage(deviceId, lastApiResult));
}

// ================== SEND SENSOR DATA ==================
void sendSensorData(float t, float h, float co2) {
  if (WiFi.status() != WL_CONNECTED || deviceId.isEmpty()) return;

  WiFiClientSecure client;
  client.setCACert(root_ca);
  HTTPClient https;
  String url = String(PARAM_API_URL) + deviceId + "/parameterspost";

  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");
    StaticJsonDocument<256> doc;
    
    doc["temperature"] = isnan(t) ? 0 : round(t * 10) / 10.0;
    doc["humidity"]    = isnan(h) ? 0 : round(h * 10) / 10.0;
    // Обмежуємо знизу 400 ppm для логічності даних
    doc["co2"]         = (co2 < 400) ? 400 : (int)round(co2);

    String payload;
    serializeJson(doc, payload);
    https.POST(payload);
    https.end();
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(14, 27);

  if (!aht.begin()) {
    Serial.println("❌ AHT sensor not found");
  } else {
    Serial.println("✅ AHT sensor ready");
  }

  // Налаштування MQ-135 для CO2
  MQ135.setRegressionMethod(1); 
  MQ135.setA(110.47); MQ135.setB(-2.862); 
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

  Serial.println("🌐 System Ready");
}

// ================== LOOP ==================
void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();

  unsigned long now = millis();

  // ---- Читання сенсорів ----
  if (now - lastReadTime >= READ_INTERVAL) {
    sensors_event_t humEvent, tempEvent;
    aht.getEvent(&humEvent, &tempEvent);

    currentTemp = tempEvent.temperature;
    currentHum = humEvent.relative_humidity;

    // Оновлюємо MQ-135 з урахуванням температури (якщо бібліотека підтримує)
    MQ135.update(); 
    
    // MQ-135 вимірює ПРИРІСТ над чистим повітрям.
    // На вулиці (чисте повітря) він покаже ~0-50, тому додаємо 400.
    float ppmRaw = MQ135.readSensor();
    currentCO2 = ppmRaw + 400.0; 

    Serial.printf("🌡 %.1f°C | 💧 %.1f%% | 🟢 CO2: %.0f ppm\n", 
                  currentTemp, currentHum, currentCO2);

    lastReadTime = now;
  }

  // ---- Відправка даних ----
  if (now - lastDataSend >= DATA_SEND_INTERVAL) {
    if (!isnan(currentTemp) && WiFi.status() == WL_CONNECTED) {
      sendSensorData(currentTemp, currentHum, currentCO2);
      Serial.println("📤 Data sent to API");
      lastDataSend = now;
    }
  }
  
  // Авто-перепідключення WiFi якщо була втрата зв'язку
  if (WiFi.status() != WL_CONNECTED && !deviceId.isEmpty() && (now % 60000 < 20)) {
     WiFi.reconnect();
  }

  delay(10);
}