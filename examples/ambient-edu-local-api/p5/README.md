# Ambient Air Quality — p5.js Demo

A minimal multi-file project showing how to fetch data from the Ambient
ESP32 sensor and visualise it with [p5.js](https://p5js.org/).

## Files

| File | Purpose |
|------|---------|
| `index.html` | Page structure — loads scripts in the right order |
| `style.css` | All visual styling |
| `mock.js` | Fake sensor data so you can work without a device |
| `sketch-live.js` | **Sketch 01** — polls `/api` every 2 s, draws live values |
| `sketch-history.js` | **Sketch 02** — fetches `/api/history`, draws line graphs |
| `app.js` | Connects the IP input to the sketches |

## Running locally

Just open `index.html` in a browser. The sketches use **mock data** by
default so everything works without a device connected.

## Connecting to a real device

1. Make sure your computer is on the same WiFi network as the ESP32.
2. Open `index.html`, type the device's IP address into the input box, and
   click **Connect**.
3. If the device responds, the status dot turns green and the sketches
   switch to live data automatically.

> **Note on CORS** — the ESP32 sketch already sends
> `Access-Control-Allow-Origin: *` so the browser won't block the request.
> If you're using the p5.js web editor, host the sketch there and it
> should work fine as long as the device is reachable.

## Extending the sketches

Each sketch file has a **"Things to try"** comment at the top. Some ideas:

- Add a third sketch that draws a coloured circle whose **size** maps to PM2.5
- Plot `temperature` and `humidity` on the history graph
- Add an animated alert when CO₂ > 1000 ppm
- Store readings in a `localStorage` array so history persists across page reloads

## API reference

### `GET /api`
Returns the latest single reading:
```json
{
  "timestamp": "2025-01-01T12:00:00Z",
  "pm1p0": 2.10, "pm2p5": 4.20, "pm4p0": 5.00, "pm10p0": 6.10,
  "humidity": 48.00, "temperature": 22.50,
  "vocIndex": 110.00, "noxIndex": 1.00, "co2": 612
}
```

### `GET /api/history`
Returns an array of the last N readings (oldest first):
```json
[
  { "timestamp": "…", "pm2p5": 4.1, "co2": 608, "temperature": 22.4, "humidity": 47.8 },
  …
]
```
