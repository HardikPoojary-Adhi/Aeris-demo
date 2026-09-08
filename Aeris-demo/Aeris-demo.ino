#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <DHT.h>
#include <HardwareSerial.h>
#include <time.h>
#include <math.h>

// =====================================================
// AERIS - ESP32 AIR QUALITY MONITOR
// =====================================================

// ---------------- PINS ----------------
#define DHTPIN 4
#define DHTTYPE DHT22

#define RESET_BUTTON_PIN 0

#define SDS_RX 16
#define SDS_TX 17

#define MQ7_PIN 34
#define MQ135_PIN 35

// ---------------- OBJECTS ----------------
DHT dht(DHTPIN, DHTTYPE);

HardwareSerial sdsSerial(2);

WebServer server(80);
DNSServer dnsServer;

Preferences preferences;

// ---------------- WIFI ----------------
String savedSSID = "";
String savedPassword = "";

bool wifiConnected = false;

// ---------------- FIRESTORE ----------------
const char* FIRESTORE_URL =
  "https://firestore.googleapis.com/v1/projects/aeris-8af63/databases/(default)/documents/devices/device_01";

// ---------------- TIMERS ----------------
unsigned long lastSensorRead = 0;
unsigned long lastFirestoreUpload = 0;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long FIRESTORE_INTERVAL = 10000;

// ---------------- SENSOR VALUES ----------------
float pm25 = 0.0;
float pm10 = 0.0;

float temperature = 0.0;
float humidity = 0.0;

float mq7 = 0.0;
float mq135 = 0.0;

int currentAQI = 0;

String currentStatus = "Unknown";

// ---------------- PM CALIBRATION ----------------
// SDS011 already reports PM in µg/m³.
// Keep this at 1.0 unless you calibrate against a trusted monitor.
const float PM_CALIBRATION_FACTOR = 1.0;

// =====================================================
// AQI CALCULATION
// =====================================================

int calculateAQI(float pm25_val) {

  float c_low;
  float c_high;

  float i_low;
  float i_high;

  if (pm25_val <= 30) {

    c_low = 0;
    c_high = 30;

    i_low = 0;
    i_high = 50;
  }

  else if (pm25_val <= 60) {

    c_low = 31;
    c_high = 60;

    i_low = 51;
    i_high = 100;
  }

  else if (pm25_val <= 90) {

    c_low = 61;
    c_high = 90;

    i_low = 101;
    i_high = 200;
  }

  else if (pm25_val <= 120) {

    c_low = 91;
    c_high = 120;

    i_low = 201;
    i_high = 300;
  }

  else if (pm25_val <= 250) {

    c_low = 121;
    c_high = 250;

    i_low = 301;
    i_high = 400;
  }

  else if (pm25_val <= 500) {

    c_low = 251;
    c_high = 500;

    i_low = 401;
    i_high = 500;
  }

  else {

    return 500;
  }

  float aqi =
    ((i_high - i_low) / (c_high - c_low)) *
    (pm25_val - c_low) +
    i_low;

  return (int)round(aqi);
}

// =====================================================
// AQI STATUS
// =====================================================

String getAQIStatus(int aqi) {

  if (aqi <= 50)
    return "Good";

  else if (aqi <= 100)
    return "Satisfactory";

  else if (aqi <= 200)
    return "Moderate";

  else if (aqi <= 300)
    return "Poor";

  else if (aqi <= 400)
    return "Severe";

  else
    return "Hazardous";
}

// =====================================================
// READ SDS011
// =====================================================

bool readSDS011() {

  static uint8_t packet[10];

  // Search for packet start
  while (sdsSerial.available()) {

    uint8_t firstByte = sdsSerial.read();

    if (firstByte != 0xAA) {
      continue;
    }

    // Wait for second header byte
    unsigned long startTime = millis();

    while (!sdsSerial.available()) {

      if (millis() - startTime > 100) {
        return false;
      }

      delay(1);
    }

    uint8_t secondByte = sdsSerial.read();

    if (secondByte != 0xC0) {
      continue;
    }

    packet[0] = 0xAA;
    packet[1] = 0xC0;

    // Read remaining 8 bytes
    int received = 0;

    startTime = millis();

    while (received < 8) {

      if (sdsSerial.available()) {

        packet[received + 2] = sdsSerial.read();
        received++;
      }

      else {

        if (millis() - startTime > 100) {
          return false;
        }

        delay(1);
      }
    }

    // Packet should end with AB
    if (packet[9] != 0xAB) {
      Serial.println("[SDS011] Invalid packet end");
      return false;
    }

    // Checksum
    uint8_t checksum = 0;

    for (int i = 2; i <= 7; i++) {
      checksum += packet[i];
    }

    if (checksum != packet[8]) {

      Serial.println("[SDS011] Checksum error");

      return false;
    }

    // -------------------------------------------------
    // Decode PM2.5
    // -------------------------------------------------

    uint16_t rawPM25 =
      ((uint16_t)packet[3] << 8) |
      packet[2];

    uint16_t rawPM10 =
      ((uint16_t)packet[5] << 8) |
      packet[4];

    float rawPM25Value = rawPM25 / 10.0;
    float rawPM10Value = rawPM10 / 10.0;

    // Apply calibration
    pm25 = rawPM25Value * PM_CALIBRATION_FACTOR;
    pm10 = rawPM10Value * PM_CALIBRATION_FACTOR;

    // -------------------------------------------------
    // DEBUG
    // -------------------------------------------------

    Serial.print("[SDS011] Raw PM2.5: ");
    Serial.print(rawPM25Value, 2);

    Serial.print(" | PM2.5: ");
    Serial.println(pm25, 2);

    Serial.print("[SDS011] Raw PM10: ");
    Serial.print(rawPM10Value, 2);

    Serial.print(" | PM10: ");
    Serial.println(pm10, 2);

    // Calculate AQI immediately
    currentAQI = calculateAQI(pm25);

    currentStatus = getAQIStatus(currentAQI);

    Serial.print("[AQI] ");
    Serial.print(currentAQI);

    Serial.print(" | Status: ");
    Serial.println(currentStatus);

    return true;
  }

  return false;
}

// =====================================================
// READ OTHER SENSORS
// =====================================================

void readOtherSensors() {

  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();

  if (!isnan(newHumidity)) {
    humidity = newHumidity;
  }

  if (!isnan(newTemperature)) {
    temperature = newTemperature;
  }

  mq7 = analogRead(MQ7_PIN);
  mq135 = analogRead(MQ135_PIN);

  Serial.println();
  Serial.println("[SENSORS]");

  Serial.print("Temperature: ");
  Serial.print(temperature, 2);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(humidity, 2);
  Serial.println(" %");

  Serial.print("MQ7: ");
  Serial.println(mq7, 2);

  Serial.print("MQ135: ");
  Serial.println(mq135, 2);
}

// =====================================================
// GET TIME
// =====================================================

String getTimestamp() {

  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {

    return String(millis());
  }

  char buffer[30];

  strftime(
    buffer,
    sizeof(buffer),
    "%Y-%m-%dT%H:%M:%S",
    &timeinfo
  );

  return String(buffer);
}

// =====================================================
// FIRESTORE UPLOAD
// =====================================================

void uploadSensorData() {

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("[FIRESTORE] WiFi not connected");

    return;
  }

  HTTPClient http;

  http.begin(FIRESTORE_URL);

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  // Calculate latest AQI
  currentAQI = calculateAQI(pm25);
  currentStatus = getAQIStatus(currentAQI);

  String timestamp = getTimestamp();

  // Firestore REST API format
  String json = "{";

  json += "\"fields\":{";

  json += "\"aqi\":{";
  json += "\"integerValue\":" + String(currentAQI);
  json += "},";

  json += "\"status\":{";
  json += "\"stringValue\":\"" + currentStatus + "\"";
  json += "},";

  json += "\"temp\":{";
  json += "\"doubleValue\":" + String(temperature, 2);
  json += "},";

  json += "\"humidity\":{";
  json += "\"doubleValue\":" + String(humidity, 2);
  json += "},";

  json += "\"pm25\":{";
  json += "\"doubleValue\":" + String(pm25, 2);
  json += "},";

  json += "\"pm10\":{";
  json += "\"doubleValue\":" + String(pm10, 2);
  json += "},";

  json += "\"mq7\":{";
  json += "\"doubleValue\":" + String(mq7, 2);
  json += "},";

  json += "\"mq135\":{";
  json += "\"doubleValue\":" + String(mq135, 2);
  json += "},";

  json += "\"last_updated\":{";
  json += "\"stringValue\":\"" + timestamp + "\"";
  json += "}";

  json += "}";

  json += "}";

  Serial.println();
  Serial.println("[FIRESTORE] Uploading...");

  int httpCode = http.sendRequest(
    "PATCH",
    json
  );

  Serial.print("[FIRESTORE] HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode >= 200 && httpCode < 300) {

    Serial.println(
      "[FIRESTORE] Upload SUCCESS."
    );
  }

  else {

    Serial.println(
      "[FIRESTORE] Upload FAILED."
    );

    Serial.println(
      http.getString()
    );
  }

  http.end();
}

// =====================================================
// WIFI CONNECTION
// =====================================================

void connectWiFi() {

  if (savedSSID.length() == 0) {

    Serial.println(
      "[WIFI] No saved WiFi credentials."
    );

    return;
  }

  Serial.println();
  Serial.println("[WIFI] Connecting...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    savedSSID.c_str(),
    savedPassword.c_str()
  );

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 15000
  ) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    wifiConnected = true;

    Serial.println(
      "[WIFI] Connected!"
    );

    Serial.print(
      "[WIFI] IP: "
    );

    Serial.println(
      WiFi.localIP()
    );
  }

  else {

    wifiConnected = false;

    Serial.println(
      "[WIFI] Connection failed."
    );
  }
}

// =====================================================
// LOAD WIFI CREDENTIALS
// =====================================================

void loadWiFiCredentials() {

  preferences.begin(
    "wifi",
    true
  );

  savedSSID =
    preferences.getString(
      "ssid",
      ""
    );

  savedPassword =
    preferences.getString(
      "password",
      ""
    );

  preferences.end();

  Serial.print(
    "[WIFI] Saved SSID: "
  );

  Serial.println(
    savedSSID
  );
}

// =====================================================
// RESET WIFI
// =====================================================

void resetWiFi() {

  Serial.println();
  Serial.println(
    "[WIFI] Reset button pressed."
  );

  preferences.begin(
    "wifi",
    false
  );

  preferences.clear();

  preferences.end();

  Serial.println(
    "[WIFI] Credentials cleared."
  );

  delay(1000);

  ESP.restart();
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "        AERIS AIR MONITOR"
  );

  Serial.println(
    "================================"
  );

  // -------------------------------------------------
  // Pins
  // -------------------------------------------------

  pinMode(
    RESET_BUTTON_PIN,
    INPUT_PULLUP
  );

  pinMode(
    MQ7_PIN,
    INPUT
  );

  pinMode(
    MQ135_PIN,
    INPUT
  );

  // -------------------------------------------------
  // DHT
  // -------------------------------------------------

  dht.begin();

  // -------------------------------------------------
  // SDS011
  // -------------------------------------------------

  sdsSerial.begin(
    9600,
    SERIAL_8N1,
    SDS_RX,
    SDS_TX
  );

  Serial.println(
    "[SDS011] Serial started."
  );

  // -------------------------------------------------
  // WiFi
  // -------------------------------------------------

  loadWiFiCredentials();

  connectWiFi();

  // -------------------------------------------------
  // Time
  // -------------------------------------------------

  if (WiFi.status() == WL_CONNECTED) {

    configTime(
      19800,
      0,
      "pool.ntp.org",
      "time.nist.gov"
    );

    Serial.println(
      "[TIME] NTP configured."
    );
  }

  Serial.println();
  Serial.println(
    "[SYSTEM] Setup complete."
  );

  Serial.println(
    "[SYSTEM] Waiting for SDS011 data..."
  );

  Serial.println();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // -------------------------------------------------
  // RESET BUTTON
  // -------------------------------------------------

  if (
    digitalRead(RESET_BUTTON_PIN)
    == LOW
  ) {

    delay(100);

    if (
      digitalRead(RESET_BUTTON_PIN)
      == LOW
    ) {

      resetWiFi();
    }
  }

  // -------------------------------------------------
  // SDS011 + OTHER SENSORS
  // -------------------------------------------------

  if (
    millis() - lastSensorRead
    >= SENSOR_INTERVAL
  ) {

    lastSensorRead = millis();

    readSDS011();

    readOtherSensors();
  }

  // -------------------------------------------------
  // FIRESTORE
  // -------------------------------------------------

  if (
    millis() - lastFirestoreUpload
    >= FIRESTORE_INTERVAL
  ) {

    lastFirestoreUpload = millis();

    uploadSensorData();
  }

  // -------------------------------------------------
  // WIFI RECONNECT
  // -------------------------------------------------

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    wifiConnected = false;
  }

  else {

    wifiConnected = true;
  }

  delay(10);
}