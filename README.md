# Ambient Edu

An educational air quality monitoring kit by [Ambient Works](https://ambientworks.io). Designed for students, makers, and educators to learn about environmental sensing with real hardware.

<p align="center">
  <img src="images/ambient-edu-kit.jpg" alt="Ambient Edu Kit" width="600">
</p>

> **Note:** Photo placeholder — replace `images/ambient-edu-kit.jpg` with an actual kit photo.

---

## What's in the Kit

| Component | Description |
|---|---|
| **SparkFun ESP32-C6 Thing Plus** | Wi-Fi 6 & BLE microcontroller with USB-C, Qwiic connector, and onboard WS2812 LED. [Hardware guide →](https://docs.sparkfun.com/SparkFun_Thing_Plus_ESP32_C6/hardware_overview/) |
| **Sensirion SEN66** | All-in-one environmental sensor measuring PM1.0, PM2.5, PM4, PM10, CO₂, VOC, NOx, temperature, and humidity. [Product page →](https://sensirion.com/products/catalog/SEN66) · [Datasheet (PDF) →](https://sensirion.com/media/documents/SEN6x_Datasheet.pdf) |
| **Lithium-Ion Battery** | 3500 mAh rechargeable battery for portable monitoring |
| **3D-Printed Enclosure** | Printable case for the kit — STL files in the [`3d-files/`](3d-files/) folder |
| **Qwiic Cable** | Pre-wired I²C cable to connect the sensor to the board (no soldering needed) |

---

## Getting Started

### 1. Install Arduino IDE

Download and install [Arduino IDE 2.x](https://www.arduino.cc/en/software).

### 2. Add ESP32-C6 Board Support

1. Open **File → Preferences**
2. In **Additional Board Manager URLs**, add:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**, search for **esp32**, and install **esp32 by Espressif Systems**
4. Select **Tools → Board → esp32 → ESP32C6 Dev Module**

### 3. Install Required Libraries

Open **Sketch → Include Library → Manage Libraries** and install:

- **Adafruit NeoPixel** — for the onboard LED
- **Sensirion I2C SEN66** — for the SEN66 sensor (also installs Sensirion Core automatically)

### 4. Connect the Hardware

1. Connect the SEN66 to the SparkFun board using the Qwiic cable
2. Plug in the USB-C cable to your computer
3. Select the correct port in **Tools → Port**

### 5. Upload an Example

Open one of the example sketches from the [`examples/`](examples/) folder, verify, and upload!

---

## Examples

### [`01_sensor_read.ino`](examples/01_sensor_read.ino)
**Read sensor data + LED colour alert**

Reads all 9 measurements from the SEN66 and prints them to Serial Monitor every second. A simple `if/else` block changes the LED colour based on PM2.5 levels — edit the thresholds to experiment with different pollutants.

What you'll learn:
- I²C sensor communication
- Reading environmental data
- Controlling a NeoPixel LED
- Using serial output for debugging

---

### Future Examples (Coming Soon)

| Example | Description |
|---|---|
| `02_data_logger.ino` | Log sensor readings to Serial in CSV format for import into spreadsheets |
| `03_wifi_dashboard.ino` | Serve a real-time web dashboard over Wi-Fi showing live air quality data |
| `04_threshold_buzzer.ino` | Add a piezo buzzer that beeps when air quality drops below a threshold |
| `05_battery_monitor.ino` | Read the battery level and display remaining charge on Serial |
| `06_sleep_mode.ino` | Use deep sleep to take periodic readings and extend battery life |
| `07_ble_broadcast.ino` | Broadcast sensor data over Bluetooth Low Energy for phone apps to read |
| `08_multi_sensor_compare.ino` | Compare readings from multiple pollutants and determine the worst one |

---

## Board Configuration

The example sketches support two boards via a `#define` at the top of each file:

```cpp
#define BOARD_AMBIENT_ONE       // Ambient One PCB (if you have one)
// #define BOARD_SPARKFUN_C6    // SparkFun ESP32-C6 Thing Plus (default for this kit)
```

Uncomment the line matching your board. The pin mappings adjust automatically:

| Pin | SparkFun C6 | Ambient One |
|---|---|---|
| NeoPixel LED | GPIO 23 | GPIO 23 |
| I²C SDA | GPIO 6 | GPIO 19 |
| I²C SCL | GPIO 7 | GPIO 20 |
| Power Enable | — | GPIO 15 |

---

## Sensor Reference

The SEN66 measures 9 environmental parameters:

| Parameter | Unit | Typical Range | Notes |
|---|---|---|---|
| PM1.0 | µg/m³ | 0 – 1000 | Fine particulate matter |
| PM2.5 | µg/m³ | 0 – 1000 | Most commonly regulated PM size |
| PM4.0 | µg/m³ | 0 – 1000 | Coarse particulate matter |
| PM10 | µg/m³ | 0 – 1000 | Total inhalable particles |
| CO₂ | ppm | 400 – 5000+ | Carbon dioxide |
| VOC Index | 0 – 500 | 100 = normal | Volatile organic compounds |
| NOx Index | 0 – 500 | 1 = normal | Nitrogen oxides |
| Temperature | °C | -10 – 50 | ±0.45°C accuracy |
| Humidity | %RH | 0 – 100 | ±4.5%RH accuracy |

> **Warm-up:** VOC and NOx indices need approximately 60 seconds after power-on to produce meaningful readings. PM and CO₂ stabilise faster but benefit from a few minutes of run time.

---

## 3D-Printed Enclosure

STL files for the enclosure are in the [`3d-files/`](3d-files/) folder.

**Print settings (recommended):**
- Material: PLA or PETG
- Layer height: 0.2 mm
- Infill: 15–20%
- No supports needed

---

## Troubleshooting

| Problem | Solution |
|---|---|
| **No serial output** | Make sure baud rate is set to **115200** in Serial Monitor |
| **Sensor not found / I²C error** | Check Qwiic cable is seated firmly on both ends |
| **All readings show 0 or 65535** | Sensor is still warming up — wait 60 seconds |
| **LED doesn't light up** | Verify the correct board is selected in the `#define` at the top |
| **Upload fails** | Hold the **BOOT** button on the SparkFun board while uploading |

---

## Project Ideas

Once you're comfortable with the basics, try these:

- 📊 **Classroom air quality map** — Place kits around a building and compare readings
- 🍳 **Cooking emissions study** — Monitor PM2.5 before, during, and after cooking
- 🚗 **Traffic pollution tracker** — Log data near roads at different times of day
- 🪴 **Plant room vs office** — Compare VOC and CO₂ levels in different environments
- 😮‍💨 **Breathing experiment** — Watch CO₂ rise when people enter a closed room

---

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License](LICENSE) (CC BY-NC-SA 4.0).

You are free to:
- **Share** — copy and redistribute the material
- **Adapt** — remix, transform, and build upon the material

Under the following terms:
- **Attribution** — You must give appropriate credit to [Ambient Works](https://ambientworks.io)
- **NonCommercial** — You may not use the material for commercial purposes
- **ShareAlike** — If you remix or build upon the material, you must distribute your contributions under the same license

This kit incorporates open-source components from [Sensirion](https://github.com/Sensirion) (BSD), [SparkFun](https://www.sparkfun.com/) (MIT), and [Adafruit](https://www.adafruit.com/) (MIT). See individual library licenses for details.

---

<p align="center">
  Made with ❤️ by <a href="https://ambientworks.io">Ambient Works</a>
</p>
