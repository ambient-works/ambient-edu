/**
 * mock.js — fetch helpers for the sketches
 *
 * fetchLatest() and fetchHistory() call the real device API.
 * On failure they pass null to the callback so sketches can show an error.
 */

// ── Public API used by the sketches ─────────────────────────────────────────

window.fetchLatest = function(ip, callback) {
  fetch('http://' + ip + '/api', { signal: AbortSignal.timeout(4000) })
    .then(function(r) { return r.json(); })
    .then(function(data) { callback(data); })
    .catch(function(err) {
      console.warn('fetchLatest failed:', err);
      callback(null);
    });
};

window.fetchHistory = function(ip, callback) {
  fetch('http://' + ip + '/api/history', { signal: AbortSignal.timeout(4000) })
    .then(function(r) { return r.json(); })
    .then(function(data) { callback(data); })
    .catch(function(err) {
      console.warn('fetchHistory failed:', err);
      callback(null);
    });
};
