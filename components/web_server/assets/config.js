'use strict';

(async () => {
  const out = document.getElementById('out');
  const cfg = document.getElementById('cfg');
  const reboot = document.getElementById('reboot');
  const say = (text) => { out.textContent += text + '\n'; };

  const session = await Serialite.startSession().catch((e) => ({error: e}));
  if (session.locked || session.error) {
    say(session.locked ? 'Locked from the device; press PWR to unlock.' : session.error.message);
    return;
  }

  document.getElementById('load').onclick = async () => {
    const r = await Serialite.get('/api/config');
    const text = await r.text();
    if (!r.ok) {
      say(`export failed: ${r.status} ${text}`);
      return;
    }
    cfg.value = text;
    say('current config loaded');
  };

  document.getElementById('import').onclick = async () => {
    if (!confirm('Import this config? It applies after a reboot.')) return;
    const r = await Serialite.post('/api/config', cfg.value, 'application/json');
    say(`${r.status} ${await r.text()}`);
    if (r.ok) reboot.disabled = false;
  };

  reboot.onclick = async () => {
    if (!confirm('Reboot Serialite now? The serial bridge drops for a few seconds.')) return;
    const r = await Serialite.post('/api/reboot');
    say(`${r.status} ${await r.text()}`);
  };
})();
