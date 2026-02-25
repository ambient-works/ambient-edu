/**
 * Ambient Edu — Sensor Read + LED Alert + SD Card Logging
 * 
 * Reads all measurements from the Sensirion SEN66 air quality sensor and
 * prints them to Serial every second. A simple if/else block sets the
 * NeoPixel LED colour based on CO2. Data is also logged to SD card as
 * a .csv file every 10 seconds.
 * 
 * BOARD SELECTION
 * ───────────────
 * Uncomment ONE of the two #define lines below to match your hardware.
 * 
 * Board:    ESP32-C6 (select "ESP32C6 Dev Module" in Arduino IDE)
 * Requires: - Adafruit NeoPixel   (Library Manager)
 *           - Sensirion I2C SEN66 (Library Manager or manual install)
 *           - Sensirion Core      (installed automatically with the above)
 *           - SD (built-in Arduino library)
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
  #define SPI_CS_PIN       5    // SD card chip select — adjust if needed

#elif defined(BOARD_SPARKFUN_C6)
  // SparkFun ESP32-C6 Thing Plus
  #define NEOPIXEL_PIN    23    // WS2812 status LED data in
  #define I2C_SDA          6    // Default Qwiic / I2C SDA
  #define I2C_SCL          7    // Default Qwiic / I2C SCL
  #define SPI_CS_PIN      18    // SPI chip select (SD card)
  #define SD_DETECT_PIN   22    // SD card detect
  #define MAX17_ALERT_PIN 11    // MAX17038 fuel gauge alert
  #define LP_CTRL_PIN     15    // Low-power control
  #define HAS_POWER_EN    false // No switched power rail on this board

#else
  #error "No board selected! Uncomment BOARD_AMBIENT_ONE or BOARD_SPARKFUN_C6 at the top of this file."
#endif

// ── Common config ───────────────────────────────────────────
#define NUM_PIXELS       1      // Single onboard LED
#define LED_BRIGHTNESS  50      // 0–255  (keep moderate to avoid glare)

// ── SD / Logging config ─────────────────────────────────────
#define LOG_INTERVAL_MS  10000  // Log to SD every 10 seconds
#define LOG_FILENAME     "/airquality.csv"

// ─────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_NeoPixel.h>
#include <SensirionI2cSen66.h>

// ── Optional: Battery Monitor (SparkFun C6 has MAX17048) ────
// Uncomment the next line and install "Adafruit MAX1704X" from Library Manager
#define ENABLE_BATTERY
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
Adafruit_NeoPixel pixel(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
SensirionI2cSen66 sensor;

static char errorMessage[64];
static int16_t error;

bool sdAvailable = false;           // Set true if SD card mounts OK
unsigned long lastLogTime = 0;      // Tracks when we last wrote to SD
unsigned long loopCount   = 0;      // Used to derive a simple elapsed timestamp

// ── Write CSV header if the file is brand new ───────────────
void initCSV() {
  // Only write the header if the file doesn't already exist
  if (!SD.exists(LOG_FILENAME)) {
    File f = SD.open(LOG_FILENAME, FILE_WRITE);
    if (f) {
      f.println("elapsed_s,pm1p0_ugm3,pm2p5_ugm3,pm4p0_ugm3,pm10p0_ugm3,"
                "co2_ppm,voc_index,nox_index,temp_c,rh_pct"
#ifdef ENABLE_BATTERY
                ",batt_pct,batt_v"
#endif
                );
      f.close();
      Serial.println("SD: CSV header written → " LOG_FILENAME);
    } else {
      Serial.println("SD: Failed to create " LOG_FILENAME);
    }
  } else {
    Serial.println("SD: Appending to existing " LOG_FILENAME);
  }
}

// ── Append one data row to the CSV ──────────────────────────
void logToSD(unsigned long elapsedSec,
             float pm1p0, float pm2p5, float pm4p0, float pm10p0,
             uint16_t co2, float vocIndex, float noxIndex,
             float temperature, float humidity
#ifdef ENABLE_BATTERY
             , float battPct, float battV
#endif
             ) {
  File f = SD.open(LOG_FILENAME, FILE_APPEND);
  if (!f) {
    Serial.println("SD: Could not open file for append!");
    return;
  }

  // Build the CSV row
  f.print(elapsedSec);    f.print(',');
  f.print(pm1p0,  1);     f.print(',');
  f.print(pm2p5,  1);     f.print(',');
  f.print(pm4p0,  1);     f.print(',');
  f.print(pm10p0, 1);     f.print(',');
  f.print(co2);           f.print(',');
  f.print(vocIndex,  0);  f.print(',');
  f.print(noxIndex,  0);  f.print(',');
  f.print(temperature, 1); f.print(',');
  f.print(humidity,    1);
#ifdef ENABLE_BATTERY
  f.print(',');
  f.print(battPct, 0);  f.print(',');
  f.print(battV,   2);
#endif
  f.println();
  f.close();

  Serial.printf("SD: Row logged at %lu s → " LOG_FILENAME "\n", elapsedSec);
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

  // ── Power rail (Ambient One only) ─────────────────────────
#if HAS_POWER_EN
  pinMode(POWER_EN_PIN, OUTPUT);
  digitalWrite(POWER_EN_PIN, HIGH);
  pinMode(POWER_DET_PIN, INPUT_PULLUP);
  Serial.println("Power rail enabled (GPIO 15 = HIGH)");
  delay(100);
#endif

  // ── NeoPixel ──────────────────────────────────────────────
  pixel.begin();
  pixel.setBrightness(LED_BRIGHTNESS);
  pixel.clear();
  pixel.show();
  Serial.printf("NeoPixel ready on GPIO %d\n", NEOPIXEL_PIN);

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

  // ── SD card ───────────────────────────────────────────────
  Serial.printf("Initialising SD card on CS=GPIO %d … ", SPI_CS_PIN);
  if (SD.begin(SPI_CS_PIN)) {
    Serial.println("OK");
    sdAvailable = true;
    initCSV();
  } else {
    Serial.println("FAILED — check card & wiring. Logging disabled.");
  }

  // ── Battery gauge (optional) ──────────────────────────────
#ifdef ENABLE_BATTERY
  if (!battery.begin()) {
    Serial.println("MAX17048 not found — battery monitoring disabled.");
  } else {
    Serial.printf("Battery: %.0f%%  %.2fV\n", battPercent(), battery.cellVoltage());
  }
#endif

  Serial.println("\nSensor started — readings below.");
  Serial.println("NOTE: VOC/NOx need ~60 s to warm up.\n");

  lastLogTime = millis();
}

// ═════════════════════════════════════════════════════════════
void loop() {
  float pm1p0 = 0, pm2p5 = 0, pm4p0 = 0, pm10p0 = 0;
  float humidity = 0, temperature = 0;
  float vocIndex = 0, noxIndex = 0;
  uint16_t co2 = 0;

  delay(1000);  // SEN66 updates roughly every second
  loopCount++;

  error = sensor.readMeasuredValues(
      pm1p0, pm2p5, pm4p0, pm10p0,
      humidity, temperature, vocIndex, noxIndex, co2);

  if (error != NO_ERROR) {
    Serial.print("Read error: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return;
  }

  // ── Print readings ────────────────────────────────────────
  Serial.println("────────────────────────────────────");
  Serial.printf("  PM1.0:  %.1f µg/m³\n", pm1p0);
  Serial.printf("  PM2.5:  %.1f µg/m³\n", pm2p5);
  Serial.printf("  PM4.0:  %.1f µg/m³\n", pm4p0);
  Serial.printf("  PM10:   %.1f µg/m³\n", pm10p0);
  Serial.printf("  CO2:    %u ppm\n",      co2);
  Serial.printf("  VOC:    %.0f (index)\n", vocIndex);
  Serial.printf("  NOx:    %.0f (index)\n", noxIndex);
  Serial.printf("  Temp:   %.1f °C\n",     temperature);
  Serial.printf("  RH:     %.1f %%\n",     humidity);
#ifdef ENABLE_BATTERY
  Serial.printf("  Batt:   %.0f%% (%.2fV) %s\n",
                battPercent(), battery.cellVoltage(),
                battCharging() ? "[charging]" : "[discharging]");
#endif

  // ── LED colour based on CO2 ───────────────────────────────
  if (co2 < 1000) {
    pixel.setPixelColor(0, pixel.Color(0, 255, 0));
    Serial.println("  LED -> GREEN (Good)");
  } else if (co2 < 1500) {
    pixel.setPixelColor(0, pixel.Color(255, 180, 0));
    Serial.println("  LED -> YELLOW (Moderate)");
  } else if (co2 < 5000) {
    pixel.setPixelColor(0, pixel.Color(255, 80, 0));
    Serial.println("  LED -> ORANGE (Poor)");
  } else {
    pixel.setPixelColor(0, pixel.Color(255, 0, 0));
    Serial.println("  LED -> RED (Severe)");
  }
  pixel.show();

  // ── SD card logging every LOG_INTERVAL_MS ─────────────────
  if (sdAvailable && (millis() - lastLogTime >= LOG_INTERVAL_MS)) {
    lastLogTime = millis();
    unsigned long elapsedSec = loopCount;  // ~1 s per loop

    logToSD(elapsedSec,
            pm1p0, pm2p5, pm4p0, pm10p0,
            co2, vocIndex, noxIndex,
            temperature, humidity
#ifdef ENABLE_BATTERY
            , battPercent(), battery.cellVoltage()
#endif
            );
  }

  Serial.println();
}
