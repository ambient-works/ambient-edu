/**
 * Ambient Edu — Data Logger (LittleFS)
 * 
 * Reads the SEN66 sensor every 10 seconds and logs readings to a CSV file
 * stored on the ESP32's internal flash filesystem (LittleFS).
 * 
 * The LED blinks blue during each write. On startup the sketch prints any
 * previously logged data to Serial so you can copy/paste it into a
 * spreadsheet without needing an SD card.
 * 
 * BOARD SELECTION
 * ───────────────
 * Uncomment ONE of the two #define lines below to match your hardware.
 * 
 * Board:    ESP32-C6 (select "ESP32C6 Dev Module" in Arduino IDE)
 * Requires: - Adafruit NeoPixel   (Library Manager)
 *           - Sensirion I2C SEN66 (Library Manager)
 *           - LittleFS            (built-in, no install needed)
 */

// ═══════════════════════════════════════════════════════════════
// ██  BOARD SELECTION — uncomment exactly ONE line  ████████████
// ═══════════════════════════════════════════════════════════════
// #define BOARD_AMBIENT_ONE         // Ambient One PCB (Rev5)
#define BOARD_SPARKFUN_C6         // SparkFun ESP32-C6 Thing Plus
// ═══════════════════════════════════════════════════════════════

// ── Board-specific pin mapping ──────────────────────────────
#if defined(BOARD_AMBIENT_ONE)
  #define NEOPIXEL_PIN    23
  #define I2C_SDA         19
  #define I2C_SCL         20
  #define POWER_EN_PIN    15
  #define POWER_DET_PIN    2
  #define HAS_POWER_EN    true

#elif defined(BOARD_SPARKFUN_C6)
  #define NEOPIXEL_PIN    23
  #define I2C_SDA          6
  #define I2C_SCL          7
  #define HAS_POWER_EN    false

#else
  #error "No board selected! Uncomment BOARD_AMBIENT_ONE or BOARD_SPARKFUN_C6."
#endif

// ── Logging config (edit these!) ────────────────────────────
#define LOG_INTERVAL_MS   10000         // How often to log (10 seconds)
#define LOG_FILE          "/data.csv"   // File path on flash
#define MAX_READINGS      500           // Stop logging after this many rows
                                        // (prevents filling up flash)

// ─────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <SensirionI2cSen66.h>
#include <LittleFS.h>

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

// ── Globals ─────────────────────────────────────────────────
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
SensirionI2cSen66 sensor;

static char errorMessage[64];
static int16_t error;
unsigned long lastLogTime = 0;
int readingCount = 0;

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(100); }
  delay(500);

  Serial.println("\n=== Ambient Edu — Data Logger ===");

  // ── Power rail (Ambient One only) ─────────────────────────
#if HAS_POWER_EN
  pinMode(POWER_EN_PIN, OUTPUT);
  digitalWrite(POWER_EN_PIN, HIGH);
  pinMode(POWER_DET_PIN, INPUT_PULLUP);
  Serial.println("Power rail enabled");
  delay(100);
#endif

  // ── NeoPixel ──────────────────────────────────────────────
  pixel.begin();
  pixel.setBrightness(30);
  pixel.clear();
  pixel.show();

  // ── LittleFS ──────────────────────────────────────────────
  if (!LittleFS.begin(true)) {  // true = format on first use
    Serial.println("ERROR: LittleFS mount failed!");
    while (true) { delay(1000); }
  }
  Serial.println("LittleFS mounted");

  // Print any existing data from a previous run
  printExistingData();

  // ── I2C + SEN66 ──────────────────────────────────────────
  Wire.begin(I2C_SDA, I2C_SCL);
  sensor.begin(Wire, SEN66_I2C_ADDR_6B);

  error = sensor.deviceReset();
  if (error != NO_ERROR) {
    Serial.print("Sensor reset error: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return;
  }
  delay(1200);

  error = sensor.startContinuousMeasurement();
  if (error != NO_ERROR) {
    Serial.print("Sensor start error: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return;
  }

  // Write CSV header if file is new
  if (!LittleFS.exists(LOG_FILE)) {
    File file = LittleFS.open(LOG_FILE, "w");
    if (file) {
      file.println("seconds,pm1_0,pm2_5,pm4_0,pm10,co2,voc,nox,temp_c,humidity");
      file.close();
      Serial.println("Created new log file with header");
    }
  }

  Serial.println("\nLogging started. Open Serial Monitor to watch readings.");
  Serial.printf("Interval: %d seconds | Max readings: %d\n", LOG_INTERVAL_MS / 1000, MAX_READINGS);
  Serial.println("Type 'print' to dump CSV data");
  Serial.println("Type 'clear' to delete the log file\n");
}

// ═════════════════════════════════════════════════════════════
void loop() {
  // Check for serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "print") {
      printExistingData();
    } else if (cmd == "clear") {
      LittleFS.remove(LOG_FILE);
      readingCount = 0;
      Serial.println("Log file deleted.");
    }
  }

  // Time to log?
  if (millis() - lastLogTime < LOG_INTERVAL_MS) return;
  lastLogTime = millis();

  // Check limit
  if (readingCount >= MAX_READINGS) {
    Serial.println("Max readings reached. Type 'clear' to reset.");
    return;
  }

  // Read sensor
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
    return;
  }

  // Build CSV line
  unsigned long seconds = millis() / 1000;
  char line[128];
  snprintf(line, sizeof(line), "%lu,%.1f,%.1f,%.1f,%.1f,%u,%.0f,%.0f,%.1f,%.1f",
           seconds, pm1p0, pm2p5, pm4p0, pm10p0, co2,
           vocIndex, noxIndex, temperature, humidity);

  // Write to flash
  File file = LittleFS.open(LOG_FILE, "a");  // "a" = append
  if (file) {
    file.println(line);
    file.close();
    readingCount++;

    // Flash LED blue to show write happened
    pixel.setPixelColor(0, pixel.Color(0, 0, 255));
    pixel.show();

    Serial.printf("[%d] %s\n", readingCount, line);

    delay(100);
    pixel.clear();
    pixel.show();
  } else {
    Serial.println("ERROR: Could not open log file for writing");
  }
}

// ─────────────────────────────────────────────────────────────
// Print existing CSV data (copy/paste into a spreadsheet)
// ─────────────────────────────────────────────────────────────
void printExistingData() {
  if (!LittleFS.exists(LOG_FILE)) {
    Serial.println("No existing log data found.");
    return;
  }

  File file = LittleFS.open(LOG_FILE, "r");
  if (!file) {
    Serial.println("ERROR: Could not open log file for reading");
    return;
  }

  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║    SAVED DATA — copy from here       ║");
  Serial.println("╚══════════════════════════════════════╝");

  while (file.available()) {
    Serial.println(file.readStringUntil('\n'));
  }
  file.close();

  Serial.println("───── end of data ─────\n");
}
