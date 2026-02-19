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
// ── WiFi Credentials ────────────────────────────────────────
const char* ssid = "TP-Link_8FBA";
const char* password = "61780316";

// ─────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2cSen66.h>

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

// ── Globals ─────────────────────────────────────────────────
SensirionI2cSen66 sensor;
WebServer server(80);

static char errorMessage[64];
static int16_t error;

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
  json += "}";

  // Send data to endpoint
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);

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

  server.begin();
  Serial.print("Web server started on: http://");
  Serial.print(WiFi.localIP());
  Serial.print("/api");

  Serial.println();

}

// ═════════════════════════════════════════════════════════════
void loop() {

  server.handleClient();

  delay(1000);  // SEN66 updates roughly every second

  server.on("/api", HTTP_GET, handleApi);
}
