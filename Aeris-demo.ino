#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DHT.h>
#include <HardwareSerial.h>
#include <time.h>
#include <math.h>

// ============================================================
// AERIS — AIR INTELLIGENCE
// ESP32 + WiFi Provisioning + Sensors + Firestore
// ============================================================


// ============================================================
// PIN CONFIGURATION
// ============================================================

#define DHTPIN 4
#define DHTTYPE DHT22

#define RESET_BUTTON_PIN 0

#define SDS_RX 16
#define SDS_TX 17

#define MQ7_PIN 34
#define MQ135_PIN 35


// ============================================================
// OBJECTS
// ============================================================

DHT dht(DHTPIN, DHTTYPE);

HardwareSerial sdsSerial(2);

WebServer server(80);

Preferences preferences;


// ============================================================
// ESP32 SETUP HOTSPOT
// ============================================================

const char* AP_SSID = "ESP32_Setup";
const char* AP_PASSWORD = "12345678";


// ============================================================
// FIRESTORE
// ============================================================

const char* FIRESTORE_URL =
  "https://firestore.googleapis.com/v1/projects/"
  "aeris-8af63/databases/(default)/documents/"
  "devices/device_01";


// ============================================================
// WIFI VARIABLES
// ============================================================

String savedSSID = "";
String savedPassword = "";

bool wifiConnected = false;


// ============================================================
// SENSOR TIMING
// ============================================================

unsigned long lastSensorRead = 0;
unsigned long lastFirestoreUpload = 0;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long FIRESTORE_INTERVAL = 10000;


// ============================================================
// SENSOR VALUES
// ============================================================

float pm25 = 0.0;
float pm10 = 0.0;

float temperature = 0.0;
float humidity = 0.0;

float mq7 = 0.0;
float mq135 = 0.0;

int currentAQI = 0;

String currentStatus = "Unknown";


// ============================================================
// CALIBRATION
// ============================================================

const float PM_CALIBRATION_FACTOR = 1.0;


// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void startSetupAP();
void setupWebServer();

void handleRoot();
void handleConnect();
void handleNotFound();

void loadWiFiCredentials();
void saveWiFiCredentials(String ssid, String password);
void clearWiFiCredentials();

bool connectWiFi();

void readSensors();
bool readSDS011();

int calculateAQI(float concentration);
String getAQIStatus(int aqi);

String getTimestamp();

void uploadSensorData();


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(2000);

  Serial.println();
  Serial.println("==============================================");
  Serial.println("        AERIS — AIR INTELLIGENCE");
  Serial.println("==============================================");
  Serial.println();


  // ----------------------------------------------------------
  // GPIO
  // ----------------------------------------------------------

  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  pinMode(MQ7_PIN, INPUT);
  pinMode(MQ135_PIN, INPUT);


  // ----------------------------------------------------------
  // Check reset button
  // ----------------------------------------------------------

  if (digitalRead(RESET_BUTTON_PIN) == LOW) {

    Serial.println("[RESET] Button detected.");
    Serial.println("[RESET] Clearing saved WiFi credentials...");

    clearWiFiCredentials();

    delay(1500);

    Serial.println("[RESET] Restarting ESP32...");

    ESP.restart();
  }


  // ----------------------------------------------------------
  // Sensors
  // ----------------------------------------------------------

  dht.begin();

  sdsSerial.begin(
    9600,
    SERIAL_8N1,
    SDS_RX,
    SDS_TX
  );

  analogReadResolution(12);


  // ----------------------------------------------------------
  // Load saved WiFi
  // ----------------------------------------------------------

  loadWiFiCredentials();


  // ----------------------------------------------------------
  // Start ESP32 setup hotspot
  // ----------------------------------------------------------

  startSetupAP();


  // ----------------------------------------------------------
  // Start web server
  // ----------------------------------------------------------

  setupWebServer();


  // ----------------------------------------------------------
  // Try saved WiFi
  // ----------------------------------------------------------

  if (savedSSID.length() > 0) {

    Serial.println();
    Serial.println("[WIFI] Saved credentials found.");
    Serial.print("[WIFI] SSID: ");
    Serial.println(savedSSID);

    connectWiFi();

  } else {

    Serial.println();
    Serial.println("[WIFI] No saved credentials.");
    Serial.println("[WIFI] Waiting for setup through ESP32_Setup.");
  }


  // ----------------------------------------------------------
  // Start NTP if WiFi connected
  // ----------------------------------------------------------

  if (wifiConnected) {

    configTime(
      19800,
      0,
      "pool.ntp.org",
      "time.nist.gov"
    );

    Serial.println("[TIME] NTP started.");
  }


  Serial.println();
  Serial.println("==============================================");
  Serial.println("              SYSTEM READY");
  Serial.println("==============================================");

  Serial.print("[SETUP] Connect to WiFi hotspot: ");
  Serial.println(AP_SSID);

  Serial.println("[SETUP] Open:");
  Serial.println("        http://192.168.4.1");

  Serial.println();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  // ----------------------------------------------------------
  // Web server
  // ----------------------------------------------------------

  server.handleClient();


  // ----------------------------------------------------------
  // Check reset button
  // ----------------------------------------------------------

  static unsigned long resetPressedAt = 0;

  if (digitalRead(RESET_BUTTON_PIN) == LOW) {

    if (resetPressedAt == 0) {
      resetPressedAt = millis();
    }

    if (millis() - resetPressedAt > 3000) {

      Serial.println();
      Serial.println("[RESET] Button held for 3 seconds.");
      Serial.println("[RESET] Clearing WiFi credentials...");

      clearWiFiCredentials();

      delay(1000);

      ESP.restart();
    }

  } else {

    resetPressedAt = 0;
  }


  // ----------------------------------------------------------
  // Sensor reading
  // ----------------------------------------------------------

  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {

    lastSensorRead = millis();

    readSensors();
  }


  // ----------------------------------------------------------
  // Firestore upload
  // ----------------------------------------------------------

  if (
    wifiConnected &&
    millis() - lastFirestoreUpload >= FIRESTORE_INTERVAL
  ) {

    lastFirestoreUpload = millis();

    uploadSensorData();
  }


  // ----------------------------------------------------------
  // Check WiFi
  // ----------------------------------------------------------

  if (savedSSID.length() > 0) {

    if (WiFi.status() == WL_CONNECTED) {

      wifiConnected = true;

    } else {

      if (wifiConnected) {

        Serial.println();
        Serial.println("[WIFI] Connection lost.");

      }

      wifiConnected = false;
    }
  }


  delay(2);
}


// ============================================================
// START ESP32 SETUP HOTSPOT
// ============================================================

void startSetupAP() {

  Serial.println();
  Serial.println("[AP] Starting ESP32 setup hotspot...");


  WiFi.disconnect(true, true);

  delay(1000);


  // AP + Station simultaneously
  WiFi.mode(WIFI_AP_STA);

  delay(500);


  bool result = WiFi.softAP(
    AP_SSID,
    AP_PASSWORD,
    1,
    false,
    4
  );


  if (result) {

    Serial.println();
    Serial.println("******** AP STARTED ********");

    Serial.print("SSID: ");
    Serial.println(AP_SSID);

    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);

    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());

    Serial.println("*****************************");

  } else {

    Serial.println();
    Serial.println("[AP] ERROR: Failed to start hotspot.");
  }
}


// ============================================================
// WEB SERVER
// ============================================================

void setupWebServer() {

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );


  server.on(
    "/connect",
    HTTP_POST,
    handleConnect
  );


  server.on(
    "/reset",
    HTTP_GET,
    []() {

      clearWiFiCredentials();

      server.send(
        200,
        "text/html",
        "<html>"
        "<head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "</head>"
        "<body>"
        "<h2>WiFi credentials cleared.</h2>"
        "<p>Restarting ESP32...</p>"
        "</body>"
        "</html>"
      );

      delay(1000);

      ESP.restart();
    }
  );


  server.onNotFound(handleNotFound);


  server.begin();

  Serial.println("[WEB] Server started.");
}


// ============================================================
// ROOT PAGE
// ============================================================

void handleRoot() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html>

<head>

<meta name="viewport"
      content="width=device-width, initial-scale=1">

<title>AERIS WiFi Setup</title>

<style>

* {
  box-sizing: border-box;
}

body {

  margin: 0;
  padding: 20px;

  min-height: 100vh;

  font-family:
    Arial,
    Helvetica,
    sans-serif;

  background:
    linear-gradient(
      145deg,
      #050912,
      #091525
    );

  color: #eafcff;

  display: flex;

  justify-content: center;

  align-items: center;
}

.card {

  width: 100%;
  max-width: 430px;

  padding: 30px;

  border-radius: 22px;

  background:
    rgba(10, 23, 38, 0.94);

  border:
    1px solid
    rgba(53, 214, 229, 0.25);

  box-shadow:
    0 20px 60px
    rgba(0,0,0,0.45);
}

.logo {

  font-size: 28px;

  font-weight: 800;

  letter-spacing: 3px;

  color: #35d6e5;

  margin-bottom: 5px;
}

.subtitle {

  color: #8da5b5;

  margin-bottom: 25px;
}

label {

  display: block;

  margin-top: 16px;
  margin-bottom: 7px;

  color: #9bb3c2;

  font-size: 14px;
}

input {

  width: 100%;

  padding: 14px;

  border-radius: 12px;

  border:
    1px solid
    #263d50;

  background: #07111d;

  color: white;

  outline: none;

  font-size: 15px;
}

input:focus {

  border-color: #35d6e5;

  box-shadow:
    0 0 0 2px
    rgba(53,214,229,0.12);
}

button {

  width: 100%;

  margin-top: 24px;

  padding: 15px;

  border: none;

  border-radius: 12px;

  background: #35d6e5;

  color: #041017;

  font-size: 16px;

  font-weight: 700;

  cursor: pointer;
}

.status {

  margin-top: 22px;

  padding: 14px;

  border-radius: 12px;

  background: #07111d;

  color: #8da5b5;

  font-size: 14px;
}

</style>

</head>


<body>

<div class="card">

  <div class="logo">
    AERIS
  </div>

  <div class="subtitle">
    Air Intelligence — WiFi Setup
  </div>


  <form action="/connect"
        method="POST">

    <label>
      WiFi Network
    </label>

    <input
      type="text"
      name="ssid"
      placeholder="Enter WiFi name"
      required
    >


    <label>
      WiFi Password
    </label>

    <input
      type="password"
      name="password"
      placeholder="Enter WiFi password"
    >


    <button type="submit">
      Connect & Save
    </button>

  </form>


  <div class="status">

    <b>ESP32 Setup Network</b>

    <br><br>

    SSID:
    ESP32_Setup

    <br>

    IP:
    192.168.4.1

  </div>

</div>

</body>

</html>
)rawliteral";


  server.send(
    200,
    "text/html",
    html
  );
}


// ============================================================
// CONNECT TO WIFI
// ============================================================

void handleConnect() {

  if (!server.hasArg("ssid")) {

    server.send(
      400,
      "text/html",
      "<h2>Missing WiFi SSID.</h2>"
    );

    return;
  }


  String ssid =
    server.arg("ssid");

  String password =
    server.arg("password");


  ssid.trim();


  if (ssid.length() == 0) {

    server.send(
      400,
      "text/html",
      "<h2>SSID cannot be empty.</h2>"
    );

    return;
  }


  Serial.println();
  Serial.println("==============================================");
  Serial.println("[WIFI] New credentials received");
  Serial.println("==============================================");

  Serial.print("[WIFI] SSID: ");
  Serial.println(ssid);

  Serial.println("[WIFI] Connecting...");


  // ----------------------------------------------------------
  // Try new network
  // ----------------------------------------------------------

  WiFi.disconnect(false);

  delay(500);

  WiFi.begin(
    ssid.c_str(),
    password.c_str()
  );


  unsigned long startTime =
    millis();


  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 20000
  ) {

    delay(500);

    Serial.print(".");
  }


  Serial.println();


  // ----------------------------------------------------------
  // SUCCESS
  // ----------------------------------------------------------

  if (WiFi.status() == WL_CONNECTED) {

    wifiConnected = true;


    // Save credentials permanently
    saveWiFiCredentials(
      ssid,
      password
    );


    configTime(
      19800,
      0,
      "pool.ntp.org",
      "time.nist.gov"
    );


    Serial.println();
    Serial.println("******** WIFI CONNECTED ********");

    Serial.print("SSID: ");
    Serial.println(ssid);

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    Serial.println("Credentials saved.");

    Serial.println("********************************");


    String html = R"rawliteral(
<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
      content="width=device-width, initial-scale=1">

<title>AERIS Connected</title>

<style>

body {

  margin: 0;

  min-height: 100vh;

  display: flex;

  align-items: center;

  justify-content: center;

  background: #060a10;

  color: white;

  font-family: Arial;

  padding: 20px;
}

.card {

  width: 100%;

  max-width: 420px;

  padding: 30px;

  border-radius: 22px;

  background: #0a1422;

  text-align: center;

  border:
    1px solid
    rgba(53,214,229,.3);
}

.ok {

  font-size: 55px;

  margin-bottom: 10px;
}

h1 {

  color: #35d6e5;
}

.ip {

  padding: 14px;

  margin-top: 20px;

  background: #07111d;

  border-radius: 12px;

  font-family: monospace;
}

</style>

</head>

<body>

<div class="card">

<div class="ok">
✓
</div>

<h1>
WiFi Connected
</h1>

<p>
AERIS is now connected to your WiFi network.
</p>

<div class="ip">

ESP32 IP:
)rawliteral";


    html += WiFi.localIP().toString();


    html += R"rawliteral(

</div>

<p>
Your WiFi credentials have been saved.
</p>

<p>
You can now close this page.
</p>

</div>

</body>

</html>
)rawliteral";


    server.send(
      200,
      "text/html",
      html
    );


    return;
  }


  // ----------------------------------------------------------
  // FAILED
  // ----------------------------------------------------------

  wifiConnected = false;


  Serial.println();
  Serial.println("[WIFI] Connection FAILED.");


  String html = R"rawliteral(
<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
      content="width=device-width, initial-scale=1">

<title>AERIS WiFi Error</title>

<style>

body {

  margin: 0;

  min-height: 100vh;

  display: flex;

  align-items: center;

  justify-content: center;

  background: #060a10;

  color: white;

  font-family: Arial;

  padding: 20px;
}

.card {

  width: 100%;

  max-width: 420px;

  padding: 30px;

  border-radius: 22px;

  background: #0a1422;

  text-align: center;
}

h1 {

  color: #ff7373;
}

button {

  margin-top: 20px;

  padding: 14px 25px;

  border: none;

  border-radius: 10px;

  background: #35d6e5;

  font-weight: bold;
}

</style>

</head>

<body>

<div class="card">

<h1>
Connection Failed
</h1>

<p>
Could not connect to the WiFi network.
</p>

<p>
Check the SSID and password and try again.
</p>

<button onclick="location.href='/'">
Try Again
</button>

</div>

</body>

</html>
)rawliteral";


  server.send(
    200,
    "text/html",
    html
  );
}


// ============================================================
// 404
// ============================================================

void handleNotFound() {

  server.send(
    404,
    "text/plain",
    "AERIS: Page not found."
  );
}


// ============================================================
// LOAD SAVED WIFI CREDENTIALS
// ============================================================

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


  Serial.println();

  Serial.println("[MEMORY] WiFi credentials:");

  if (savedSSID.length() > 0) {

    Serial.print("SSID: ");
    Serial.println(savedSSID);

    Serial.println("Password: SAVED");

  } else {

    Serial.println("No credentials saved.");
  }
}


// ============================================================
// SAVE WIFI CREDENTIALS
// ============================================================

void saveWiFiCredentials(
  String ssid,
  String password
) {

  preferences.begin(
    "wifi",
    false
  );


  preferences.putString(
    "ssid",
    ssid
  );


  preferences.putString(
    "password",
    password
  );


  preferences.end();


  savedSSID = ssid;
  savedPassword = password;


  Serial.println("[MEMORY] WiFi credentials saved.");
}


// ============================================================
// CLEAR WIFI CREDENTIALS
// ============================================================

void clearWiFiCredentials() {

  preferences.begin(
    "wifi",
    false
  );


  preferences.clear();


  preferences.end();


  savedSSID = "";
  savedPassword = "";


  Serial.println("[MEMORY] WiFi credentials cleared.");
}


// ============================================================
// CONNECT USING SAVED WIFI
// ============================================================

bool connectWiFi() {

  if (savedSSID.length() == 0) {

    Serial.println(
      "[WIFI] No saved SSID."
    );

    return false;
  }


  Serial.println();
  Serial.println("[WIFI] Connecting to saved network...");

  Serial.print("[WIFI] SSID: ");
  Serial.println(savedSSID);


  WiFi.begin(
    savedSSID.c_str(),
    savedPassword.c_str()
  );


  unsigned long startTime =
    millis();


  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 20000
  ) {

    delay(500);

    Serial.print(".");
  }


  Serial.println();


  if (WiFi.status() == WL_CONNECTED) {

    wifiConnected = true;


    Serial.println();
    Serial.println("******** WIFI CONNECTED ********");

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    Serial.println("********************************");


    configTime(
      19800,
      0,
      "pool.ntp.org",
      "time.nist.gov"
    );


    return true;
  }


  wifiConnected = false;


  Serial.println(
    "[WIFI] Could not connect using saved credentials."
  );


  return false;
}


// ============================================================
// READ ALL SENSORS
// ============================================================

void readSensors() {

  // ----------------------------------------------------------
  // DHT22
  // ----------------------------------------------------------

  float newHumidity =
    dht.readHumidity();


  float newTemperature =
    dht.readTemperature();


  if (
    !isnan(newHumidity) &&
    !isnan(newTemperature)
  ) {

    humidity =
      newHumidity;

    temperature =
      newTemperature;
  }


  // ----------------------------------------------------------
  // SDS011
  // ----------------------------------------------------------

  readSDS011();


  // ----------------------------------------------------------
  // MQ7
  // ----------------------------------------------------------

  mq7 =
    analogRead(MQ7_PIN);


  // ----------------------------------------------------------
  // MQ135
  // ----------------------------------------------------------

  mq135 =
    analogRead(MQ135_PIN);


  // ----------------------------------------------------------
  // AQI
  // ----------------------------------------------------------

  currentAQI =
    calculateAQI(pm25);


  currentStatus =
    getAQIStatus(currentAQI);


  // ----------------------------------------------------------
  // Serial output
  // ----------------------------------------------------------

  Serial.println();
  Serial.println("--------------- SENSOR DATA ---------------");

  Serial.print("Temperature : ");
  Serial.print(temperature);
  Serial.println(" °C");

  Serial.print("Humidity    : ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("PM2.5       : ");
  Serial.print(pm25);
  Serial.println(" µg/m³");

  Serial.print("PM10        : ");
  Serial.print(pm10);
  Serial.println(" µg/m³");

  Serial.print("MQ7         : ");
  Serial.println(mq7);

  Serial.print("MQ135       : ");
  Serial.println(mq135);

  Serial.print("AQI         : ");
  Serial.println(currentAQI);

  Serial.print("Status      : ");
  Serial.println(currentStatus);

  Serial.println("-------------------------------------------");
}


// ============================================================
// SDS011 READER
// ============================================================

bool readSDS011() {

  static uint8_t buffer[10];

  while (sdsSerial.available()) {

    uint8_t byteRead =
      sdsSerial.read();


    // --------------------------------------------------------
    // First header byte
    // --------------------------------------------------------

    if (byteRead != 0xAA) {

      continue;
    }


    // --------------------------------------------------------
    // Second header byte
    // --------------------------------------------------------

    unsigned long waitStart =
      millis();


    while (
      !sdsSerial.available() &&
      millis() - waitStart < 100
    ) {

      delay(1);
    }


    if (!sdsSerial.available()) {

      return false;
    }


    if (sdsSerial.read() != 0xC0) {

      continue;
    }


    buffer[0] = 0xAA;
    buffer[1] = 0xC0;


    // --------------------------------------------------------
    // Read remaining 8 bytes
    // --------------------------------------------------------

    int index = 2;

    waitStart = millis();


    while (
      index < 10 &&
      millis() - waitStart < 200
    ) {

      if (sdsSerial.available()) {

        buffer[index++] =
          sdsSerial.read();

        waitStart = millis();
      }
    }


    if (index < 10) {

      return false;
    }


    // --------------------------------------------------------
    // Checksum
    // --------------------------------------------------------

    uint8_t checksum = 0;


    for (int i = 2; i <= 7; i++) {

      checksum += buffer[i];
    }


    if (checksum != buffer[8]) {

      Serial.println(
        "[SDS011] Checksum error."
      );

      return false;
    }


    // --------------------------------------------------------
    // End byte
    // --------------------------------------------------------

    if (buffer[9] != 0xAB) {

      return false;
    }


    // --------------------------------------------------------
    // PM2.5
    // --------------------------------------------------------

    uint16_t rawPM25 =
      buffer[2] |
      (buffer[3] << 8);


    // --------------------------------------------------------
    // PM10
    // --------------------------------------------------------

    uint16_t rawPM10 =
      buffer[4] |
      (buffer[5] << 8);


    pm25 =
      (rawPM25 / 10.0) *
      PM_CALIBRATION_FACTOR;


    pm10 =
      (rawPM10 / 10.0) *
      PM_CALIBRATION_FACTOR;


    return true;
  }


  return false;
}


// ============================================================
// AQI CALCULATION
// India-style PM2.5 AQI breakpoints
// ============================================================

int calculateAQI(
  float concentration
) {

  if (concentration < 0) {

    concentration = 0;
  }


  float c = concentration;


  float aqi;


  if (c <= 30) {

    aqi =
      0 +
      (c - 0) *
      (50.0 - 0.0) /
      (30.0 - 0.0);
  }

  else if (c <= 60) {

    aqi =
      51 +
      (c - 31) *
      (100.0 - 51.0) /
      (60.0 - 31.0);
  }

  else if (c <= 90) {

    aqi =
      101 +
      (c - 61) *
      (200.0 - 101.0) /
      (90.0 - 61.0);
  }

  else if (c <= 120) {

    aqi =
      201 +
      (c - 91) *
      (300.0 - 201.0) /
      (120.0 - 91.0);
  }

  else if (c <= 250) {

    aqi =
      301 +
      (c - 121) *
      (400.0 - 301.0) /
      (250.0 - 121.0);
  }

  else {

    aqi =
      401 +
      (c - 251) *
      (500.0 - 401.0) /
      (350.0 - 251.0);
  }


  if (aqi < 0) {

    aqi = 0;
  }


  if (aqi > 500) {

    aqi = 500;
  }


  return round(aqi);
}


// ============================================================
// AQI STATUS
// ============================================================

String getAQIStatus(
  int aqi
) {

  if (aqi <= 50) {

    return "Good";
  }

  else if (aqi <= 100) {

    return "Satisfactory";
  }

  else if (aqi <= 200) {

    return "Moderate";
  }

  else if (aqi <= 300) {

    return "Poor";
  }

  else if (aqi <= 400) {

    return "Very Poor";
  }

  else {

    return "Severe";
  }
}


// ============================================================
// GET TIMESTAMP
// ============================================================

String getTimestamp() {

  struct tm timeinfo;


  if (
    getLocalTime(
      &timeinfo,
      1000
    )
  ) {

    char timestamp[32];


    strftime(
      timestamp,
      sizeof(timestamp),
      "%Y-%m-%d %H:%M:%S",
      &timeinfo
    );


    return String(timestamp);
  }


  // Fallback if NTP hasn't synced yet

  return String(
    "Uptime: "
  ) +
  String(
    millis() / 1000
  ) +
  "s";
}


// ============================================================
// FIRESTORE UPLOAD
// ============================================================

void uploadSensorData() {

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    Serial.println(
      "[FIRESTORE] WiFi not connected."
    );

    return;
  }


  WiFiClientSecure client;


  // ----------------------------------------------------------
  // For prototype/testing.
  // Production should use proper certificate validation.
  // ----------------------------------------------------------

  client.setInsecure();


  HTTPClient http;


  Serial.println();
  Serial.println("[FIRESTORE] Uploading data...");


  if (
    !http.begin(
      client,
      FIRESTORE_URL
    )
  ) {

    Serial.println(
      "[FIRESTORE] HTTP begin failed."
    );

    return;
  }


  http.addHeader(
    "Content-Type",
    "application/json"
  );


  // ----------------------------------------------------------
  // Build Firestore JSON
  // ----------------------------------------------------------

  String timestamp =
    getTimestamp();


  String json = "{";

  json += "\"fields\":{";


  // AQI

  json +=
    "\"aqi\":{"
    "\"integerValue\":" +
    String(currentAQI) +
    "},";


  // Status

  json +=
    "\"status\":{"
    "\"stringValue\":\"" +
    currentStatus +
    "\"},";


  // Temperature

  json +=
    "\"temp\":{"
    "\"doubleValue\":" +
    String(temperature, 2) +
    "},";


  // Humidity

  json +=
    "\"humidity\":{"
    "\"doubleValue\":" +
    String(humidity, 2) +
    "},";


  // PM2.5

  json +=
    "\"pm25\":{"
    "\"doubleValue\":" +
    String(pm25, 2) +
    "},";


  // PM10

  json +=
    "\"pm10\":{"
    "\"doubleValue\":" +
    String(pm10, 2) +
    "},";


  // MQ7

  json +=
    "\"mq7\":{"
    "\"doubleValue\":" +
    String(mq7, 2) +
    "},";


  // MQ135

  json +=
    "\"mq135\":{"
    "\"doubleValue\":" +
    String(mq135, 2) +
    "},";


  // Timestamp

  json +=
    "\"last_updated\":{"
    "\"stringValue\":\"" +
    timestamp +
    "\"}";


  json += "}";


  json += "}";


  // ----------------------------------------------------------
  // PATCH Firestore document
  // ----------------------------------------------------------

  int httpCode =
    http.PATCH(json);


  Serial.print(
    "[FIRESTORE] HTTP code: "
  );

  Serial.println(
    httpCode
  );


  if (
    httpCode >= 200 &&
    httpCode < 300
  ) {

    Serial.println(
      "[FIRESTORE] Upload successful."
    );

  } else {

    Serial.println(
      "[FIRESTORE] Upload failed."
    );

    Serial.println(
      http.getString()
    );
  }


  http.end();
}