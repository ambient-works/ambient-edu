/**
 * app.js — UI glue
 *
 * Handles the IP input + Connect button.
 * When connected, sets window.USE_MOCK = false so the sketches
 * call the real device instead of mock data.
 */

// Default IP — sketches use this even before Connect is pressed
window.deviceIP = document.getElementById('ip-input').value.trim();

const connectBtn   = document.getElementById('connect-btn');
const statusDot    = document.getElementById('status-dot');
const statusLabel  = document.getElementById('status-label');

function setStatus(state) {
  statusDot.className = 'dot ' + state;
  const labels = {
    disconnected: 'not connected',
    connecting:   'connecting…',
    connected:    'connected  —  ' + window.deviceIP,
    error:        'connection failed',
  };
  statusLabel.textContent = labels[state] || state;
}

setStatus('disconnected');

connectBtn.addEventListener('click', function() {
  const ip = document.getElementById('ip-input').value.trim();
  if (!ip) return;
  window.deviceIP = ip;
  setStatus('connecting');

  // Probe the /api endpoint — if it responds we switch to live mode
  fetch('http://' + ip + '/api', { signal: AbortSignal.timeout(4000) })
    .then(function(r) {
      if (!r.ok) throw new Error('bad status ' + r.status);
      return r.json();
    })
    .then(function() {
      setStatus('connected');
    })
    .catch(function(err) {
      console.warn('Could not reach device:', err);
      setStatus('error');
    });
});
