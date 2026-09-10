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
// ESP32 Wi-Fi Provisioning + Sensors + Firestore
// ============================================================

// -------------------- PINS --------------------

#define DHTPIN 4
#define DHTTYPE DHT22

#define RESET_BUTTON_PIN 0

#define SDS_RX 16
#define SDS_TX 17

#define MQ7_PIN 34
#define MQ135_PIN 35

// -------------------- OBJECTS --------------------

DHT dht(DHTPIN, DHTTYPE);

HardwareSerial sdsSerial(2);

WebServer server(80);

Preferences preferences;

// -------------------- ACCESS POINT --------------------

const char* AP_SSID = "ESP32_Setup";
const char* AP_PASSWORD = "12345678";

// -------------------- FIRESTORE --------------------

const char* FIRESTORE_URL =
  "https://firestore.googleapis.com/v1/projects/"
  "aeris-8af63/databases/(default)/documents/devices/device_01";

// -------------------- DASHBOARD --------------------

const char* DASHBOARD_URL =
  "https://hardikpoojary-adhi.github.io/Aeris-demo/index.html";

// -------------------- WIFI --------------------

String savedSSID = "";
String savedPassword = "";

bool wifiConnected = false;

// -------------------- SENSOR VALUES --------------------

float pm25 = 0.0;
float pm10 = 0.0;

float temperature = 0.0;
float humidity = 0.0;

int mq7 = 0;
int mq135 = 0;

int currentAQI = 0;

String currentStatus = "Good";

// -------------------- TIMING --------------------

unsigned long lastSensorRead = 0;
unsigned long lastFirestoreUpload = 0;
unsigned long lastWiFiReconnectAttempt = 0;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long FIRESTORE_INTERVAL = 10000;

// Retry Wi-Fi every 30 seconds if it drops
const unsigned long WIFI_RECONNECT_INTERVAL = 30000;

// -------------------- SDS011 --------------------

float PM_CALIBRATION_FACTOR = 1.0;

// -------------------- RESET BUTTON --------------------

unsigned long resetButtonStart = 0;
bool resetButtonHeld = false;


// ============================================================
// FUNCTION PROTOTYPES
// ============================================================

void loadWiFiCredentials();
void saveWiFiCredentials(String ssid, String password);
void clearWiFiCredentials();

void startSetupAP();
bool connectWiFi(String ssid, String password);
void stopWiFiStation();
void printWiFiStatus();

void setupWebServer();
void handleRoot();
void handleConnect();
void handleNotFound();

void readSensors();
void readSDS011();

int calculateAQI(float pm);
String getAQIStatus(int aqi);

String getTimestamp();

void uploadSensorData();

void checkResetButton();


// ============================================================
// WIFI CREDENTIAL STORAGE
// ============================================================

void loadWiFiCredentials() {

  preferences.begin("wifi", true);

  savedSSID =
    preferences.getString("ssid", "");

  savedPassword =
    preferences.getString("password", "");

  preferences.end();

  Serial.println();
  Serial.println("===== SAVED WIFI =====");

  if (savedSSID.length() > 0) {

    Serial.print("SSID: ");
    Serial.println(savedSSID);

  } else {

    Serial.println("No saved Wi-Fi credentials.");
  }

  Serial.println("======================");
}


// ============================================================

void saveWiFiCredentials(String ssid, String password) {

  preferences.begin("wifi", false);

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

  Serial.println();
  Serial.println("Wi-Fi credentials saved.");
}


// ============================================================

void clearWiFiCredentials() {

  preferences.begin("wifi", false);

  preferences.clear();

  preferences.end();

  savedSSID = "";
  savedPassword = "";

  Serial.println();
  Serial.println("Wi-Fi credentials cleared.");
}


// ============================================================
// START ESP32 ACCESS POINT
// ============================================================

void startSetupAP() {

  Serial.println();
  Serial.println("Starting ESP32 setup hotspot...");

  // Completely stop any previous Wi-Fi activity
  WiFi.disconnect(true, false);

  delay(500);

  // AP + Station mode
  WiFi.mode(WIFI_AP_STA);

  delay(200);

  bool result =
    WiFi.softAP(
      AP_SSID,
      AP_PASSWORD,
      1,
      false,
      4
    );

  if (result) {

    Serial.println();
    Serial.println("================================");
    Serial.println("ESP32 SETUP HOTSPOT STARTED");
    Serial.println("================================");

    Serial.print("SSID: ");
    Serial.println(AP_SSID);

    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);

    Serial.print("Setup IP: ");
    Serial.println(WiFi.softAPIP());

    Serial.println("================================");
    Serial.println();

  } else {

    Serial.println(
      "ERROR: Failed to start Access Point."
    );
  }
}


// ============================================================
// STOP WIFI STATION CLEANLY
// ============================================================

void stopWiFiStation() {

  Serial.println();
  Serial.println("Stopping previous Wi-Fi connection...");

  // Stop current STA connection completely.
  // IMPORTANT:
  // false = do not turn off the Wi-Fi radio
  // false = do not erase ESP32 internal STA config
  WiFi.disconnect(true, false);

  delay(700);

  // Re-enable AP + STA mode
  WiFi.mode(WIFI_AP_STA);

  delay(300);

  // Make sure the setup AP still exists
  if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {

    Serial.println(
      "Restarting setup hotspot..."
    );

    WiFi.softAP(
      AP_SSID,
      AP_PASSWORD,
      1,
      false,
      4
    );

    delay(300);
  }

  wifiConnected = false;

  Serial.println(
    "Previous Wi-Fi connection stopped."
  );
}


// ============================================================
// PRINT WIFI STATUS
// ============================================================

void printWiFiStatus() {

  Serial.print("Wi-Fi status: ");

  switch (WiFi.status()) {

    case WL_IDLE_STATUS:
      Serial.println("IDLE");
      break;

    case WL_NO_SSID_AVAIL:
      Serial.println("SSID NOT FOUND");
      break;

    case WL_SCAN_COMPLETED:
      Serial.println("SCAN COMPLETED");
      break;

    case WL_CONNECTED:
      Serial.println("CONNECTED");
      break;

    case WL_CONNECT_FAILED:
      Serial.println("CONNECT FAILED");
      break;

    case WL_CONNECTION_LOST:
      Serial.println("CONNECTION LOST");
      break;

    case WL_DISCONNECTED:
      Serial.println("DISCONNECTED");
      break;

    default:
      Serial.println("UNKNOWN");
      break;
  }
}


// ============================================================
// CONNECT TO WIFI
// ============================================================

bool connectWiFi(
  String ssid,
  String password
) {

  Serial.println();
  Serial.println("================================");
  Serial.println("CONNECTING TO WIFI");
  Serial.println("================================");

  Serial.print("SSID: ");
  Serial.println(ssid);

  // ----------------------------------------------------------
  // IMPORTANT FIX
  // ----------------------------------------------------------
  // Stop ANY previous STA connection before calling WiFi.begin()
  // This prevents:
  //
  // "sta is connecting, cannot set config"
  //
  // ----------------------------------------------------------

  stopWiFiStation();

  delay(500);

  Serial.println();
  Serial.println("Starting new Wi-Fi connection...");

  // Explicitly keep AP + STA mode
  WiFi.mode(WIFI_AP_STA);

  delay(200);

  // Start connection
  if (password.length() == 0) {

    Serial.println(
      "Connecting to open Wi-Fi network..."
    );

    WiFi.begin(
      ssid.c_str()
    );

  } else {

    WiFi.begin(
      ssid.c_str(),
      password.c_str()
    );
  }

  unsigned long startTime =
    millis();

  unsigned long lastDot =
    millis();

  // ----------------------------------------------------------
  // WAIT FOR CONNECTION
  // ----------------------------------------------------------

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 20000
  ) {

    delay(100);

    // Keep web server responsive
    server.handleClient();

    if (
      millis() - lastDot >= 500
    ) {

      Serial.print(".");

      lastDot =
        millis();
    }
  }

  Serial.println();

  // ----------------------------------------------------------
  // SUCCESS
  // ----------------------------------------------------------

  if (
    WiFi.status() == WL_CONNECTED
  ) {

    wifiConnected = true;

    Serial.println();
    Serial.println("================================");
    Serial.println("WIFI CONNECTED");
    Serial.println("================================");

    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    Serial.print("Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    Serial.println();

    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());

    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());

    Serial.println("================================");
    Serial.println();

    return true;
  }

  // ----------------------------------------------------------
  // FAILURE
  // ----------------------------------------------------------

  wifiConnected = false;

  Serial.println();
  Serial.println("================================");
  Serial.println("WIFI CONNECTION FAILED");
  Serial.println("================================");

  printWiFiStatus();

  Serial.println();

  switch (WiFi.status()) {

    case WL_NO_SSID_AVAIL:

      Serial.println(
        "Reason: Wi-Fi network was not found."
      );

      Serial.println(
        "Check that the router is broadcasting 2.4 GHz."
      );

      break;

    case WL_CONNECT_FAILED:

      Serial.println(
        "Reason: Connection failed."
      );

      Serial.println(
        "Check the Wi-Fi password."
      );

      break;

    case WL_CONNECTION_LOST:

      Serial.println(
        "Reason: Connection was lost."
      );

      break;

    case WL_DISCONNECTED:

      Serial.println(
        "Reason: ESP32 is disconnected."
      );

      break;

    default:

      Serial.println(
        "Reason: Unknown Wi-Fi error."
      );

      break;
  }

  Serial.println();
  Serial.println(
    "The ESP32 is ready for another attempt."
  );

  Serial.println(
    "================================"
  );
  Serial.println();

  // ----------------------------------------------------------
  // VERY IMPORTANT
  // ----------------------------------------------------------
  // Cleanly terminate this failed attempt so the next
  // WiFi.begin() can never collide with the old attempt.
  // ----------------------------------------------------------

  WiFi.disconnect(true, false);

  delay(500);

  // Restore AP + STA mode
  WiFi.mode(WIFI_AP_STA);

  delay(200);

  // Make sure setup AP remains alive
  if (
    WiFi.softAPIP() == IPAddress(0, 0, 0, 0)
  ) {

    WiFi.softAP(
      AP_SSID,
      AP_PASSWORD,
      1,
      false,
      4
    );

    delay(300);
  }

  return false;
}


// ============================================================
// SETUP WEB SERVER
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
        "<html><body>"
        "<h2>Wi-Fi credentials cleared.</h2>"
        "<p>Restarting ESP32...</p>"
        "</body></html>"
      );

      delay(1000);

      ESP.restart();
    }
  );

  server.onNotFound(
    handleNotFound
  );

  server.begin();

  Serial.println(
    "Web server started."
  );
}


// ============================================================
// SETUP PAGE
// ============================================================

void handleRoot() {

  String html = R"rawliteral(

<!DOCTYPE html>
<html lang="en">

<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width, initial-scale=1.0">

<title>AERIS — Device Setup</title>

<style>

:root {
  --bg: #050a12;
  --bg2: #081321;
  --text: #edfaff;
  --muted: #8298aa;
  --cyan: #35d6e5;
  --border: rgba(53,214,229,0.18);
}

* {
  box-sizing: border-box;
}

body {
  margin: 0;
  min-height: 100vh;

  font-family: Arial, sans-serif;

  color: var(--text);

  background:
    radial-gradient(
      circle at top right,
      rgba(53,214,229,0.08),
      transparent 35%
    ),
    linear-gradient(
      135deg,
      var(--bg),
      var(--bg2)
    );

  display: flex;
  justify-content: center;
  align-items: center;

  padding: 20px;
}

.container {
  width: 100%;
  max-width: 480px;
}

.brand {
  text-align: center;
  margin-bottom: 24px;
}

.brand-title {
  font-size: 30px;
  font-weight: 800;
  letter-spacing: 4px;
  color: var(--cyan);
}

.brand-sub {
  margin-top: 7px;
  font-size: 12px;
  letter-spacing: 2px;
  color: var(--muted);
  text-transform: uppercase;
}

.card {

  background:
    linear-gradient(
      145deg,
      rgba(14,29,46,0.96),
      rgba(7,17,29,0.96)
    );

  border: 1px solid var(--border);

  border-radius: 18px;

  padding: 28px;

  box-shadow:
    0 20px 60px
    rgba(0,0,0,0.4);
}

.header {
  margin-bottom: 26px;
}

.header h1 {
  margin: 0;
  font-size: 23px;
}

.header p {
  margin-top: 9px;
  color: var(--muted);
  line-height: 1.5;
  font-size: 14px;
}

label {
  display: block;
  margin: 18px 0 8px;
  font-size: 13px;
  color: #a8bdca;
}

input {

  width: 100%;

  padding: 14px 15px;

  border-radius: 12px;

  border:
    1px solid rgba(255,255,255,0.08);

  background: #07111d;

  color: white;

  font-size: 15px;

  outline: none;
}

input:focus {

  border-color: var(--cyan);

  box-shadow:
    0 0 0 3px
    rgba(53,214,229,0.08);
}

button {

  width: 100%;

  margin-top: 25px;

  padding: 14px;

  border: none;

  border-radius: 12px;

  background: var(--cyan);

  color: #031016;

  font-size: 15px;

  font-weight: 800;

  cursor: pointer;
}

.info {

  margin-top: 22px;

  padding: 13px 15px;

  border-radius: 12px;

  background:
    rgba(53,214,229,0.05);

  border:
    1px solid rgba(53,214,229,0.1);

  color: var(--muted);

  font-size: 12px;

  line-height: 1.5;
}

.footer {

  text-align: center;

  margin-top: 18px;

  font-size: 10px;

  letter-spacing: 2px;

  color: #506574;
}

</style>

</head>

<body>

<div class="container">

<div class="brand">

<div class="brand-title">
AERIS
</div>

<div class="brand-sub">
Air Intelligence
</div>

</div>

<div class="card">

<div class="header">

<h1>
Device Network Setup
</h1>

<p>
Connect your AERIS monitoring device
to your local Wi-Fi network.
</p>

</div>

<form action="/connect" method="POST">

<label>
Wi-Fi Network
</label>

<input
type="text"
name="ssid"
placeholder="Enter Wi-Fi name"
required
>

<label>
Wi-Fi Password
</label>

<input
type="password"
name="password"
placeholder="Enter Wi-Fi password"
>

<button type="submit">
Connect Device
</button>

</form>

<div class="info">

<strong>Setup Network</strong><br>

Connected through:
<b>ESP32_Setup</b><br>

Device address:
<b>192.168.4.1</b>

</div>

</div>

<div class="footer">

AERIS — AIR INTELLIGENCE

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
// HANDLE WIFI CONNECTION
// ============================================================

void handleConnect() {

  if (!server.hasArg("ssid")) {

    server.send(
      400,
      "text/html",
      "<h2>Missing Wi-Fi SSID.</h2>"
    );

    return;
  }

  String ssid =
    server.arg("ssid");

  String password = "";

  if (server.hasArg("password")) {

    password =
      server.arg("password");
  }

  ssid.trim();

  Serial.println();
  Serial.println(
    "Wi-Fi credentials received from browser."
  );

  Serial.print("New SSID: ");
  Serial.println(ssid);

  Serial.print("Password length: ");
  Serial.println(password.length());

  // ----------------------------------------------------------
  // TRY NEW CONNECTION
  // ----------------------------------------------------------

  bool success =
    connectWiFi(
      ssid,
      password
    );


  // ==========================================================
  // SUCCESS
  // ==========================================================

  if (success) {

    // Save ONLY after successful connection
    saveWiFiCredentials(
      ssid,
      password
    );

    String ip =
      WiFi.localIP().toString();

    String html =
      "<!DOCTYPE html>"
      "<html>"
      "<head>"
      "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"

      "<style>"

      "body{"
      "margin:0;"
      "min-height:100vh;"
      "display:flex;"
      "align-items:center;"
      "justify-content:center;"
      "background:#050a12;"
      "color:#edfaff;"
      "font-family:Arial;"
      "padding:20px;"
      "}"

      ".card{"
      "max-width:520px;"
      "width:100%;"
      "padding:32px;"
      "text-align:center;"
      "background:#0b1726;"
      "border:1px solid rgba(53,214,229,.18);"
      "border-radius:20px;"
      "}"

      ".logo{"
      "color:#35d6e5;"
      "font-size:30px;"
      "font-weight:bold;"
      "letter-spacing:5px;"
      "}"

      ".success{"
      "font-size:50px;"
      "color:#55e6a5;"
      "margin:20px;"
      "}"

      ".ip{"
      "margin:20px;"
      "padding:12px;"
      "background:#07111d;"
      "border-radius:10px;"
      "color:#35d6e5;"
      "}"

      "a{"
      "display:block;"
      "margin-top:20px;"
      "padding:15px;"
      "background:#35d6e5;"
      "color:#031016;"
      "text-decoration:none;"
      "border-radius:12px;"
      "font-weight:bold;"
      "}"

      "</style>"

      "</head>"

      "<body>"

      "<div class='card'>"

      "<div class='logo'>AERIS</div>"

      "<div class='success'>✓</div>"

      "<h1>Connection Established</h1>"

      "<p>"
      "Your AERIS monitoring device is now connected "
      "and ready for cloud synchronization."
      "</p>"

      "<div class='ip'>Device IP: " +
      ip +
      "</div>"

      "<a href='" +
      String(DASHBOARD_URL) +
      "' target='_blank'>"

      "Open AERIS Dashboard →"

      "</a>"

      "</div>"

      "</body>"
      "</html>";

    server.send(
      200,
      "text/html",
      html
    );

    return;
  }


  // ==========================================================
  // FAILED
  // ==========================================================

  String html = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
content="width=device-width, initial-scale=1.0">

<title>AERIS — Connection Failed</title>

<style>

body {

  margin: 0;

  min-height: 100vh;

  display: flex;

  align-items: center;

  justify-content: center;

  padding: 20px;

  background: #050a12;

  color: #edfaff;

  font-family: Arial, sans-serif;
}

.card {

  max-width: 480px;

  width: 100%;

  padding: 30px;

  text-align: center;

  background: #0b1726;

  border:
    1px solid
    rgba(255,255,255,0.08);

  border-radius: 18px;
}

h1 {

  font-size: 24px;
}

p {

  color: #8298aa;

  line-height: 1.6;
}

button {

  margin-top: 15px;

  padding: 13px 20px;

  border: none;

  border-radius: 10px;

  background: #35d6e5;

  color: #031016;

  font-weight: bold;

  cursor: pointer;
}

</style>

</head>

<body>

<div class="card">

<h1>
Connection Failed
</h1>

<p>

The ESP32 could not connect to the
provided Wi-Fi network.

<br><br>

Check the network name and password
and try again.

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
// NOT FOUND
// ============================================================

void handleNotFound() {

  server.send(
    404,
    "text/plain",
    "AERIS: Page not found."
  );
}


// ============================================================
// DHT + MQ + SDS SENSOR READING
// ============================================================

void readSensors() {

  // ---------------- DHT22 ----------------

  float newTemperature =
    dht.readTemperature();

  float newHumidity =
    dht.readHumidity();

  if (!isnan(newTemperature)) {

    temperature =
      newTemperature;
  }

  if (!isnan(newHumidity)) {

    humidity =
      newHumidity;
  }


  // ---------------- MQ7 ----------------

  mq7 =
    analogRead(MQ7_PIN);


  // ---------------- MQ135 ----------------

  mq135 =
    analogRead(MQ135_PIN);


  // ---------------- SDS011 ----------------

  readSDS011();


  // ---------------- AQI ----------------

  currentAQI =
    calculateAQI(pm25);

  currentStatus =
    getAQIStatus(currentAQI);


  // ---------------- SERIAL OUTPUT ----------------

  Serial.println();
  Serial.println(
    "========== SENSOR DATA =========="
  );

  Serial.print("Temperature: ");
  Serial.print(
    temperature
  );
  Serial.println(" °C");

  Serial.print("Humidity: ");
  Serial.print(
    humidity
  );
  Serial.println(" %");

  Serial.print("PM2.5: ");
  Serial.print(
    pm25
  );
  Serial.println(" µg/m³");

  Serial.print("PM10: ");
  Serial.print(
    pm10
  );
  Serial.println(" µg/m³");

  Serial.print("MQ7: ");
  Serial.println(mq7);

  Serial.print("MQ135: ");
  Serial.println(mq135);

  Serial.print("AQI: ");
  Serial.println(currentAQI);

  Serial.print("Status: ");
  Serial.println(currentStatus);

  Serial.println(
    "================================="
  );
}


// ============================================================
// SDS011 READING
// ============================================================

void readSDS011() {

  while (
    sdsSerial.available() >= 10
  ) {

    uint8_t buffer[10];

    if (
      sdsSerial.read() != 0xAA
    ) {

      continue;
    }

    buffer[0] =
      0xAA;

    for (
      int i = 1;
      i < 10;
      i++
    ) {

      buffer[i] =
        sdsSerial.read();
    }

    if (
      buffer[1] != 0xC0
    ) {

      continue;
    }

    uint8_t checksum =
      0;

    for (
      int i = 2;
      i <= 7;
      i++
    ) {

      checksum +=
        buffer[i];
    }

    if (
      checksum != buffer[8]
    ) {

      continue;
    }

    if (
      buffer[9] != 0xAB
    ) {

      continue;
    }

    uint16_t pm25Raw =
      ((uint16_t)buffer[3] << 8)
      | buffer[2];

    uint16_t pm10Raw =
      ((uint16_t)buffer[5] << 8)
      | buffer[4];

    pm25 =
      (pm25Raw / 10.0)
      * PM_CALIBRATION_FACTOR;

    pm10 =
      (pm10Raw / 10.0)
      * PM_CALIBRATION_FACTOR;

    return;
  }
}


// ============================================================
// AQI CALCULATION
// INDIA PM2.5 BREAKPOINTS
// ============================================================

int calculateAQI(float pm) {

  if (pm <= 30.0) {

    return round(
      ((50.0 - 0.0) /
       (30.0 - 0.0))
      * pm
    );

  } else if (pm <= 60.0) {

    return round(
      ((100.0 - 51.0) /
       (60.0 - 30.0))
      * (pm - 30.0)
      + 51.0
    );

  } else if (pm <= 90.0) {

    return round(
      ((200.0 - 101.0) /
       (90.0 - 60.0))
      * (pm - 60.0)
      + 101.0
    );

  } else if (pm <= 120.0) {

    return round(
      ((300.0 - 201.0) /
       (120.0 - 90.0))
      * (pm - 90.0)
      + 201.0
    );

  } else if (pm <= 250.0) {

    return round(
      ((400.0 - 301.0) /
       (250.0 - 120.0))
      * (pm - 120.0)
      + 301.0
    );

  } else {

    int aqi =
      round(
        ((500.0 - 401.0) /
         (500.0 - 250.0))
        * (pm - 250.0)
        + 401.0
      );

    return constrain(
      aqi,
      401,
      500
    );
  }
}


// ============================================================
// AQI STATUS
// ============================================================

String getAQIStatus(int aqi) {

  if (aqi <= 50) {

    return "Good";

  } else if (aqi <= 100) {

    return "Satisfactory";

  } else if (aqi <= 200) {

    return "Moderate";

  } else if (aqi <= 300) {

    return "Poor";

  } else if (aqi <= 400) {

    return "Very Poor";

  } else {

    return "Severe";
  }
}


// ============================================================
// GET IST TIMESTAMP
// ============================================================

String getTimestamp() {

  struct tm timeinfo;

  if (
    !getLocalTime(&timeinfo)
  ) {

    return "Not Synced";
  }

  char buffer[32];

  strftime(
    buffer,
    sizeof(buffer),
    "%Y-%m-%d %H:%M:%S",
    &timeinfo
  );

  return String(buffer);
}


// ============================================================
// FIRESTORE UPLOAD
// ============================================================

void uploadSensorData() {

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    wifiConnected = false;

    Serial.println(
      "Firestore skipped: Wi-Fi disconnected."
    );

    return;
  }

  wifiConnected = true;

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  if (
    !http.begin(
      client,
      FIRESTORE_URL
    )
  ) {

    Serial.println(
      "Firestore HTTP begin failed."
    );

    return;
  }

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  String timestamp =
    getTimestamp();


  // ==========================================================
  // CREATE FIRESTORE JSON
  // ==========================================================

  String json = "{";

  json += "\"fields\":{";

  json +=
    "\"aqi\":{\"integerValue\":\"" +
    String(currentAQI) +
    "\"},";

  json +=
    "\"status\":{\"stringValue\":\"" +
    currentStatus +
    "\"},";

  json +=
    "\"temp\":{\"doubleValue\":" +
    String(temperature, 2) +
    "},";

  json +=
    "\"humidity\":{\"doubleValue\":" +
    String(humidity, 2) +
    "},";

  json +=
    "\"pm25\":{\"doubleValue\":" +
    String(pm25, 2) +
    "},";

  json +=
    "\"pm10\":{\"doubleValue\":" +
    String(pm10, 2) +
    "},";

  json +=
    "\"mq7\":{\"integerValue\":\"" +
    String(mq7) +
    "\"},";

  json +=
    "\"mq135\":{\"integerValue\":\"" +
    String(mq135) +
    "\"},";

  json +=
    "\"last_updated\":{\"stringValue\":\"" +
    timestamp +
    "\"}";

  json += "}";

  json += "}";


  // ==========================================================
  // UPLOAD
  // ==========================================================

  Serial.println();
  Serial.println(
    "Uploading data to Firestore..."
  );

  int httpCode =
    http.PATCH(json);

  Serial.print(
    "Firestore HTTP code: "
  );

  Serial.println(
    httpCode
  );


  if (httpCode > 0) {

    String response =
      http.getString();

    if (
      httpCode >= 200 &&
      httpCode < 300
    ) {

      Serial.println(
        "Firestore upload successful."
      );

    } else {

      Serial.println(
        "Firestore returned an error:"
      );

      Serial.println(
        response
      );
    }

  } else {

    Serial.print(
      "Firestore request failed: "
    );

    Serial.println(
      http.errorToString(httpCode)
    );
  }

  http.end();
}


// ============================================================
// RESET BUTTON
// ============================================================

void checkResetButton() {

  bool pressed =
    digitalRead(
      RESET_BUTTON_PIN
    ) == LOW;

  if (pressed) {

    if (!resetButtonHeld) {

      resetButtonHeld = true;

      resetButtonStart =
        millis();

      Serial.println(
        "Reset button pressed."
      );
    }

    if (
      millis() -
      resetButtonStart >= 3000
    ) {

      Serial.println();
      Serial.println(
        "RESET BUTTON HELD FOR 3 SECONDS."
      );

      Serial.println(
        "Clearing Wi-Fi credentials..."
      );

      clearWiFiCredentials();

      delay(500);

      ESP.restart();
    }

  } else {

    resetButtonHeld = false;
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println();

  Serial.println(
    "================================"
  );

  Serial.println(
    "        AERIS DEVICE BOOT"
  );

  Serial.println(
    "================================"
  );


  // ---------------- RESET BUTTON ----------------

  pinMode(
    RESET_BUTTON_PIN,
    INPUT_PULLUP
  );


  // ---------------- SENSORS ----------------

  dht.begin();


  sdsSerial.begin(
    9600,
    SERIAL_8N1,
    SDS_RX,
    SDS_TX
  );


  analogReadResolution(12);


  // ---------------- WIFI CREDENTIALS ----------------

  loadWiFiCredentials();


  // ---------------- ACCESS POINT ----------------

  startSetupAP();


  // ---------------- WEB SERVER ----------------

  setupWebServer();


  // ---------------- NTP ----------------

  configTime(
    19800,
    0,
    "pool.ntp.org",
    "time.nist.gov"
  );


  // ---------------- SAVED WIFI ----------------

  if (
    savedSSID.length() > 0
  ) {

    Serial.println(
      "Saved Wi-Fi credentials found."
    );

    bool connected =
      connectWiFi(
        savedSSID,
        savedPassword
      );

    if (!connected) {

      Serial.println();
      Serial.println(
        "Saved Wi-Fi connection failed."
      );

      Serial.println(
        "You can enter new credentials"
      );

      Serial.println(
        "from the AERIS setup page."
      );
    }

  } else {

    Serial.println();

    Serial.println(
      "No saved Wi-Fi credentials."
    );

    Serial.println(
      "Connect to ESP32_Setup"
    );

    Serial.println(
      "and open http://192.168.4.1"
    );
  }


  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "AERIS READY"
  );

  Serial.println(
    "================================"
  );

  Serial.print(
    "Setup page: http://"
  );

  Serial.println(
    WiFi.softAPIP()
  );


  if (wifiConnected) {

    Serial.print(
      "Wi-Fi IP: "
    );

    Serial.println(
      WiFi.localIP()
    );
  }

  Serial.println(
    "================================"
  );
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

  // ----------------------------------------------------------
  // WEB SERVER
  // ----------------------------------------------------------

  server.handleClient();

  checkResetButton();


  // ----------------------------------------------------------
  // SENSOR READING
  // ----------------------------------------------------------

  if (
    millis() -
    lastSensorRead >=
    SENSOR_INTERVAL
  ) {

    lastSensorRead =
      millis();

    readSensors();
  }


  // ----------------------------------------------------------
  // WIFI CONNECTION STATE
  // ----------------------------------------------------------

  if (
    WiFi.status() == WL_CONNECTED
  ) {

    if (!wifiConnected) {

      wifiConnected = true;

      Serial.println();
      Serial.println(
        "Wi-Fi connection restored."
      );

      Serial.print(
        "IP: "
      );

      Serial.println(
        WiFi.localIP()
      );
    }

  } else {

    if (wifiConnected) {

      wifiConnected = false;

      Serial.println();
      Serial.println(
        "Wi-Fi connection lost."
      );

      printWiFiStatus();
    }
  }


  // ----------------------------------------------------------
  // AUTOMATIC WIFI RECONNECT
  // ----------------------------------------------------------
  //
  // Only try automatic reconnect if:
  // 1. Credentials exist
  // 2. Wi-Fi isn't connected
  // 3. 30 seconds have passed
  //
  // ----------------------------------------------------------

  if (
    savedSSID.length() > 0 &&
    WiFi.status() != WL_CONNECTED &&
    millis() -
    lastWiFiReconnectAttempt >=
    WIFI_RECONNECT_INTERVAL
  ) {

    lastWiFiReconnectAttempt =
      millis();

    Serial.println();
    Serial.println(
      "Attempting automatic Wi-Fi reconnect..."
    );

    connectWiFi(
      savedSSID,
      savedPassword
    );
  }


  // ----------------------------------------------------------
  // FIRESTORE UPLOAD
  // ----------------------------------------------------------

  if (
    millis() -
    lastFirestoreUpload >=
    FIRESTORE_INTERVAL
  ) {

    lastFirestoreUpload =
      millis();

    uploadSensorData();
  }
}