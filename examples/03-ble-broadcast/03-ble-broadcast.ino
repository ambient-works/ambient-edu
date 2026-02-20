/**
 * Ambient Edu — BLE Broadcast
 *
 * Reads the SEN66 sensor and broadcasts every measurement over
 * Bluetooth Low Energy (BLE).  Any BLE scanner app — such as
 * nRF Connect or LightBlue — can see the values immediately,
 * no custom app required.
 *
 * DEVICE ID
 * ─────────
 * Change DEVICE_ID below (1–99) so each student's board gets a
 * unique BLE name: "Ambient Edu 01", "Ambient Edu 02", etc.
 * The onboard LED also flashes a unique colour on startup so you
 * can tell boards apart without a phone.
 *
 * HOW IT WORKS
 * ────────────
 * The board advertises with a single BLE Service containing three
 * grouped characteristics — one for particles, one for gases, and
 * one for environment.  Each characteristic packs related values
 * into a single byte payload (little-endian).
 *
 * BUILDING YOUR OWN APP
 * ─────────────────────
 * 1. Scan for a device named "Ambient Edu XX"
 * 2. Connect and discover the service UUID below
 * 3. Subscribe to NOTIFY on the characteristic(s) you care about
 * 4. Decode the byte payload — see the map below
 *
 * CHARACTERISTIC MAP
 * ──────────────────
 *
 *   Particles  (UUID ...0010)   16 bytes
 *   ┌────────────┬────────────┬────────────┬────────────┐
 *   │ PM1.0 (f32)│ PM2.5 (f32)│ PM4.0 (f32)│ PM10  (f32)│   all µg/m³
 *   └────────────┴────────────┴────────────┴────────────┘
 *
 *   Gases      (UUID ...0011)   10 bytes
 *   ┌────────────┬────────────┬────────────┐
 *   │ CO2  (u16) │ VOC   (f32)│ NOx   (f32)│   ppm / index
 *   └────────────┴────────────┴────────────┘
 *
 *   Environment (UUID ...0012)    8 bytes
 *   ┌────────────┬────────────┐
 *   │ Temp  (f32)│ Humi  (f32)│   °C / %RH
 *   └────────────┴────────────┘
 *
 *   Battery (UUID ...0013, optional)   8 bytes
 *   ┌────────────┬────────────┐
 *   │ SoC   (f32)│ Volts (f32)│   % / V
 *   └────────────┴────────────┘
 *
 *   f32 = 4-byte IEEE 754 float, little-endian
 *   u16 = 2-byte unsigned integer, little-endian
 *
 * BOARD SELECTION
 * ───────────────
 * Uncomment ONE of the two #define lines below to match your hardware.
 *
 * Board:    ESP32-C6 (select "ESP32C6 Dev Module" in Arduino IDE)
 * Requires: - Adafruit NeoPixel   (Library Manager)
 *           - Sensirion I2C SEN66 (Library Manager)
 *           - Sensirion Core      (installed automatically with the above)
 */

// ═══════════════════════════════════════════════════════════════
// ██  DEVICE ID — give each board a unique number (1–99)  ██████
// ═══════════════════════════════════════════════════════════════
#define DEVICE_ID  1
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// ██  BOARD SELECTION — uncomment exactly ONE line  ████████████
// ═══════════════════════════════════════════════════════════════
// #define BOARD_AMBIENT_ONE         // Ambient One PCB (Rev5)
#define BOARD_SPARKFUN_C6      // SparkFun ESP32-C6 Thing Plus
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

// ── Common config ───────────────────────────────────────────
#define NUM_PIXELS       1
#define LED_BRIGHTNESS  50

// ─────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <SensirionI2cSen66.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

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

// ── BLE UUIDs ───────────────────────────────────────────────
#define SERVICE_UUID     "12340001-1234-1234-1234-123456789abc"
#define CHAR_PART_UUID   "12340010-1234-1234-1234-123456789abc"  // Particles
#define CHAR_GAS_UUID    "12340011-1234-1234-1234-123456789abc"  // Gases
#define CHAR_ENV_UUID    "12340012-1234-1234-1234-123456789abc"  // Environment
#define CHAR_BATT_UUID   "12340013-1234-1234-1234-123456789abc"  // Battery (opt)

// ── Globals ─────────────────────────────────────────────────
Adafruit_NeoPixel pixel(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
SensirionI2cSen66 sensor;

static char errorMessage[64];
static int16_t error;

bool deviceConnected    = false;
bool oldDeviceConnected = false;

BLECharacteristic *charParticles;
BLECharacteristic *charGases;
BLECharacteristic *charEnv;
#ifdef ENABLE_BATTERY
BLECharacteristic *charBattery;
#endif

// ── BLE callbacks ───────────────────────────────────────────
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s)    { deviceConnected = true;  Serial.println("BLE client connected"); }
  void onDisconnect(BLEServer* s) { deviceConnected = false; Serial.println("BLE client disconnected"); }
};

// ── Helpers ─────────────────────────────────────────────────
BLECharacteristic* addChar(BLEService* svc, const char* uuid) {
  BLECharacteristic* c = svc->createCharacteristic(
      uuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  c->addDescriptor(new BLE2902());
  return c;
}

// Return a deterministic colour from the device ID (hue wheel).
uint32_t idColour(uint8_t id) {
  uint16_t hue = ((uint16_t)(id - 1) * 9830) % 65536;  // spread evenly
  return Adafruit_NeoPixel::ColorHSV(hue, 255, 200);
}

// Flash the LED so the student can visually identify their board.
void flashId(uint8_t id) {
  uint32_t c = idColour(id);
  for (int i = 0; i < 5; i++) {
    pixel.setPixelColor(0, c);
    pixel.show();
    delay(200);
    pixel.clear();
    pixel.show();
    delay(200);
  }
  // Leave LED on solid at low brightness as a steady indicator
  pixel.setPixelColor(0, c);
  pixel.show();
}

// Build the BLE device name from DEVICE_ID (e.g. "Ambient Edu 03").
String buildName() {
  char buf[20];
  snprintf(buf, sizeof(buf), "Ambient Edu %02d", DEVICE_ID);
  return String(buf);
}

// ═════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(100); }
  delay(500);

  String bleName = buildName();

#if defined(BOARD_AMBIENT_ONE)
  Serial.println("\n=== Ambient Edu BLE — Board: Ambient One ===");
#elif defined(BOARD_SPARKFUN_C6)
  Serial.println("\n=== Ambient Edu BLE — Board: SparkFun C6 ===");
#endif
  Serial.printf("Device ID: %d  →  BLE name: \"%s\"\n", DEVICE_ID, bleName.c_str());

  // ── Power rail (Ambient One only) ─────────────────────────
#if HAS_POWER_EN
  pinMode(POWER_EN_PIN, OUTPUT);
  digitalWrite(POWER_EN_PIN, HIGH);
  pinMode(POWER_DET_PIN, INPUT_PULLUP);
  Serial.println("Power rail enabled (GPIO 15 = HIGH)");
  delay(100);
#endif

  // ── NeoPixel — flash the device's ID colour ──────────────
  pixel.begin();
  pixel.setBrightness(LED_BRIGHTNESS);
  pixel.clear();
  pixel.show();
  flashId(DEVICE_ID);

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

  int8_t serialNumber[32] = {0};
  error = sensor.getSerialNumber(serialNumber, 32);
  if (error == NO_ERROR) {
    Serial.print("SEN66 serial: ");
    Serial.println((const char*)serialNumber);
  }

  error = sensor.startContinuousMeasurement();
  if (error != NO_ERROR) {
    Serial.print("Error starting measurement: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return;
  }

  // ── Battery gauge (optional) ──────────────────────────────
#ifdef ENABLE_BATTERY
  if (!battery.begin()) {
    Serial.println("MAX17048 not found — battery monitoring disabled.");
  } else {
    Serial.printf("Battery: %.0f%%  %.2fV\n", battPercent(), battery.cellVoltage());
  }
#endif

  // ── BLE ───────────────────────────────────────────────────
  BLEDevice::init(bleName.c_str());
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* svc = pServer->createService(BLEUUID(SERVICE_UUID), 20);

  charParticles = addChar(svc, CHAR_PART_UUID);
  charGases     = addChar(svc, CHAR_GAS_UUID);
  charEnv       = addChar(svc, CHAR_ENV_UUID);
#ifdef ENABLE_BATTERY
  charBattery   = addChar(svc, CHAR_BATT_UUID);
#endif

  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.printf("BLE advertising as \"%s\"\n", bleName.c_str());
  Serial.println("Open nRF Connect or LightBlue to see the data.\n");
}

// ═════════════════════════════════════════════════════════════
void loop() {
  float pm1p0 = 0, pm2p5 = 0, pm4p0 = 0, pm10p0 = 0;
  float humidity = 0, temperature = 0;
  float vocIndex = 0, noxIndex = 0;
  uint16_t co2 = 0;

  delay(1000);

  error = sensor.readMeasuredValues(
      pm1p0, pm2p5, pm4p0, pm10p0,
      humidity, temperature, vocIndex, noxIndex, co2);

  if (error != NO_ERROR) {
    Serial.print("Read error: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return;
  }

  // ── Pack & notify: Particles (16 bytes) ───────────────────
  // [PM1.0 f32] [PM2.5 f32] [PM4.0 f32] [PM10 f32]
  {
    uint8_t buf[16];
    memcpy(buf + 0,  &pm1p0,  4);
    memcpy(buf + 4,  &pm2p5,  4);
    memcpy(buf + 8,  &pm4p0,  4);
    memcpy(buf + 12, &pm10p0, 4);
    charParticles->setValue(buf, 16);
    if (deviceConnected) charParticles->notify();
  }

  // ── Pack & notify: Gases (10 bytes) ───────────────────────
  // [CO2 u16] [VOC f32] [NOx f32]
  {
    uint8_t buf[10];
    memcpy(buf + 0, &co2,      2);
    memcpy(buf + 2, &vocIndex, 4);
    memcpy(buf + 6, &noxIndex, 4);
    charGases->setValue(buf, 10);
    if (deviceConnected) charGases->notify();
  }

  // ── Pack & notify: Environment (8 bytes) ──────────────────
  // [Temperature f32] [Humidity f32]
  {
    uint8_t buf[8];
    memcpy(buf + 0, &temperature, 4);
    memcpy(buf + 4, &humidity,    4);
    charEnv->setValue(buf, 8);
    if (deviceConnected) charEnv->notify();
  }

  // ── Pack & notify: Battery (8 bytes, optional) ────────────
#ifdef ENABLE_BATTERY
  {
    float soc   = battPercent();
    float volts = battery.cellVoltage();
    float rate  = battery.chargeRate();
    uint8_t buf[12];
    memcpy(buf + 0, &soc,   4);
    memcpy(buf + 4, &volts, 4);
    memcpy(buf + 8, &rate,  4);
    charBattery->setValue(buf, 12);
    if (deviceConnected) charBattery->notify();
  }
#endif

  // ── Serial output ─────────────────────────────────────────
  Serial.println("────────────────────────────────────");
  Serial.printf("  PM2.5: %.1f µg/m³   CO2: %u ppm\n", pm2p5, co2);
  Serial.printf("  Temp:  %.1f °C      RH:  %.1f %%\n", temperature, humidity);
#ifdef ENABLE_BATTERY
  Serial.printf("  Battery: %.0f%% %.2fV %s\n",
                battPercent(), battery.cellVoltage(),
                battCharging() ? "[charging]" : "[discharging]");
#endif
  if (deviceConnected) Serial.println("  [BLE client connected]");

  // ── Handle reconnection ───────────────────────────────────
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    BLEDevice::startAdvertising();
    Serial.println("Restarted advertising");
  }
  oldDeviceConnected = deviceConnected;
}
