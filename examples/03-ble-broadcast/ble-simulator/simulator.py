#!/usr/bin/env python3
"""
Ambient Edu — BLE Simulator

Simulates the BLE peripheral from 03-ble-broadcast.ino so app developers
can build and test without the physical hardware.

The simulator advertises the same service and characteristics, packs data
in the exact same byte layout, and sends realistic (randomised) sensor
values every second.

Requirements:
    pip install bless

Usage:
    python simulator.py              # defaults to device ID 1
    python simulator.py --id 5       # advertise as "Ambient Edu 05"

Works on macOS, Linux, and Windows (with a BLE-capable Bluetooth adapter).
"""

import argparse
import asyncio
import math
import random
import struct
import time
from typing import Any

from bless import (
    BlessServer,
    BlessGATTCharacteristic,
    GATTCharacteristicProperties,
    GATTAttributePermissions,
)

# ── UUIDs (must match the Arduino sketch) ─────────────────────
SERVICE_UUID = "12340001-1234-1234-1234-123456789abc"
CHAR_PARTICLES_UUID = "12340010-1234-1234-1234-123456789abc"
CHAR_GASES_UUID = "12340011-1234-1234-1234-123456789abc"
CHAR_ENV_UUID = "12340012-1234-1234-1234-123456789abc"
CHAR_BATTERY_UUID = "12340013-1234-1234-1234-123456789abc"


# ── Fake sensor data generator ────────────────────────────────
class SensorSimulator:
    """Generates realistic-ish air quality data with gentle drift."""

    def __init__(self):
        self.t = 0
        # Base values (typical indoor air)
        self.pm25_base = 8.0
        self.co2_base = 550
        self.temp_base = 22.0
        self.humi_base = 48.0
        self.battery_soc = 78.0  # slowly drains
        self.battery_v = 3.85

    def tick(self) -> dict:
        self.t += 1
        s = self.t  # shorthand

        # Gentle sine-wave drift + small noise
        drift = math.sin(s / 60) * 0.3
        noise = random.gauss(0, 0.3)

        pm1 = max(0, self.pm25_base * 0.6 + drift + noise)
        pm25 = max(0, self.pm25_base + drift * 2 + noise)
        pm4 = max(0, pm25 * 1.1 + random.gauss(0, 0.2))
        pm10 = max(0, pm4 * 1.15 + random.gauss(0, 0.3))

        co2 = max(400, int(self.co2_base + math.sin(s / 90) * 60 + random.gauss(0, 10)))
        voc = max(0, 100 + math.sin(s / 120) * 30 + random.gauss(0, 5))
        nox = max(0, 1 + math.sin(s / 150) * 3 + random.gauss(0, 0.5))

        temp = self.temp_base + math.sin(s / 200) * 1.5 + random.gauss(0, 0.1)
        humi = self.humi_base + math.sin(s / 180) * 5 + random.gauss(0, 0.3)

        # Battery slowly drains
        self.battery_soc = max(0, min(100, self.battery_soc - 0.002))
        self.battery_v = 3.3 + (self.battery_soc / 100) * 0.85
        charge_rate = -0.12  # %/hr, negative = discharging

        return {
            "pm1": pm1, "pm25": pm25, "pm4": pm4, "pm10": pm10,
            "co2": co2, "voc": voc, "nox": nox,
            "temp": temp, "humi": humi,
            "soc": self.battery_soc, "voltage": self.battery_v,
            "charge_rate": charge_rate,
        }


# ── Pack data into the same byte layout as the Arduino sketch ─
def pack_particles(d: dict) -> bytes:
    """16 bytes: [PM1.0 f32] [PM2.5 f32] [PM4.0 f32] [PM10 f32]"""
    return struct.pack("<ffff", d["pm1"], d["pm25"], d["pm4"], d["pm10"])


def pack_gases(d: dict) -> bytes:
    """10 bytes: [CO2 u16] [VOC f32] [NOx f32]"""
    return struct.pack("<Hff", d["co2"], d["voc"], d["nox"])


def pack_environment(d: dict) -> bytes:
    """8 bytes: [Temp f32] [Humidity f32]"""
    return struct.pack("<ff", d["temp"], d["humi"])


def pack_battery(d: dict) -> bytes:
    """12 bytes: [SoC f32] [Voltage f32] [ChargeRate f32]"""
    return struct.pack("<fff", d["soc"], d["voltage"], d["charge_rate"])


# ── BLE server ────────────────────────────────────────────────
def on_read(characteristic: BlessGATTCharacteristic, **kwargs) -> bytearray:
    return characteristic.value


async def run(device_id: int):
    name = f"Ambient Edu {device_id:02d}"
    print(f"Starting BLE simulator as \"{name}\"")
    print(f"Service UUID: {SERVICE_UUID}")
    print()

    server = BlessServer(name=name)
    server.read_request_func = on_read

    await server.add_new_service(SERVICE_UUID)

    flags = (
        GATTCharacteristicProperties.read
        | GATTCharacteristicProperties.notify
    )
    perms = GATTAttributePermissions.readable

    for uuid in [CHAR_PARTICLES_UUID, CHAR_GASES_UUID, CHAR_ENV_UUID, CHAR_BATTERY_UUID]:
        await server.add_new_characteristic(
            SERVICE_UUID, uuid, flags, None, perms,
        )

    await server.start()
    print(f"Advertising as \"{name}\" — open nRF Connect or your app to connect.")
    print("Press Ctrl+C to stop.\n")

    sim = SensorSimulator()

    try:
        while True:
            d = sim.tick()

            server.get_characteristic(CHAR_PARTICLES_UUID).value = bytearray(pack_particles(d))
            server.update_value(SERVICE_UUID, CHAR_PARTICLES_UUID)

            server.get_characteristic(CHAR_GASES_UUID).value = bytearray(pack_gases(d))
            server.update_value(SERVICE_UUID, CHAR_GASES_UUID)

            server.get_characteristic(CHAR_ENV_UUID).value = bytearray(pack_environment(d))
            server.update_value(SERVICE_UUID, CHAR_ENV_UUID)

            server.get_characteristic(CHAR_BATTERY_UUID).value = bytearray(pack_battery(d))
            server.update_value(SERVICE_UUID, CHAR_BATTERY_UUID)

            # Print to terminal so you can see what's being sent
            print(
                f"  PM2.5: {d['pm25']:5.1f} µg/m³  "
                f"CO2: {d['co2']:4d} ppm  "
                f"Temp: {d['temp']:4.1f}°C  "
                f"RH: {d['humi']:4.1f}%  "
                f"Batt: {d['soc']:3.0f}%"
            )

            await asyncio.sleep(1)

    except KeyboardInterrupt:
        print("\nStopping...")

    await server.stop()
    print("Simulator stopped.")


# ── Entry point ───────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Ambient Edu BLE Simulator")
    parser.add_argument(
        "--id", type=int, default=1, metavar="N",
        help="Device ID (1–99), sets BLE name to 'Ambient Edu NN' (default: 1)",
    )
    args = parser.parse_args()

    asyncio.run(run(args.id))
