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
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

#include "config.h"
#include "utils.h"
#include "index_html.h"
#include "result_html.h"

// ================== DEFINES ==================
#define RELAY_PIN 26
#define AP_KEEP_TIME 120000UL
#define WIFI_TRIES 40

#define Board ("ESP-32")
#define Pin 34
#define Type ("MQ-135")
#define Voltage_Resolution 3.3
#define ADC_Bit_Resolution 12
#define RatioMQ135CleanAir 3.6
#define MQ135_R0 100.22

#define READ_INTERVAL 5000
#define DATA_SEND_INTERVAL 60000UL

// ================== OBJECTS ==================
Preferences prefs;
WebServer webServer(80);
DNSServer dnsServer;
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_AHTX0 aht;
MQUnifiedsensor MQ135(Board, Voltage_Resolution, ADC_Bit_Resolution, Pin, Type);

IPAddress apIP(192, 168, 4, 1);

// ================== GLOBALS ==================
String deviceId = "";
bool apRunning = false;
unsigned long apStartTime = 0;

float currentTemp = NAN;
float currentHum  = NAN;
float currentCO2  = NAN;

unsigned long lastReadTime = 0;
unsigned long lastDataSend = 0;
unsigned long lastAlertCheck = 0;
unsigned long lastWiFiLog = 0;

// ================== LCD ICONS ==================
byte tempIcon[8] = {B00100,B01010,B01010,B01110,B01110,B11111,B11111,B00100};
byte humIcon[8]  = {B00100,B00100,B01010,B01010,B10001,B10001,B10001,B01110};
byte co2Icon[8]  = {B00000,B01110,B10001,B11111,B11011,B10001,B01110,B00000};

// ================== FLASH ==================
bool loadCredentials(String &devId, String &ssid, String &pass) {
  Serial.println("📦 Loading credentials from flash...");
  prefs.begin("device", true);
  devId = prefs.getString("deviceId", "");
  ssid  = prefs.getString("ssid", "");
  pass  = prefs.getString("password", "");
  prefs.end();

  Serial.println("   deviceId: " + devId);
  Serial.println("   ssid: " + ssid);
  Serial.println(devId.isEmpty() ? "❌ No saved data" : "✅ Flash OK");

  return !(devId.isEmpty() || ssid.isEmpty());
}

void saveCredentials(const String &devId, const String &ssid, const String &pass) {
  Serial.println("💾 Saving credentials...");
  prefs.begin("device", false);
  prefs.putString("deviceId", devId);
  prefs.putString("ssid", ssid);
  prefs.putString("password", pass);
  prefs.end();
  Serial.println("✅ Credentials saved");
}

// ================== AP ==================
void startAP() {
  if (apRunning) return;

  Serial.println("📡 Starting Access Point...");
  WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  dnsServer.start(53, "*", apIP);

  apRunning = true;
  apStartTime = millis();

  Serial.print("📍 AP IP: ");
  Serial.println(apIP);
}

void stopAP() {
  if (!apRunning) return;

  Serial.println("🛑 Stopping Access Point");
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  apRunning = false;
}

// ================== WEB ==================
void handleRoot() {
  Serial.println("🌐 HTTP /");
  webServer.send_P(200, "text/html", index_html);
}

void handleNotFound() {
  Serial.println("🔁 Redirect captive portal");
  webServer.sendHeader("Location", "http://" + apIP.toString(), true);
  webServer.send(302, "text/plain", "");
}

// ================== CONNECT ==================
void handleConnect() {
  Serial.println("🔗 HTTP /connect");

  if (!webServer.hasArg("ssid") || !webServer.hasArg("password")) {
    Serial.println("❌ Missing credentials");
    webServer.send(400, "text/plain", "Missing credentials");
    return;
  }

  String ssid = webServer.arg("ssid");
  String pass = webServer.arg("password");

  Serial.println("📶 SSID: " + ssid);
  Serial.print("🔑 PASS LEN: ");
  Serial.println(pass.length());

  deviceId = generateIdentifier();
  Serial.println("🆔 Generated deviceId: " + deviceId);

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < WIFI_TRIES) {
    delay(250);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  Serial.print("📡 WiFi status code: ");
  Serial.println(WiFi.status());

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi connection failed");
    webServer.send(200, "text/html",
      getResultPage(deviceId, "WiFi failed"));
    return;
  }

  Serial.println("✅ WiFi connected");
  Serial.print("🌍 IP: ");
  Serial.println(WiFi.localIP());

  // ---- HTTPS REGISTRATION ----
  Serial.println("🔐 Registering device...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  if (https.begin(client, REGISTRATION_API_URL)) {
    https.addHeader("Content-Type", "application/json");
    String payload =
      "{\"deviceId\":\"" + deviceId +
      "\",\"mac\":\"" + WiFi.macAddress() + "\"}";
    int code = https.POST(payload);
    Serial.print("📡 Registration HTTP: ");
    Serial.println(code);
    https.end();
  }

  saveCredentials(deviceId, ssid, pass);

  webServer.send(200, "text/html",
    getResultPage(deviceId, "SUCCESS"));
}

// ================== SEND DATA ==================
void sendSensorData(float t, float h, float co2) {
  if (WiFi.status() != WL_CONNECTED || deviceId.isEmpty()) return;

  Serial.println("📤 Sending sensor data");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = String(PARAM_API_URL) + deviceId + "/parameterspost";
  if (!https.begin(client, url)) {
    Serial.println("❌ HTTPS begin failed");
    return;
  }

  https.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["temperature"] = isnan(t) ? 0 : t;
  doc["humidity"]    = isnan(h) ? 0 : h;
  doc["co2"]         = (int)co2;

  String payload;
  serializeJson(doc, payload);

  int code = https.POST(payload);
  Serial.print("📡 Data HTTP: ");
  Serial.println(code);
  https.end();
}

// ================== ALERT ==================
bool GetAlert() {
  if (WiFi.status() != WL_CONNECTED || deviceId.isEmpty()) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url =
    "https://safepoint-bei0.onrender.com/api/device/" +
    deviceId + "/doorstatus";

  if (!http.begin(client, url)) return false;

  int code = http.GET();
  Serial.print("🚨 Alert HTTP: ");
  Serial.println(code);

  if (code <= 0) {
    http.end();
    return false;
  }

  StaticJsonDocument<256> doc;
  deserializeJson(doc, http.getString());
  http.end();

  return doc["status"];
}

// ================== RELAY ==================
void SetRelay(bool alert) {
  digitalWrite(RELAY_PIN, alert ? HIGH : LOW);
  Serial.println(alert ? "🚨 RELAY ON" : "✅ RELAY OFF");
}

// ================== LCD ==================
void updateLCD() {
  if (digitalRead(RELAY_PIN) == HIGH) {
    lcd.noBacklight();
    lcd.clear();
    Serial.println("📟 LCD OFF (alert)");
    return;
  }

  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.write(0); lcd.printf("%4.1fC ", currentTemp);
  lcd.write(1); lcd.printf("%2.0f%%", currentHum);

  lcd.setCursor(0,1);
  lcd.write(2);
  lcd.printf(" CO2:%dppm", (int)currentCO2);
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n🚀 ESP32 BOOT");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  Wire.begin(14,16);
  Wire1.begin(17,25);

  lcd.init();
  lcd.backlight();
  lcd.createChar(0,tempIcon);
  lcd.createChar(1,humIcon);
  lcd.createChar(2,co2Icon);
  lcd.print("SafePoint");

  aht.begin(&Wire1);

  MQ135.init();
  MQ135.setR0(MQ135_R0);

  String ssid, pass, devId;
  bool hasData = loadCredentials(devId, ssid, pass);

  WiFi.mode(WIFI_AP_STA);

  if (!hasData) {
    Serial.println("🆕 First boot → AP only");
    startAP();
  } else {
    deviceId = devId;
    Serial.println("🔁 Saved creds → STA + AP");
    startAP();
    WiFi.begin(ssid.c_str(), pass.c_str());
  }

  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/connect", HTTP_POST, handleConnect);
  webServer.on("/generate_204", HTTP_ANY, handleRoot);
  webServer.on("/favicon.ico", HTTP_ANY, handleRoot);
  webServer.on("/hotspot-detect.html", HTTP_ANY, handleRoot);
  webServer.on("/ncsi.txt", HTTP_ANY, handleRoot);
  webServer.onNotFound(handleNotFound);

  webServer.begin();
  Serial.println("🌐 Web server ready");
}

// ================== LOOP ==================
void loop() {
  webServer.handleClient();
  dnsServer.processNextRequest();

  unsigned long now = millis();

  if (now - lastReadTime > READ_INTERVAL) {
    sensors_event_t h, t;
    aht.getEvent(&h, &t);

    currentTemp = t.temperature;
    currentHum  = h.relative_humidity;

    MQ135.update();
    currentCO2 = MQ135.readSensor() + 400;

    Serial.printf("🌡 %.1fC 💧 %.0f%% CO2 %dppm\n",
      currentTemp, currentHum, (int)currentCO2);

    updateLCD();
    lastReadTime = now;
  }

  if (now - lastDataSend > DATA_SEND_INTERVAL) {
    sendSensorData(currentTemp, currentHum, currentCO2);
    lastDataSend = now;
  }

  if (now - lastAlertCheck > 5000) {
    SetRelay(GetAlert());
    lastAlertCheck = now;
  }

  if (apRunning && WiFi.status() == WL_CONNECTED &&
      now - apStartTime > AP_KEEP_TIME) {
    stopAP();
  }

  delay(10);
}
