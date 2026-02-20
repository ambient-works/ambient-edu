/**
 * Ambient Edu — Supabase Data Logger
 * 
 * Reads all measurements from the Sensirion SEN66 air quality sensor every
 * minute and pushes them to a Supabase database via the REST API.
 * Device presence is tracked via an upsert on the `devices` table.
 * 
 * BOARD SELECTION
 * ───────────────
 * Uncomment ONE of the two #define lines below to match your hardware.
 * This controls pin assignments and whether the power-enable rail is used.
 * 
 * Board:    ESP32-C6 (select "ESP32C6 Dev Module" in Arduino IDE)
 * Requires: - Sensirion I2C SEN66 (install manually or from Library Manager)
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
  #define I2C_SDA         19
  #define I2C_SCL         20
  #define POWER_EN_PIN    15    // Switched power rail — must be HIGH for sensor
  #define POWER_DET_PIN    2    // Power-button detect (input, not required here)
  #define HAS_POWER_EN    true  // This board needs the power rail enabled

#elif defined(BOARD_SPARKFUN_C6)
  // SparkFun ESP32-C6 Thing Plus
  #define I2C_SDA          6    // Default Qwiic / I2C SDA
  #define I2C_SCL          7    // Default Qwiic / I2C SCL
  #define HAS_POWER_EN    false // No switched power rail on this board

#else
  #error "No board selected! Uncomment BOARD_AMBIENT_ONE or BOARD_SPARKFUN_C6 at the top of this file."
#endif

// ── WiFi Credentials ────────────────────────────────────────
const char* ssid     = "wifi-ssid";
const char* password = "wifi-password";

// –– Student Config ––––––––––––––––––––––––––––––––––––––––––
const char* STUDENT_NAME  = "";
const char* STUDENT_EMAIL = "";

const float LOCATION_LATITUDE    = .0;
const float LOCATION_LONGITUDE   = .0;
const char* LOCATION_DESCRIPTION = "";

// ── Supabase Config ─────────────────────────────────────────
const char* SUPABASE_URL      = "https://<project-ref>.supabase.co";
const char* SUPABASE_ANON_KEY = "<anon-key>";

// ── Device Config ––─────────────────────────────────────────
static char DEVICE_ID[18];
const char* KIT_NUMBER = "amb-1337";

// ── Push interval ────────────────────────────────────────────
#define PUSH_INTERVAL_MS 60000UL  // Push a reading every 60 s

// ─────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <ESPSupabase.h>
#include <SensirionI2cSen66.h>

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

// ── Globals ─────────────────────────────────────────────────
SensirionI2cSen66 sensor;
Supabase db;

static char errorMessage[64];
static int16_t error;
static unsigned long lastPushTime = 0;

// ── Supabase helpers ─────────────────────────────────────────

static bool deviceRegistered = false;
static String deviceUuid = "";

String getDeviceUUID() {
  String data = db.from("devices").select("id").eq("device_id", String(DEVICE_ID)).eq("student_name", String(STUDENT_NAME)).limit(1).doSelect();
  data.replace("[{\"id\":\"", "");
  data.replace("\"}]", "");
  Serial.println("Device UUID: " + data + " (unique to device_id + student_name)");
  deviceUuid = data;
  deviceRegistered = true;
  db.urlQuery_reset();
  return data;
}

void updateDevice(const char* timestamp) {
  if (deviceRegistered) return;
  if (deviceUuid.length() == 0) return;

  String json = "{";
  json += "\"device_id\":\""            + String(DEVICE_ID)            + "\",";
  json += "\"student_name\":\""         + String(STUDENT_NAME)         + "\",";
  json += "\"student_email\":\""        + String(STUDENT_EMAIL)        + "\",";
  json += "\"kit_number\":\""           + String(KIT_NUMBER)           + "\",";
  json += "\"latitude\":"               + String(LOCATION_LATITUDE)    + ",";
  json += "\"longitude\":"              + String(LOCATION_LONGITUDE)   + ",";
  json += "\"location_description\":\"" + String(LOCATION_DESCRIPTION) + "\",";
  json += "\"last_seen\":\""            + String(timestamp)            + "\"";
  json += "}";

  int code = db.insert("devices", json, false);
  if (code == 201) {
    Serial.println("Device registered.");
    deviceRegistered = true;
  } else if (code == 409) {  // HTTP: Unique conflict
    Serial.println("Device already registered.");
    deviceRegistered = true;
  } else {
    Serial.printf("Device registration failed, HTTP %d (will retry next cycle)\n", code);
  }
  db.urlQuery_reset();
}

// Insert one sensor reading into sensor_readings.
void pushReading(const char* timestamp,
                 float pm1p0, float pm2p5, float pm4p0, float pm10p0,
                 float humidity, float temperature,
                 float vocIndex, float noxIndex, uint16_t co2) {
  String json = "{";
  json += "\"device_id\":\""   + String(DEVICE_ID)      + "\",";
  json += "\"device_uuid\":\"" + String(deviceUuid)     + "\",";
  json += "\"timestamp\":\""   + String(timestamp)      + "\",";
  json += "\"pm1_0\":"         + String(pm1p0, 2)       + ",";
  json += "\"pm2_5\":"         + String(pm2p5, 2)       + ",";
  json += "\"pm4_0\":"         + String(pm4p0, 2)       + ",";
  json += "\"pm10_0\":"        + String(pm10p0, 2)      + ",";
  json += "\"temperature\":"   + String(temperature, 2) + ",";
  json += "\"humidity\":"      + String(humidity, 2)    + ",";
  json += "\"co2\":"           + String(co2)            + ",";
  json += "\"nox_index\":"     + String(noxIndex, 2)    + ",";
  json += "\"voc_index\":"     + String(vocIndex, 2);
  json += "}";
  int code = db.insert("sensor_readings", json, false);

  db.urlQuery_reset();
  if (code < 200 || code >= 300) {
    Serial.printf("sensor_readings insert failed, HTTP %d\n", code);
  }
}

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(100); }                                                                     
  delay(500);


  if (strlen(STUDENT_NAME) == 0) {
    Serial.println("Student name needed, sleeping. Please edit the sketch and set STUDENT_NAME and STUDENT_EMAIL at the top of the file.");
    vTaskDelete(NULL);
  } else {
    Serial.printf("Student: %s <%s>\n", STUDENT_NAME, STUDENT_EMAIL);
  }

  if (SUPABASE_URL == "https://<your-project-ref>.supabase.co" || SUPABASE_ANON_KEY == "<your-anon-key>") {
    Serial.println("Supabase config missing, sleeping. Please edit the sketch and set SUPABASE_URL and SUPABASE_ANON_KEY at the top of the file.");
    vTaskDelete(NULL);
  } else {
    Serial.printf("Supabase url: %s \n", SUPABASE_URL);
  }

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


  Serial.println("");
  Serial.println("=====================================================================");
  Serial.println("                 _     _            _                      _        ");
  Serial.println("                | |   (_)          | |                    | |       ");
  Serial.println("  __ _ _ __ ___ | |__  _  ___ _ __ | |___      _____  _ __| | _____ ");
  Serial.println(" / _` | '_ ` _ \\| '_ \\| |/ _ \\ '_ \\| __\\ \\ /\\ / / _ \\| '__| |/ / __|");
  Serial.println("| (_| | | | | | | |_) | |  __/ | | | |_ \\ V  V / (_) | |  |   <\\__ \\");
  Serial.println(" \\__,_|_| |_| |_|_.__/|_|\\___|_| |_|\\__| \\_/\\_/ \\___/|_|  |_|\\_\\___/");
  Serial.println("");
  Serial.println("=====================================================================");
  Serial.println("");

  // Start continuous measurement
  error = sensor.startContinuousMeasurement();
  if (error != NO_ERROR) {
    Serial.print("Error starting measurement: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return;
  }

  // ── WiFi ─────────────────────────────────────────────────
  WiFi.setSleep(false);  // Disable modem sleep — prevents TCP timeouts on first connect
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  db.begin(SUPABASE_URL, SUPABASE_ANON_KEY);

  // ── Device ID from MAC address ──────────────────────────
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(DEVICE_ID, sizeof(DEVICE_ID),
           "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("Device ID (MAC): %s\n", DEVICE_ID);

  // ── NTP time sync ────────────────────────────────────────
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Syncing time");
  time_t now = time(nullptr);
  while (now < 1000000000UL) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println(" done.");

  getDeviceUUID();

  delay(2000);

  readAndPush();

}

void readAndPush() {

  float pm1p0 = 0, pm2p5 = 0, pm4p0 = 0, pm10p0 = 0;
  float humidity = 0, temperature = 0;
  float vocIndex = 0, noxIndex = 0;
  uint16_t co2 = 0;

  int16_t err = sensor.readMeasuredValues(
      pm1p0, pm2p5, pm4p0, pm10p0,
      humidity, temperature, vocIndex, noxIndex, co2);

  if (err != NO_ERROR) {
    errorToString(err, errorMessage, sizeof errorMessage);
    Serial.printf("Sensor read error: %s\n", errorMessage);
    return;
  }

  time_t t = time(nullptr);
  struct tm* ti = localtime(&t);
  char ts[25];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", ti);

  Serial.printf("[%s] PM2.5=%.2f CO2=%u T=%.1f H=%.1f\n",
                ts, pm2p5, co2, temperature, humidity);

  updateDevice(ts);
  pushReading(ts, pm1p0, pm2p5, pm4p0, pm10p0,
              humidity, temperature, vocIndex, noxIndex, co2);
}

// ═════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();
  if (now - lastPushTime >= PUSH_INTERVAL_MS) {
    lastPushTime = now;

    readAndPush();
  }

  delay(100);
}