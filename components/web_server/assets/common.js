'use strict';

// Shared by every page: the operator session and a few helpers.
// The session cookie is HttpOnly; the CSRF token lives only in this page.
const Serialite = (() => {
  let csrf = '';

  // Opens (or reuses) the operator session. Resolves to {locked: true} while
  // the device's emergency lock is engaged.
  async function startSession() {
    const r = await fetch('/api/session', {method: 'POST', cache: 'no-store'});
    if (r.status === 423) {
      csrf = '';
      return {locked: true};
    }
    if (!r.ok) {
      throw new Error('session request failed: HTTP ' + r.status);
    }
    const session = await r.json();
    csrf = session.csrf;
    return session;
  }

  function get(url) {
    return fetch(url, {cache: 'no-store'});
  }

  function post(url, body, contentType) {
    const headers = {'X-CSRF-Token': csrf};
    if (contentType) {
      headers['Content-Type'] = contentType;
    }
    return fetch(url, {method: 'POST', headers, body, cache: 'no-store'});
  }

  function esc(value) {
    const map = {'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'};
    return String(value == null ? '' : value).replace(/[&<>"']/g, (c) => map[c]);
  }

  function bytes(value) {
    const v = Number(value || 0);
    if (v > 1048576) return (v / 1048576).toFixed(1) + ' MB';
    if (v > 1024) return (v / 1024).toFixed(1) + ' KB';
    return v + ' B';
  }

  return {startSession, get, post, esc, bytes};
})();
