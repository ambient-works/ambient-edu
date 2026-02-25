/**
 * Ambient Edu — Sensor Read + LED Alert + Web Server + Logging
 *
 * Reads the Sensirion SEN66 every LOG_INTERVAL_MS, serves live data
 * and history over WiFi, and logs to either LittleFS (internal flash)
 * or an SD card — choose with STORAGE_BACKEND below.
 *
 * BOARD SELECTION
 * ───────────────
 * Uncomment ONE of the two #define lines to match your hardware.
 *
 * Requires: - Sensirion I2C SEN66  (Library Manager)
 *           - Sensirion Core        (auto-installed with the above)
 *           - SD                    (built-in, only needed for STORAGE_SD)
 *           - LittleFS              (built-in, only needed for STORAGE_LITTLEFS)
 */

// ═══════════════════════════════════════════════════════════════
// ██  BOARD SELECTION — uncomment exactly ONE line  ████████████
// ═══════════════════════════════════════════════════════════════
// #define BOARD_AMBIENT_ONE         // Ambient One PCB (Rev5)
#define BOARD_SPARKFUN_C6      // SparkFun ESP32-C6 Thing Plus
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// ██  STORAGE BACKEND — uncomment exactly ONE line  ████████████
// ═══════════════════════════════════════════════════════════════
#define STORAGE_LITTLEFS   // Internal flash via LittleFS (no extra hardware)
// #define STORAGE_SD      // External SD card via SPI
// ═══════════════════════════════════════════════════════════════

// ── Board-specific pin mapping ──────────────────────────────
#if defined(BOARD_AMBIENT_ONE)
  #define NEOPIXEL_PIN    23
  #define I2C_SDA         19
  #define I2C_SCL         20
  #define POWER_EN_PIN    15
  #define POWER_DET_PIN    2
  #define HAS_POWER_EN    true
  #define SPI_CS_PIN       5    // SD card CS — adjust if needed

#elif defined(BOARD_SPARKFUN_C6)
  #define NEOPIXEL_PIN    23
  #define I2C_SDA          6
  #define I2C_SCL          7
  #define SPI_CS_PIN      18    // SD card CS
  #define SD_DETECT_PIN   22
  #define MAX17_ALERT_PIN 11
  #define LP_CTRL_PIN     15
  #define HAS_POWER_EN    false

#else
  #error "No board selected! Uncomment BOARD_AMBIENT_ONE or BOARD_SPARKFUN_C6."
#endif

// ── Storage validation ───────────────────────────────────────
#if !defined(STORAGE_LITTLEFS) && !defined(STORAGE_SD)
  #error "No storage backend selected! Uncomment STORAGE_LITTLEFS or STORAGE_SD."
#endif
#if defined(STORAGE_LITTLEFS) && defined(STORAGE_SD)
  #error "Two storage backends selected! Uncomment only ONE of STORAGE_LITTLEFS / STORAGE_SD."
#endif

// ── WiFi credentials ────────────────────────────────────────
const char* ssid     = "wifi-ssid";
const char* password = "wifi-password";

// ── Logging config ──────────────────────────────────────────
#define LOG_FILE        "/airquality.csv"
#define MAX_LOG_LINES   2880        // 48 h at 1-min intervals (LittleFS only)
#define LOG_INTERVAL_MS 60000UL     // Log a reading every 60 s

// ── Optional: Battery Monitor ────────────────────────────────
// Uncomment and install "Adafruit MAX1704X" from Library Manager
// #define ENABLE_BATTERY

// ─────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SensirionI2cSen66.h>

#if defined(STORAGE_LITTLEFS)
  #include <LittleFS.h>
  // Thin wrappers so the rest of the code is storage-agnostic
  #define FS_HANDLE      LittleFS
  #define FILE_APPEND    "a"
  #define FILE_READ_MODE "r"
  #define FILE_WRITE_MODE "w"

#elif defined(STORAGE_SD)
  #include <SPI.h>
  #include <SD.h>
  #define FS_HANDLE      SD
  // SD.h already defines FILE_APPEND, FILE_READ, FILE_WRITE
  #define FILE_READ_MODE  FILE_READ
  #define FILE_WRITE_MODE FILE_WRITE
#endif

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

static char    errorMessage[64];
static int16_t error;
static unsigned long lastLogTime  = 0;
static bool    storageAvailable   = false;

// ═════════════════════════════════════════════════════════════
// Storage helpers — identical interface for both backends
// ═════════════════════════════════════════════════════════════

bool storageExists(const char* path) {
  return FS_HANDLE.exists(path);
}

bool storageRemove(const char* path) {
  return FS_HANDLE.remove(path);
}

// LittleFS-only: trim oldest entries so the log stays ≤ MAX_LOG_LINES.
// SD cards are typically large enough to skip this.
#if defined(STORAGE_LITTLEFS)
void trimLogIfNeeded() {
  if (!storageAvailable) return;
  File f = LittleFS.open(LOG_FILE, FILE_READ_MODE);
  if (!f) return;
  int lineCount = 0;
  while (f.available()) { if (f.read() == '\n') lineCount++; }
  f.close();
  if (lineCount <= MAX_LOG_LINES) return;

  int toSkip = lineCount - MAX_LOG_LINES;
  f = LittleFS.open(LOG_FILE, FILE_READ_MODE);
  if (!f) return;
  File tmp = LittleFS.open("/tmp.csv", FILE_WRITE_MODE);
  if (!tmp) { f.close(); return; }

  int skipped = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (skipped < toSkip) { skipped++; continue; }
    tmp.println(line);
  }
  f.close(); tmp.close();
  LittleFS.remove(LOG_FILE);
  LittleFS.rename("/tmp.csv", LOG_FILE);
}
#else
void trimLogIfNeeded() { /* not needed for SD */ }
#endif

// Append one reading row to the CSV log.
void logReading(const char* timestamp,
                float pm1p0, float pm2p5, float pm4p0, float pm10p0,
                float humidity, float temperature,
                float vocIndex, float noxIndex, uint16_t co2) {
  if (!storageAvailable) return;

  // Write header if file does not yet exist
  bool isNew = !storageExists(LOG_FILE);
  File f = FS_HANDLE.open(LOG_FILE, FILE_APPEND);
  if (!f) { Serial.println("Log: failed to open " LOG_FILE); return; }
  if (isNew) {
    f.println("timestamp,pm1p0_ugm3,pm2p5_ugm3,pm4p0_ugm3,pm10p0_ugm3,"
              "humidity_pct,temp_c,voc_index,nox_index,co2_ppm");
  }
  f.printf("%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u\n",
           timestamp, pm1p0, pm2p5, pm4p0, pm10p0,
           humidity, temperature, vocIndex, noxIndex, co2);
  f.close();

  trimLogIfNeeded();
  Serial.printf("Log: row written to %s\n", LOG_FILE);
}

// ═════════════════════════════════════════════════════════════
// Web API helpers
// ═════════════════════════════════════════════════════════════

String jsonError(const String& message) {
  return "{\"message\":\"" + message + "\"}";
}

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

// ── GET /api — live reading ──────────────────────────────────
void handleApi() {
  float pm1p0 = 0, pm2p5 = 0, pm4p0 = 0, pm10p0 = 0;
  float humidity = 0, temperature = 0;
  float vocIndex = 0, noxIndex = 0;
  uint16_t co2 = 0;

  error = sensor.readMeasuredValues(
      pm1p0, pm2p5, pm4p0, pm10p0,
      humidity, temperature, vocIndex, noxIndex, co2);

  if (error != NO_ERROR) {
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.printf("Read error: %s\n", errorMessage);
    server.send(500, "application/json", jsonError("Unable to read sensor data."));
    return;
  }

  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  char timestamp[25];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", ti);

  String json = "{";
  json += "\"timestamp\":\""  + String(timestamp)       + "\",";
  json += "\"pm1p0\":"        + String(pm1p0, 2)         + ",";
  json += "\"pm2p5\":"        + String(pm2p5, 2)         + ",";
  json += "\"pm4p0\":"        + String(pm4p0, 2)         + ",";
  json += "\"pm10p0\":"       + String(pm10p0, 2)        + ",";
  json += "\"humidity\":"     + String(humidity, 2)      + ",";
  json += "\"temperature\":"  + String(temperature, 2)   + ",";
  json += "\"vocIndex\":"     + String(vocIndex, 2)      + ",";
  json += "\"noxIndex\":"     + String(noxIndex, 2)      + ",";
  json += "\"co2\":"          + String(co2);
#ifdef ENABLE_BATTERY
  json += ",\"battery\":"  + String(battPercent(), 1);
  json += ",\"voltage\":"  + String(battery.cellVoltage(), 2);
  json += ",\"charging\":" + String(battCharging() ? "true" : "false");
#endif
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ── GET /api/history — all stored readings as JSON ──────────
void handleHistoryJson() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!storageAvailable) {
    server.send(503, "application/json", jsonError("Storage unavailable"));
    return;
  }
  if (!storageExists(LOG_FILE)) {
    server.send(200, "application/json", "[]");
    return;
  }
  File f = FS_HANDLE.open(LOG_FILE, FILE_READ_MODE);
  if (!f) { server.send(200, "application/json", "[]"); return; }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");

  bool firstLine = true;
  bool headerSkipped = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    // Skip the header row
    if (!headerSkipped) { headerSkipped = true; continue; }

    if (!firstLine) server.sendContent(",");
    firstLine = false;

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

// ── GET /api/history/length ──────────────────────────────────
void handleHistoryLength() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!storageAvailable) {
    server.send(503, "application/json", jsonError("Storage unavailable"));
    return;
  }
  if (!storageExists(LOG_FILE)) {
    server.send(200, "application/json", "{\"length\":0}");
    return;
  }
  File f = FS_HANDLE.open(LOG_FILE, FILE_READ_MODE);
  if (!f) { server.send(200, "application/json", "{\"length\":0}"); return; }

  int lineCount = 0;
  while (f.available()) { if (f.read() == '\n') lineCount++; }
  f.close();
  // Subtract 1 for the header row
  int dataLines = max(0, lineCount - 1);
  server.send(200, "application/json", "{\"length\":" + String(dataLines) + "}");
}

// ── GET /api/history/csv — download raw CSV ─────────────────
void handleHistoryCsv() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Disposition",
                    "attachment; filename=\"ambient_history.csv\"");
  if (!storageAvailable) {
    server.send(503, "text/plain", "Storage unavailable");
    return;
  }
  if (!storageExists(LOG_FILE)) {
    server.send(404, "text/plain", "No history available");
    return;
  }
  File f = FS_HANDLE.open(LOG_FILE, FILE_READ_MODE);
  if (!f) { server.send(500, "text/plain", "Failed to open log file"); return; }

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  while (f.available()) {
    String line = f.readStringUntil('\n');
    server.sendContent(line + "\n");
  }
  f.close();
}

// ── DELETE /api/history/clear ────────────────────────────────
void handleHistoryClear() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  if (!storageAvailable) {
    server.send(503, "application/json", jsonError("Storage unavailable"));
    return;
  }
  if (storageRemove(LOG_FILE)) {
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

#if defined(BOARD_AMBIENT_ONE)
  Serial.println("\n=== Ambient Edu — Board: Ambient One ===");
#elif defined(BOARD_SPARKFUN_C6)
  Serial.println("\n=== Ambient Edu — Board: SparkFun C6 ===");
#endif

#if defined(STORAGE_LITTLEFS)
  Serial.println("Storage: LittleFS (internal flash)");
#elif defined(STORAGE_SD)
  Serial.println("Storage: SD card (SPI)");
#endif

#if HAS_POWER_EN
  pinMode(POWER_EN_PIN, OUTPUT);
  digitalWrite(POWER_EN_PIN, HIGH);
  pinMode(POWER_DET_PIN, INPUT_PULLUP);
  Serial.println("Power rail enabled (GPIO 15 = HIGH)");
  delay(100);
#endif

  // ── WiFi ──────────────────────────────────────────────────
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi connected — SSID: " + String(ssid));
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  if (MDNS.begin("ambient")) {
    Serial.println("mDNS: http://ambient.local/api");
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // ── I2C + SEN66 ──────────────────────────────────────────
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.printf("I2C started (SDA=%d, SCL=%d)\n", I2C_SDA, I2C_SCL);
  sensor.begin(Wire, SEN66_I2C_ADDR_6B);

  error = sensor.deviceReset();
  if (error != NO_ERROR) {
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.printf("deviceReset() error: %s\n", errorMessage);
    return;
  }
  delay(1200);

  int8_t serialNumber[32] = {0};
  if (sensor.getSerialNumber(serialNumber, 32) == NO_ERROR) {
    Serial.printf("SEN66 serial: %s\n", (const char*)serialNumber);
  }

  error = sensor.startContinuousMeasurement();
  if (error != NO_ERROR) {
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.printf("startContinuousMeasurement() error: %s\n", errorMessage);
    return;
  }

  // ── Battery gauge ─────────────────────────────────────────
#ifdef ENABLE_BATTERY
  if (!battery.begin()) {
    Serial.println("MAX17048 not found — battery monitoring disabled.");
  } else {
    Serial.printf("Battery: %.0f%%  %.2fV\n", battPercent(), battery.cellVoltage());
  }
#endif

  // ── Storage init ──────────────────────────────────────────
#if defined(STORAGE_LITTLEFS)
  if (LittleFS.begin(false)) {
    storageAvailable = true;
    Serial.println("LittleFS mounted.");
  } else {
    Serial.println("LittleFS mount failed — formatting...");
    if (LittleFS.format() && LittleFS.begin(false)) {
      storageAvailable = true;
      Serial.println("LittleFS formatted and mounted.");
    } else {
      Serial.println("LittleFS unavailable — history disabled.");
    }
  }

#elif defined(STORAGE_SD)
  Serial.printf("Initialising SD on CS=GPIO %d ... ", SPI_CS_PIN);
  if (SD.begin(SPI_CS_PIN)) {
    storageAvailable = true;
    Serial.println("OK");
  } else {
    Serial.println("FAILED — check card & wiring. History disabled.");
  }
#endif

  // ── Web server routes ─────────────────────────────────────
  server.on("/api",                HTTP_GET,    handleApi);
  server.on("/api/history",        HTTP_GET,    handleHistoryJson);
  server.on("/api/history/length", HTTP_GET,    handleHistoryLength);
  server.on("/api/history/csv",    HTTP_GET,    handleHistoryCsv);
  server.on("/api/history/clear",  HTTP_DELETE, handleHistoryClear);
  server.begin();
  Serial.printf("Web server running at http://%s/api\n",
                WiFi.localIP().toString().c_str());

  // Trigger first log immediately
  lastLogTime = millis() - LOG_INTERVAL_MS;
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
      logReading(ts, pm1p0, pm2p5, pm4p0, pm10p0,
                 humidity, temperature, vocIndex, noxIndex, co2);
    } else {
      errorToString(logErr, errorMessage, sizeof errorMessage);
      Serial.printf("Log read error: %s\n", errorMessage);
    }
  }

  delay(100);
}
