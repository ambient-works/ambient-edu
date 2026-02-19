/**
 * sketch-live.js — Sketch 01: Live Reading
 * ─────────────────────────────────────────
 * This sketch calls /api every 2 seconds and draws the four most useful
 * air quality values to a canvas. It's intentionally simple so students
 * can read and modify it easily.
 *
 * Things to try:
 *  - Change the poll interval (POLL_MS)
 *  - Add more values from the data object (pm1p0, vocIndex, noxIndex…)
 *  - Change the colour thresholds
 *  - Draw a shape instead of a number — e.g. a circle whose size = PM2.5
 */

// ── How often to poll (milliseconds) ────────────────────────────────────────
const POLL_MS = 2000;

new p5(function(p) {

  // ── State ──────────────────────────────────────────────────────────────────
  let data        = null;   // latest reading from the API
  let lastPoll    = -9999;  // millis() of the last successful fetch
  let fetching    = false;  // avoid overlapping requests
  let fetchError  = false;  // true when device is unreachable
  let currentIP   = '';     // track IP changes to reset state

  // ── Canvas size ────────────────────────────────────────────────────────────
  const W = 520, H = 300;

  // ── Colour helpers ─────────────────────────────────────────────────────────
  // Returns a p5 color based on PM2.5 (µg/m³) — WHO guidelines
  function pm25Color(val) {
    if (val === null || val === undefined) return p.color(80);
    if (val < 12)  return p.color(57, 224, 122);   // good   — green
    if (val < 35)  return p.color(240, 180, 41);   // moderate — yellow
    return              p.color(245, 80, 80);       // poor  — red
  }

  // Returns a p5 color based on CO₂ ppm
  function co2Color(val) {
    if (val === null || val === undefined) return p.color(80);
    if (val < 800)  return p.color(57, 224, 122);
    if (val < 1200) return p.color(240, 180, 41);
    return               p.color(245, 80, 80);
  }

  // ── p5 setup ───────────────────────────────────────────────────────────────
  p.setup = function() {
    let cnv = p.createCanvas(W, H);
    cnv.parent('sketch-live');
    p.textFont('Space Mono, monospace');
    p.noStroke();
  };

  // ── p5 draw ────────────────────────────────────────────────────────────────
  p.draw = function() {
    // Reset state when IP changes (e.g. after clicking Connect)
    if (window.deviceIP && window.deviceIP !== currentIP) {
      currentIP  = window.deviceIP;
      data       = null;
      fetching   = false;
      fetchError = false;
      lastPoll   = -9999;
    }

    // Poll at the right interval
    if (!fetching && p.millis() - lastPoll > POLL_MS) {
      fetching = true;
      fetchLatest(window.deviceIP, function(d) {
        if (d) {
          data       = d;
          fetchError = false;
        } else {
          fetchError = true;
        }
        lastPoll = p.millis();
        fetching = false;
      });
    }

    // Background
    p.background(20, 23, 22);

    if (fetchError && !data) {
      // Could not reach device
      p.fill(245, 80, 80);
      p.textSize(13);
      p.textAlign(p.CENTER, p.CENTER);
      p.text('could not reach ' + window.deviceIP, W / 2, H / 2 - 10);
      p.fill(90);
      p.textSize(11);
      p.text('check the IP address and try again', W / 2, H / 2 + 12);
      return;
    }

    if (!data) {
      // Waiting state
      p.fill(90);
      p.textSize(13);
      p.textAlign(p.CENTER, p.CENTER);
      p.text('waiting for data…', W / 2, H / 2);
      return;
    }

    // ── Draw four metric tiles ───────────────────────────────────────────────
    const metrics = [
      { label: 'PM2.5',  unit: 'µg/m³', value: data.pm2p5,       color: pm25Color(data.pm2p5) },
      { label: 'CO₂',    unit: 'ppm',   value: data.co2,          color: co2Color(data.co2)   },
      { label: 'VOC',    unit: 'index', value: data.vocIndex,    color: p.color(200, 160, 255) },
      { label: 'TEMP',   unit: '°C',    value: data.temperature,  color: p.color(74, 200, 240) },
      { label: 'RH',     unit: '%',     value: data.humidity,     color: p.color(74, 200, 240) },
    ];

    const cols   = 5;
    const tileW  = W / cols;
    const tileH  = H;

    metrics.forEach(function(m, i) {
      const x = i * tileW;

      // Subtle tile separator
      if (i > 0) {
        p.fill(40, 46, 44);
        p.rect(x, 0, 1, tileH);
      }

      // Coloured accent bar at top
      p.fill(m.color);
      p.rect(x, 0, tileW, 4);

      // Label
      p.fill(90, 100, 96);
      p.textSize(10);
      p.textAlign(p.LEFT, p.TOP);
      p.text(m.label, x + 16, 22);

      // Value
      p.fill(m.color);
      p.textSize(38);
      p.textAlign(p.LEFT, p.CENTER);
      const displayVal = (typeof m.value === 'number')
        ? (Number.isInteger(m.value) ? m.value : m.value.toFixed(1))
        : '—';
      p.text(displayVal, x + 16, H / 2 - 8);

      // Unit
      p.fill(90, 100, 96);
      p.textSize(11);
      p.textAlign(p.LEFT, p.BOTTOM);
      p.text(m.unit, x + 16, H - 20);
    });

    // ── Timestamp ─────────────────────────────────────────────────────────────
    p.fill(55, 65, 60);
    p.textSize(10);
    p.textAlign(p.RIGHT, p.BOTTOM);
    p.text(data.timestamp, W - 10, H - 8);
  };

}, document.getElementById('sketch-live').parentElement);
