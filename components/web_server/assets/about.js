'use strict';

(async () => {
  const info = document.getElementById('info');
  const rows = (pairs) => pairs.map(([k, v]) => `<dt>${Serialite.esc(k)}</dt><dd>${Serialite.esc(v)}</dd>`).join('');
  try {
    const session = await Serialite.startSession();
    if (session.locked) {
      info.innerHTML = rows([['Status', 'Locked from the device; press PWR to unlock']]);
      return;
    }
    const r = await Serialite.get('/api/about');
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const a = await r.json();
    info.innerHTML = rows([
      ['Firmware', a.version],
      ['Project', a.project],
      ['Built', `${a.build_date} ${a.build_time}`],
      ['ESP-IDF', a.idf],
      ['Transport', a.https
        ? 'HTTPS: compare the certificate SHA-256 in your browser with the one on the device screen (BOOT twice).'
        : 'HTTP inside the WPA2/WPA3 access point.'],
      ['Scrollback', `${a.scrollback.retained} of ${a.scrollback.capacity} bytes kept, ${a.scrollback.dropped} older bytes dropped`],
    ]);
  } catch (e) {
    info.innerHTML = rows([['Status', e.message || 'Unavailable']]);
  }
})();
