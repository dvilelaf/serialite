'use strict';

(async () => {
  const $ = (id) => document.getElementById(id);
  const out = $('out');
  const reboot = $('reboot');
  const say = (text) => { out.textContent += text + '\n'; };
  let maxBytes = 0;

  async function refresh() {
    const r = await Serialite.get('/api/ota');
    if (!r.ok) return;
    const s = await r.json();
    maxBytes = s.max_bytes;
    $('otaState').textContent = s.state;
    $('otaTarget').textContent = s.target || 'next OTA slot';
    $('otaBytes').textContent = `${s.written} / ${s.expected}`;
    $('otaError').textContent = s.last_error || 'none';
    reboot.disabled = !s.reboot_allowed;
  }

  const session = await Serialite.startSession().catch((e) => ({error: e}));
  if (session.locked || session.error) {
    say(session.locked ? 'Locked from the device; press PWR to unlock.' : session.error.message);
    return;
  }
  await refresh();

  $('upload').onclick = async () => {
    const file = $('fw').files[0];
    if (!file) {
      say('choose a firmware file first');
      return;
    }
    if (file.size === 0 || file.size > maxBytes) {
      say(`invalid size; the OTA slot holds at most ${maxBytes} bytes`);
      return;
    }
    if (!confirm(`Install ${file.name} (${file.size} bytes) in the inactive OTA slot?`)) return;
    say('uploading...');
    const r = await Serialite.post('/api/ota', file, 'application/octet-stream');
    say(`${r.status} ${await r.text()}`);
    await refresh();
  };

  reboot.onclick = async () => {
    if (!confirm('Reboot Serialite now? The serial bridge drops for a few seconds.')) return;
    const r = await Serialite.post('/api/reboot');
    say(`${r.status} ${await r.text()}`);
  };
})();
