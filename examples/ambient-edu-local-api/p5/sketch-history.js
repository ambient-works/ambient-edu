// /**
//  * sketch-history.js — Sketch 02: History Graph
//  * ─────────────────────────────────────────────
//  * Fetches /api/history (an array of readings) and draws CO₂ and PM2.5
//  * as line graphs. Refreshes every 10 seconds.
//  *
//  * Things to try:
//  *  - Plot a different field (temperature, humidity…)
//  *  - Change the line colours or weights
//  *  - Draw filled areas instead of lines
//  *  - Show a tooltip on mouse hover
//  *  - Animate the line drawing in on first load
//  */

// const HISTORY_POLL_MS = 10000;

// new p5(function(p) {

//   // ── State ──────────────────────────────────────────────────────────────────
//   let history     = [];
//   let lastPoll    = -99999;
//   let fetching    = false;

//   const W = 520, H = 300;
//   const PAD = { top: 36, right: 20, bottom: 40, left: 52 };

//   // ── Helpers ────────────────────────────────────────────────────────────────

//   // Map a value in [inMin, inMax] to [outMin, outMax]
//   function remap(val, inMin, inMax, outMin, outMax) {
//     if (inMax === inMin) return (outMin + outMax) / 2;
//     return outMin + (val - inMin) / (inMax - inMin) * (outMax - outMin);
//   }

//   // Draw a line graph for a series of {x (canvas), y (canvas)} points
//   function drawLine(points, col) {
//     if (points.length < 2) return;
//     p.stroke(col);
//     p.strokeWeight(2);
//     p.noFill();
//     p.beginShape();
//     points.forEach(function(pt) { p.vertex(pt.x, pt.y); });
//     p.endShape();
//     p.noStroke();
//   }

//   // ── Derive canvas coords from history array for a given field ─────────────
//   function toPoints(field) {
//     if (!history.length) return [];
//     const vals   = history.map(function(d) { return d[field]; });
//     const minVal = Math.min(...vals);
//     const maxVal = Math.max(...vals);
//     const padded_min = minVal * 0.95;
//     const padded_max = maxVal * 1.05;

//     const plotW = W - PAD.left - PAD.right;
//     const plotH = H - PAD.top  - PAD.bottom;

//     return history.map(function(d, i) {
//       return {
//         x: PAD.left + remap(i, 0, history.length - 1, 0, plotW),
//         y: PAD.top  + remap(d[field], padded_min, padded_max, plotH, 0),
//       };
//     });
//   }

//   // ── p5 setup ───────────────────────────────────────────────────────────────
//   p.setup = function() {
//     let cnv = p.createCanvas(W, H);
//     cnv.parent('sketch-history');
//     p.textFont('DM Mono, monospace');
//     p.noLoop();   // we redraw manually when data arrives
//   };

//   // ── Poll ───────────────────────────────────────────────────────────────────
//   // p5's draw() won't loop automatically here — we kick a manual interval
//   // so students can see the setInterval pattern alongside p5.
//   setInterval(function() {
//     if (fetching) return;
//     fetching = true;
//     fetchHistory(window.deviceIP, function(d) {
//       history  = d;
//       fetching = false;
//       p.redraw();
//     });
//   }, HISTORY_POLL_MS);

//   // Trigger first fetch immediately
//   fetchHistory(window.deviceIP, function(d) {
//     history  = d;
//     fetching = false;
//     p.redraw();
//   });

//   // ── p5 draw ────────────────────────────────────────────────────────────────
//   p.draw = function() {
//     p.background(20, 23, 22);

//     if (!history.length) {
//       p.fill(90);
//       p.noStroke();
//       p.textSize(13);
//       p.textAlign(p.CENTER, p.CENTER);
//       p.text('waiting for history…', W / 2, H / 2);
//       return;
//     }

//     const plotW = W - PAD.left - PAD.right;
//     const plotH = H - PAD.top  - PAD.bottom;

//     // ── Grid ─────────────────────────────────────────────────────────────────
//     p.stroke(35, 42, 38);
//     p.strokeWeight(1);
//     const gridLines = 4;
//     for (let i = 0; i <= gridLines; i++) {
//       const y = PAD.top + (i / gridLines) * plotH;
//       p.line(PAD.left, y, PAD.left + plotW, y);
//     }
//     p.noStroke();

//     // ── CO₂ line  ─────────────────────────────────────────────────────────────
//     const co2pts = toPoints('co2');
//     drawLine(co2pts, p.color(74, 200, 240));      // blue

//     // ── PM2.5 line ────────────────────────────────────────────────────────────
//     const pmpts = toPoints('pm2p5');
//     drawLine(pmpts, p.color(57, 224, 122));        // green

//     // ── Y-axis labels (CO₂ range) ─────────────────────────────────────────────
//     const co2vals = history.map(function(d) { return d.co2; });
//     const co2min  = Math.min(...co2vals);
//     const co2max  = Math.max(...co2vals);
//     p.fill(70, 80, 76);
//     p.textSize(9);
//     p.textAlign(p.RIGHT, p.CENTER);
//     for (let i = 0; i <= gridLines; i++) {
//       const val = remap(i, 0, gridLines, co2max * 1.05, co2min * 0.95);
//       const y   = PAD.top + (i / gridLines) * plotH;
//       p.text(Math.round(val), PAD.left - 6, y);
//     }

//     // ── X-axis: first and last timestamp ─────────────────────────────────────
//     p.textAlign(p.LEFT, p.TOP);
//     p.fill(70, 80, 76);
//     p.textSize(9);
//     const fmt = function(iso) {
//       const d = new Date(iso);
//       return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
//     };
//     p.text(fmt(history[0].timestamp),                  PAD.left, H - PAD.bottom + 8);
//     p.textAlign(p.RIGHT, p.TOP);
//     p.text(fmt(history[history.length - 1].timestamp), PAD.left + plotW, H - PAD.bottom + 8);

//     // ── Legend ────────────────────────────────────────────────────────────────
//     p.noStroke();

//     p.fill(74, 200, 240);
//     p.rect(PAD.left, 10, 18, 3);
//     p.textAlign(p.LEFT, p.CENTER);
//     p.textSize(10);
//     p.text('CO₂ (ppm)', PAD.left + 22, 11);

//     p.fill(57, 224, 122);
//     p.rect(PAD.left + 110, 10, 18, 3);
//     p.text('PM2.5 (µg/m³)', PAD.left + 132, 11);

//     // ── Hover: show values at mouse position ─────────────────────────────────
//     if (p.mouseX > PAD.left && p.mouseX < PAD.left + plotW &&
//         p.mouseY > PAD.top  && p.mouseY < PAD.top  + plotH) {

//       const idx = Math.round(remap(p.mouseX, PAD.left, PAD.left + plotW,
//                                               0, history.length - 1));
//       const d   = history[Math.max(0, Math.min(idx, history.length - 1))];

//       // Vertical guide
//       p.stroke(60, 70, 65);
//       p.strokeWeight(1);
//       p.line(p.mouseX, PAD.top, p.mouseX, PAD.top + plotH);
//       p.noStroke();

//       // Tooltip box
//       const tx = p.mouseX > W / 2 ? p.mouseX - 130 : p.mouseX + 10;
//       p.fill(28, 33, 30, 230);
//       p.rect(tx, p.mouseY - 44, 120, 52, 3);

//       p.fill(74, 200, 240);
//       p.textAlign(p.LEFT, p.TOP);
//       p.textSize(11);
//       p.text('CO₂  ' + d.co2 + ' ppm',    tx + 8, p.mouseY - 38);

//       p.fill(57, 224, 122);
//       p.text('PM2.5  ' + d.pm2p5.toFixed(1) + ' µg', tx + 8, p.mouseY - 20);

//       p.fill(70, 80, 76);
//       p.textSize(9);
//       p.text(fmt(d.timestamp), tx + 8, p.mouseY - 2);
//     }
//   };

//   // Redraw on mouse move to show hover tooltip
//   p.mouseMoved = function() { p.redraw(); };

// }, document.getElementById('sketch-history').parentElement);
