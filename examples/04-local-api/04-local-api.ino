/**
 * Ambient Edu — Sensor Read + LED Alert
 * 
 * Reads all measurements from the Sensirion SEN66 air quality sensor and
 * prints them to Serial every second. A simple if/else block sets the
 * NeoPixel LED colour based on PM2.5 — edit the thresholds to taste!
 * 
 * BOARD SELECTION
 * ───────────────
 * Uncomment ONE of the two #define lines below to match your hardware.
 * This controls pin assignments and whether the power-enable rail is used.
 * 
 * Board:    ESP32-C6 (select "ESP32C6 Dev Module" in Arduino IDE)
 * Requires: - Adafruit NeoPixel   (Library Manager)
 *           - Sensirion I2C SEN66 (install manually or from Library Manager)
 *           - Sensirion Core      (installed automatically with the above)
 */

// ═══════════════════════════════════════════════════════════════
// ██  BOARD SELECTION — uncomment exactly ONE line  ████████████
// ═══════════════════════════════════════════════════════════════
// #define BOARD_AMBIENT_ONE         // Ambient One PCB (Rev5)
#define BOARD_SPARKFUN_C6      // SparkFun ESP32-C6 Thing Plus
// ═══════════════════════════════════════════════════════════════

// ── Board-specific pin mapping ──────────────────────────────
#if defined(BOARD_AMBIENT_ONE)
  // Ambient One Rev5 PCB
  #define NEOPIXEL_PIN    23
  #define I2C_SDA         19
  #define I2C_SCL         20
  #define POWER_EN_PIN    15    // Switched power rail — must be HIGH for sensor
  #define POWER_DET_PIN    2    // Power-button detect (input, not required here)
  #define HAS_POWER_EN    true  // This board needs the power rail enabled

#elif defined(BOARD_SPARKFUN_C6)
  // SparkFun ESP32-C6 Thing Plus
  #define NEOPIXEL_PIN    23    // WS2812 status LED data in
  #define I2C_SDA          6    // Default Qwiic / I2C SDA
  #define I2C_SCL          7    // Default Qwiic / I2C SCL
  #define SPI_CS_PIN      18    // SPI chip select
  #define SD_DETECT_PIN   22    // SD card detect
  #define MAX17_ALERT_PIN 11    // MAX17038 fuel gauge alert
  #define LP_CTRL_PIN     15    // Low-power control
  #define HAS_POWER_EN    false // No switched power rail on this board

#else
  #error "No board selected! Uncomment BOARD_AMBIENT_ONE or BOARD_SPARKFUN_C6 at the top of this file."
#endif

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

// ── History / Logging ────────────────────────────────────────
#define LOG_FILE        "/history.csv"
#define MAX_LOG_LINES   2880     // 48 h at 1-min intervals
#define LOG_INTERVAL_MS 60000UL  // Log a reading every 60 s

// ── WiFi Credentials ────────────────────────────────────────
const char* ssid = "wifi-ssid";
const char* password = "wifi-password";

// ─────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2cSen66.h>

// ── Optional: Battery Monitor (SparkFun C6 has MAX17048) ────
// Uncomment the next line and install "Adafruit MAX1704X" from Library Manager
// #define ENABLE_BATTERY
#ifdef ENABLE_BATTERY
  #include <Adafruit_MAX1704X.h>
  Adafruit_MAX17048 battery;
  float battPercent()  { return min(battery.cellPercent(), 100.0f); }
  bool  battCharging() { return battery.chargeRate() > 0.1f; }
#endif

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

// ── Globals ─────────────────────────────────────────────────
SensirionI2cSen66 sensor;
WebServer server(80);

static char errorMessage[64];
static int16_t error;
static unsigned long lastLogTime = 0;
static bool lfsAvailable = false;

// –– API logic –––––––––––––––––––––––––––––––––––––––––––––––

String jsonError (String message) {
  String json = "{";
  json += "\"message\":\"" + message + "\"";
  json += "}";

  return json;
}

void handleApi() {

  float pm1p0 = 0, pm2p5 = 0, pm4p0 = 0, pm10p0 = 0;
  float humidity = 0, temperature = 0;
  float vocIndex = 0, noxIndex = 0;
  uint16_t co2 = 0;

  error = sensor.readMeasuredValues(
      pm1p0, pm2p5, pm4p0, pm10p0,
      humidity, temperature, vocIndex, noxIndex, co2);

  if (error != NO_ERROR) {
    Serial.print("Read error: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    server.send(500, "application/json", jsonError("Unable to read sensor data."));
    return;
  }

  // Get timestamp
  time_t now = time(nullptr);
  struct tm* timeInfo = localtime(&now);
  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", timeInfo);

  // Build JSON string
  String json = "{";
  json += "\"timestamp\":\"" + String(timestamp) + "\",";
  json += "\"pm1p0\":"      + String(pm1p0, 2)   + ",";
  json += "\"pm2p5\":"      + String(pm2p5, 2)   + ",";
  json += "\"pm4p0\":"      + String(pm4p0, 2)   + ",";
  json += "\"pm10p0\":"     + String(pm10p0, 2)  + ",";
  json += "\"humidity\":"   + String(humidity, 2) + ",";
  json += "\"temperature\":"+ String(temperature, 2) + ",";
  json += "\"vocIndex\":"   + String(vocIndex, 2) + ",";
  json += "\"noxIndex\":"   + String(noxIndex, 2) + ",";
  json += "\"co2\":"        + String(co2);
#ifdef ENABLE_BATTERY
  json += ",\"battery\":"    + String(battPercent(), 1);
  json += ",\"voltage\":"    + String(battery.cellVoltage(), 2);
  json += ",\"charging\":"   + String(battCharging() ? "true" : "false");
#endif
  json += "}";

  // Send data to endpoint
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);

}

// –– History helpers –––––––––––––––––––––––––––––––––––––––––

// Return the field at fieldIdx from a comma-separated line.
String csvFieldAt(const String& line, int fieldIdx) {
  int count = 0, start = 0;
  for (int i = 0; i <= (int)line.length(); i++) {
    if (i == (int)line.length() || line[i] == ',') {
      if (count == fieldIdx) return line.substring(start, i);
      count++;
      start = i + 1;
    }
  }
  return "";
}

// Remove oldest entries so the log does not exceed MAX_LOG_LINES.
void trimLogIfNeeded() {
  if (!lfsAvailable) return;
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) return;
  int lineCount = 0;
  while (f.available()) { if (f.read() == '\n') lineCount++; }
  f.close();
  if (lineCount <= MAX_LOG_LINES) return;

  int toSkip = lineCount - MAX_LOG_LINES;
  f = LittleFS.open(LOG_FILE, "r");
  if (!f) return;
  File tmp = LittleFS.open("/tmp.csv", "w");
  if (!tmp) { f.close(); return; }

  int skipped = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (skipped < toSkip) { skipped++; continue; }
    tmp.println(line);
  }
  f.close();
  tmp.close();
  LittleFS.remove(LOG_FILE);
  LittleFS.rename("/tmp.csv", LOG_FILE);
}

// Append one reading to the CSV log file.
void logReading(const char* timestamp,
                float pm1p0, float pm2p5, float pm4p0, float pm10p0,
                float humidity, float temperature,
                float vocIndex, float noxIndex, uint16_t co2) {
  if (!lfsAvailable) return;
  File f = LittleFS.open(LOG_FILE, "a");
  if (!f) { Serial.println("Log: failed to open " LOG_FILE); return; }
  f.printf("%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u\n",
           timestamp, pm1p0, pm2p5, pm4p0, pm10p0,
           humidity, temperature, vocIndex, noxIndex, co2);
  f.close();
  trimLogIfNeeded();
}

// GET /api/history — stream stored readings as a JSON array.
void handleHistoryJson() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!lfsAvailable) {
    server.send(503, "application/json", jsonError("Filesystem unavailable"));
    return;
  }
  if (!LittleFS.exists(LOG_FILE)) {
    server.send(200, "application/json", "[]");
    return;
  }
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) {
    server.send(200, "application/json", "[]");
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");

  bool first = true;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (!first) server.sendContent(",");
    first = false;

    String json = "{";
    json += "\"timestamp\":\""  + csvFieldAt(line, 0) + "\",";
    json += "\"pm1p0\":"        + csvFieldAt(line, 1) + ",";
    json += "\"pm2p5\":"        + csvFieldAt(line, 2) + ",";
    json += "\"pm4p0\":"        + csvFieldAt(line, 3) + ",";
    json += "\"pm10p0\":"       + csvFieldAt(line, 4) + ",";
    json += "\"humidity\":"     + csvFieldAt(line, 5) + ",";
    json += "\"temperature\":"  + csvFieldAt(line, 6) + ",";
    json += "\"vocIndex\":"     + csvFieldAt(line, 7) + ",";
    json += "\"noxIndex\":"     + csvFieldAt(line, 8) + ",";
    json += "\"co2\":"          + csvFieldAt(line, 9);
    json += "}";
    server.sendContent(json);
  }
  server.sendContent("]");
  f.close();
}

// GET /api/history/length — get history length (number of entries).
void handleHistoryLength() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!lfsAvailable) {
    server.send(503, "application/json", jsonError("Filesystem unavailable"));
    return;
  }
  if (!LittleFS.exists(LOG_FILE)) {
    server.send(200, "application/json", "{\"length\":0}");
    return;
  }
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) {
    server.send(200, "application/json", "{\"length\":0}");
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"length\":" + String(f.size()) + "}");
  f.close();
}

// GET /api/history/csv — download stored readings as a CSV file.
void handleHistoryCsv() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"ambient_history.csv\"");
  if (!lfsAvailable) {
    server.send(503, "text/plain", "Filesystem unavailable");
    return;
  }
  if (!LittleFS.exists(LOG_FILE)) {
    server.send(404, "text/plain", "No history available");
    return;
  }
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f) {
    server.send(500, "text/plain", "Failed to open log file");
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent(
    "timestamp,pm1p0,pm2p5,pm4p0,pm10p0,humidity,temperature,vocIndex,noxIndex,co2\n");
  while (f.available()) {
    String line = f.readStringUntil('\n');
    server.sendContent(line + "\n");
  }
  f.close();
}

// DELETE /api/history/clear — erase the log file.
void handleHistoryClear() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!lfsAvailable) {
    server.send(503, "application/json", jsonError("Filesystem unavailable"));
    return;
  }
  if (LittleFS.remove(LOG_FILE)) {
    server.send(200, "application/json", "{\"message\":\"History cleared\"}");
  } else {
    server.send(200, "application/json", "{\"message\":\"No history to clear\"}");
  }
}

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(100); }
  delay(500);

  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
   delay(500);
   Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.println("SSID: " + String(ssid));

  // mDNS — device reachable at http://ambient.local
  if (MDNS.begin("ambient")) {
    Serial.println("mDNS responder started: http://ambient.local/api");
  } else {
    Serial.println("mDNS responder failed to start");
  }

  // Sync time (for api timestamp)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  #if defined(BOARD_AMBIENT_ONE)
    Serial.println("\n=== Ambient Edu — Board: Ambient One ===");
  #elif defined(BOARD_SPARKFUN_C6)
    Serial.println("\n=== Ambient Edu — Board: SparkFun C6 ===");
  #endif

  // ── Power rail (Ambient One only) ─────────────────────────
  #if HAS_POWER_EN
    pinMode(POWER_EN_PIN, OUTPUT);
    digitalWrite(POWER_EN_PIN, HIGH);
    pinMode(POWER_DET_PIN, INPUT_PULLUP);
    Serial.println("Power rail enabled (GPIO 15 = HIGH)");
    delay(100);  // Let rail stabilise
  #endif

  // ── I2C + SEN66 ──────────────────────────────────────────
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.printf("I2C started (SDA=%d, SCL=%d)\n", I2C_SDA, I2C_SCL);

  sensor.begin(Wire, SEN66_I2C_ADDR_6B);

  error = sensor.deviceReset();
  if (error != NO_ERROR) {
    Serial.print("Error during deviceReset(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return;
  }
  delay(1200);

  // Print serial number
  int8_t serialNumber[32] = {0};
  error = sensor.getSerialNumber(serialNumber, 32);
  if (error != NO_ERROR) {
    Serial.print("Error reading serial number: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  } else {
    Serial.print("SEN66 serial: ");
    Serial.println((const char*)serialNumber);
  }

  // Start continuous measurement
  error = sensor.startContinuousMeasurement();
  if (error != NO_ERROR) {
    Serial.print("Error starting measurement: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return;
  }

  Serial.println();

  // ── Battery gauge (optional) ──────────────────────────────
#ifdef ENABLE_BATTERY
  if (!battery.begin()) {
    Serial.println("MAX17048 not found — battery monitoring disabled.");
  } else {
    Serial.printf("Battery: %.0f%%  %.2fV\n", battPercent(), battery.cellVoltage());
  }
#endif

  // ── LittleFS ──────────────────────────────────────────────
  // Try mounting without auto-format first, then reformat explicitly if needed.
  // This is more reliable than formatOnFail=true on ESP32-C6.
  if (LittleFS.begin(false)) {
    lfsAvailable = true;
    Serial.println("LittleFS mounted.");
  } else {
    Serial.println("LittleFS mount failed — formatting...");
    if (LittleFS.format()) {
      if (LittleFS.begin(false)) {
        lfsAvailable = true;
        Serial.println("LittleFS formatted and mounted.");
      } else {
        Serial.println("LittleFS mount failed after format — history disabled.");
      }
    } else {
      Serial.println("LittleFS format failed — history disabled.");
    }
  }

  server.on("/api",               HTTP_GET,    handleApi);
  server.on("/api/history",       HTTP_GET,    handleHistoryJson);
  server.on("/api/history/length", HTTP_GET,  handleHistoryLength);
  server.on("/api/history/csv",   HTTP_GET,    handleHistoryCsv);
  server.on("/api/history/clear", HTTP_DELETE, handleHistoryClear);
  server.begin();
  Serial.print("Web server started on: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/api");
  Serial.print("                   or: http://ambient.local/api");

  Serial.println();

}

// ═════════════════════════════════════════════════════════════
void loop() {

  server.handleClient();

  unsigned long now = millis();
  if (now - lastLogTime >= LOG_INTERVAL_MS) {
    lastLogTime = now;

    float pm1p0 = 0, pm2p5 = 0, pm4p0 = 0, pm10p0 = 0;
    float humidity = 0, temperature = 0;
    float vocIndex = 0, noxIndex = 0;
    uint16_t co2 = 0;

    int16_t logErr = sensor.readMeasuredValues(
        pm1p0, pm2p5, pm4p0, pm10p0,
        humidity, temperature, vocIndex, noxIndex, co2);

    if (logErr == NO_ERROR) {
      time_t t = time(nullptr);
      struct tm* ti = localtime(&t);
      char ts[25];
      strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", ti);
      logReading(ts, pm1p0, pm2p5, pm4p0, pm10p0, humidity, temperature, vocIndex, noxIndex, co2);
    }
  }

  delay(100);  // Short delay keeps server responsive
}
