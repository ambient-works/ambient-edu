# BLE Simulator

Simulates the Ambient Edu BLE peripheral on a laptop — no hardware needed. Advertises the same service UUID, characteristics, and byte layout as `03-ble-broadcast.ino`, so your app can't tell the difference.

## Setup

```bash
# Create a virtual environment (recommended)
python3 -m venv venv
source venv/bin/activate    # macOS/Linux
# venv\Scripts\activate     # Windows

# Install dependencies
pip install -r requirements.txt
```

## Usage

```bash
python simulator.py              # advertise as "Ambient Edu 01"
python simulator.py --id 5       # advertise as "Ambient Edu 05"
```

The simulator generates realistic fake sensor data with gentle drift and random noise, updating every second. You'll see the values printed in your terminal.

### macOS note

The first time you run the simulator, macOS will ask for **Bluetooth permission** for your terminal app (Terminal, iTerm, VS Code, etc.). Grant it, then restart the script.

### Linux note

You may need to run with `sudo` or configure BlueZ permissions. See the [bless documentation](https://github.com/kevincar/bless) for details.

## Characteristic layout

Identical to `03-ble-broadcast.ino` — see the [main README](../../../README.md#characteristic-map) for the full byte-level spec.

| Characteristic | UUID suffix | Size |
|---|---|---|
| Particles | `0x0010` | 16 bytes (4 × float) |
| Gases | `0x0011` | 10 bytes (uint16 + 2 × float) |
| Environment | `0x0012` | 8 bytes (2 × float) |
| Battery | `0x0013` | 12 bytes (3 × float) |

## Simulating multiple devices

Open multiple terminals and run with different IDs:

```bash
# Terminal 1
python simulator.py --id 1

# Terminal 2
python simulator.py --id 2
```

Each will appear as a separate BLE device ("Ambient Edu 01", "Ambient Edu 02") — useful for testing multi-device scenarios.
