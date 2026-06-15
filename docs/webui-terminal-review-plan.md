# WebUI Terminal Review and Redesign Plan

Fecha: 2026-06-15

Este documento consolida la review de equipo sobre la WebUI del ESP32-KVM. El objetivo no es embellecer una pagina: la WebUI debe sentirse como una terminal Linux real, ser segura bajo presion operativa y poder probarse localmente antes de flashear.

## Decision de Producto

La WebUI sera una experiencia terminal-first:

- `/` redirige a `/terminal`.
- No hay pantalla de login web ni password web.
- La frontera de seguridad primaria es acceso fisico al secreto WiFi local, AP WPA2 fuerte, token de sesion web aleatorio, CSRF/Origin y autenticacion real en Linux/getty.
- Hay una unica sesion web operativa. La sesion activa tiene input; las sesiones reemplazadas dejan de recibir salida y no pueden escribir.
- La terminal ocupa casi toda la pantalla. Los controles viven en un panel minimo.
- La UI solo muestra indicadores cuando aportan decision operativa. `Stream OK` no debe competir visualmente con la terminal.

## Findings Consolidados

### Criticos

1. La WebUI actual renderiza un log, no una terminal.
   El output se aplana a `textContent`, se ignora `\r`, se pierden ANSI/CSI y no hay modelo de cursor. Esto rompe `clear`, `top`, `journalctl`, progress redraws, colores, prompts y line editing.

2. El harness browser esta desfasado.
   El test Playwright todavia abre `/login` y pulsa `Open console`. La prueba e2e mas cercana al producto no valida el flujo actual.

3. Paste grande puede ejecutarse parcialmente sin feedback.
   El navegador permite hasta 2048 bytes, pero la politica del servidor acepta menos por ventana. Un script pegado puede quedar truncado a mitad de comando.

4. Emergency lock no es durable.
   Invalida sesiones, pero `/terminal` puede autocrear una nueva sesion y limpiar el estado. Un lock de emergencia no puede desbloquearse con un refresh.

5. HTTPS local rompe acciones web.
   La politica `Origin` acepta `http://` pero no `https://`, mientras la UI usa `wss://` si la pagina carga por HTTPS.

### Altos

1. Scroll inutil bajo output activo.
   Cada byte fuerza autoscroll al fondo. El usuario no puede leer boot logs mientras siguen llegando datos.

2. Sesiones reemplazadas pueden seguir recibiendo salida.
   La sesion pierde permisos de escritura, pero WebSockets antiguos pueden seguir en la lista de broadcast.

3. Segundo operador toma control en silencio.
   Abrir `/terminal` reemplaza al operador anterior sin pantalla de ocupacion ni confirmacion explicita.

4. `Ctrl+L` no se captura.
   En navegador suele enfocar la barra de direcciones. En una terminal debe enviar `0x0c` al host.

5. `Ctrl+C` puede romper copy.
   Si hay texto seleccionado, `Ctrl+C` debe copiar en el navegador, no enviar SIGINT al servidor.

6. Estados criticos son poco accionables.
   `Stream OK` solo significa WebSocket conectado, no que USB este conectado, que input sea seguro o que la sesion siga activa.

7. Mobile input no esta probado como mobile input.
   Los tests usan `page.keyboard`, no verifican foco real del textarea ni botones tactiles.

### Medios

1. Reconnect duplica scrollback.
   Cada reconexion recibe scrollback y lo concatena sobre el buffer existente.

2. Diagnostics son utiles pero no operator-grade.
   Falta jerarquia, severidad, interpretacion, copy bundle y campos como sesion, clientes web, TLS, drops de scrollback y firmware hash.

3. Rotacion WiFi puede dejar al operador varado.
   La pagina invalida la sesion y redirige sin un checklist durable de aplicar/reboot/reconnect.

4. `More` es demasiado textual/generico.
   Debe ser un icono compacto con accesibilidad y grupos claros.

5. Docs/checklist siguen describiendo web password, login y pairing.
   La documentacion debe alinearse con el modelo real.

## User Stories Obligatorias

### US1: Abrir consola y operar como terminal real

Como operador, abro `http://192.168.4.1/`, veo output de la consola serie, pulso Enter, veo `login:` si getty esta activo, escribo credenciales y uso una shell normal.

Criterios:

- No aparece pantalla intermedia de login.
- No hay botones `Take Control` / `Release`.
- La terminal soporta `CR`, `LF`, `BS`, `TAB`, `Ctrl+C`, `Ctrl+D`, `Ctrl+L`, `Esc`, flechas y Enter.
- `Ctrl+L` envia `0x0c`.
- `Ctrl+C` copia texto si hay seleccion; solo envia ETX si no hay seleccion o si se pulsa el boton dedicado.

### US2: Leer output historico sin pelear con autoscroll

Como operador, puedo hacer scroll hacia arriba para leer logs mientras el servidor sigue emitiendo output.

Criterios:

- Si el usuario esta cerca del fondo, la terminal sigue live.
- Si el usuario sube scroll, autoscroll se pausa.
- Aparece `Paused` y un boton `Jump to live`.
- Si se descarta scrollback, aparece un marcador visible `older output dropped`.

### US3: Reconnect sin duplicar ni mentir

Como operador, si el WebSocket se corta y vuelve, la UI indica reconexion y restaura contexto sin duplicar output como si fuese nuevo.

Criterios:

- `Stream reconnecting` aparece solo durante reconexion.
- El scrollback restaurado se marca como restaurado o reemplaza el buffer previo.
- La UI no habilita input hasta confirmar sesion activa, USB y estado writer.

### US4: Segundo operador no toma control en silencio

Como segundo operador, si abro la WebUI mientras otra sesion esta activa, veo que la consola esta en uso y debo confirmar takeover.

Criterios:

- Pantalla/overlay: `Console in use`.
- Muestra edad aproximada de la sesion y clientes web si esta disponible.
- Accion explicita: `Take over`.
- Al tomar control, los WebSockets anteriores se cierran o dejan de recibir output.
- La sesion anterior muestra `Session replaced by another browser` y queda read-only.

### US5: Emergency lock cierra control de verdad

Como operador, puedo cortar todas las sesiones web si sospecho uso indebido.

Criterios:

- `Emergency lock` invalida sesiones, cierra WebSockets, bloquea input y macros.
- El estado queda latched.
- `/terminal` muestra una pagina locked, no autocrea sesion.
- Unlock requiere presencia fisica: BOOT, reboot o factory/reset flow definido.
- La accion web no redirige automaticamente a una nueva sesion.

### US6: Logout o End session tiene semantica clara

Como operador, puedo cerrar mi sesion sin crear otra inmediatamente.

Criterios:

- Si se mantiene, el boton se llama `End session`.
- Cierra WebSockets de esa sesion.
- Deja una pantalla final no auto-session: `Session ended`.
- Tiene accion secundaria `Open terminal` para crear una nueva sesion conscientemente.
- Si esto no se implementa, eliminar el boton de la UI principal y dejar solo `Emergency lock`.

### US7: Diagnostics ayudan durante incidente

Como operador, abro diagnostics y entiendo si el problema es USB, stream, bridge, WiFi, sesion, memoria o firmware.

Criterios:

- Vista compacta por tarjetas: `USB`, `Stream`, `Bridge`, `WiFi AP`, `Session`, `Memory`, `Firmware`, `Recent events`.
- Cada tarjeta tiene estado `OK`, `WARN` o `FAIL`.
- Incluye bytes RX/TX, drops, queue drops, input rejects, scrollback retained/capacity/dropped, clientes WiFi/web, writer/session state, TLS state, uptime, reset reason, firmware version/hash.
- Botones: `Back to terminal`, `Copy diagnostics`, `Download JSON`.
- No muestra secretos ni transcripcion completa por defecto.

### US8: Rotar WiFi sin dejar varado al operador

Como operador, puedo rotar la password WiFi entendiendo exactamente cuando cambia, donde verla y como reconectar.

Criterios:

- Flujo tipo wizard: `Preflight`, `Rotate`, `Read on AMOLED`, `Apply/Reboot`, `Reconnect`, `Verify`.
- La nueva password nunca se devuelve por HTTP.
- La WebUI explica si la password vieja sigue activa hasta reboot o si ya se aplico.
- Tras rotar, se cierra o bloquea la sesion de forma explicita.
- El harness puede simular exito/fallo de rotacion.

## Arquitectura UX Propuesta

### Terminal

La terminal debe ocupar el 100% del viewport. El HUD debe ser flotante, compacto y silencioso si todo esta bien.

Estados visibles:

- Normal: solo un punto verde pequeno o nada; terminal domina.
- USB off: overlay persistente `USB disconnected. Check cable/getty.`
- Reconnecting: overlay `Reconnecting in Ns`.
- Session replaced: overlay read-only con `Open new session` / `Take over`.
- Locked: pantalla completa locked con instrucciones fisicas.
- Input rejected: toast persistente corto con motivo.

### More Menu

Usar un icono tipo kebab o terminal-menu con `aria-label="More controls"` y `aria-expanded`.

Grupos:

- `Keys`: Esc, Tab, Ctrl+C, Ctrl+D, Ctrl+L, arrows, Enter.
- `Terminal`: Clear display, Jump to live, Copy visible output.
- `Device`: Diagnostics, Credentials, Runbook, Firmware, About.
- `Danger`: End session, Emergency lock.

`More` no debe ser texto grande. En desktop y movil debe quedar arriba a la derecha sin quitar espacio a la terminal.

### Stream OK

No usar `Stream OK` como texto persistente principal. Reglas:

- Si todo esta bien, mostrar solo un dot pequeno o nada.
- Si falla WebSocket: `Stream reconnecting`.
- Si falla USB: `USB disconnected`.
- Si falla input/session: `Input locked`, `Session replaced`, `Emergency locked`.

## Arquitectura Tecnica Propuesta

### Terminal Renderer

Opcion recomendada: VT subset en cliente.

Implementar un modelo de pantalla limitado pero real:

- filas y columnas logicas;
- cursor;
- CR/LF/BS/TAB;
- SGR basico;
- CSI `J`, `K`, `H`, `A/B/C/D`;
- clear screen y clear line;
- wrap;
- scrollback.

El servidor no debe eliminar todas las CSI antes del cliente. Debe filtrar secuencias peligrosas como OSC clipboard/title si se exponen.

### Protocolo WebSocket

Separar tipos de mensaje:

- `replay`: scrollback restaurado;
- `data`: bytes live;
- `state`: cambios de USB/session/lock;
- `error`: input rechazado, rate limit, queue full.

Si se mantiene texto plano por compatibilidad, anadir al menos marcadores internos para replay y state antes de VT completo.

### Sesion y WebSockets

Cambios obligatorios:

- No broadcast a WebSockets con token invalido.
- Al crear/tomar sesion, cerrar WebSockets anteriores o marcarlos read-only sin salida sensible.
- Exponer motivo de invalidacion: timeout, takeover, emergency lock, credentials rotated, logout.
- `logout/end session` no debe redirigir a `/terminal` si `/terminal` autocrea sesion.

### Emergency Lock

Agregar estado latched:

- `s_runtime_status.locked = true` debe bloquear `ensure_web_session`.
- `/terminal` en locked responde con locked page.
- Unlock solo via evento fisico o reboot segun decision final.
- Web action debe esperar confirmacion del trabajo encolado o devolver un estado que el cliente trate como terminal.

### Input y Paste

Reglas:

- Client-side pacing respeta presupuesto del servidor.
- Paste grande requiere confirmacion y envio pausado.
- Si el servidor rechaza bytes, la UI lo muestra.
- No enviar nada al host al abrir/reconectar.

## Diagnostics Redesign

La pagina `/diagnostics` debe ser un panel de incidente, no un dump.

Layout:

```text
------------------------------------------------+
| Diagnostics                         Terminal  |
+------------+------------+------------+--------+
| USB OK     | Stream OK  | Bridge OK  | AP OK  |
| CDC ACM    | WS live    | no drops   | 1 WiFi |
+------------+------------+------------+--------+
| Session: active, age 04:12, clients web 1     |
| Memory: heap 180 KB, min 122 KB               |
| Firmware: a07e05a, reset POWERON, uptime ...  |
| Scrollback: 8192 retained, 0 dropped          |
+------------------------------------------------+
| Recent events                                  |
| 12:01 USB connected                            |
| 12:02 session created                          |
+------------------------------------------------+
| Copy diagnostics | Download JSON              |
+------------------------------------------------+
```

## Test Strategy

### Host C Tests

- `/login` remains absent.
- `/terminal` contains terminal-first shell.
- `Ctrl+L` constants and handlers exist.
- `/terminal-status.json` exposes USB, writer, lock reason, session reason, scrollback retained/capacity/dropped.
- Origin policy accepts `http://` and `https://` with correct host/default ports.
- Diagnostics JSON includes scrollback fields and redacts secrets.

### Python Harness Tests

- Opening `/terminal` creates exactly one session.
- Emergency lock latches and `/terminal` no longer autocreates.
- End session lands on non-auto-session page.
- Second session requires explicit takeover or exposes occupied state.
- Rotation success/failure is stubbed and never returns secrets.

### Playwright Browser Tests

The browser harness is a release gate.

Required tests:

- Loads `/` and reaches terminal without `/login`.
- No `Open console`, no password field, no `Take Control`.
- `Ctrl+L`, Backspace, Tab, Enter, Esc, arrows, Ctrl+C, Ctrl+D send expected bytes.
- Selected text + `Ctrl+C` copies, does not send ETX.
- Scroll pause and `Jump to live`.
- Reconnect does not duplicate scrollback.
- Two browser contexts: takeover/replace state is visible and old socket stops receiving.
- Emergency lock closes socket and blocks auto-session.
- Diagnostics navigation, copy bundle, JSON redaction.
- WiFi rotation wizard with fake backend.
- Mobile tap focuses input target and toolbar buttons send bytes.

## Implementation Plan

### Phase 0: Test Gate Repair

1. Update browser harness away from `/login`.
2. Make harness expose controllable fake serial/WebSocket state.
3. Add failing tests for current regressions: Ctrl+L, scroll pause, emergency lock latch, second-session output leak.
4. Do not flash WebUI changes unless browser harness passes.

### Phase 1: Safe Session Semantics

1. Implement latched emergency lock.
2. Change logout to `End session` page or remove it from main terminal.
3. Close/disable WebSockets tied to invalidated sessions.
4. Add explicit takeover flow or occupied state.
5. Add lock/takeover/session reason to status JSON.

### Phase 2: Terminal Behavior

1. Implement VT subset renderer or integrate a small self-contained terminal renderer.
2. Preserve safe ANSI data to client.
3. Implement Ctrl+L and copy-safe Ctrl+C.
4. Fix scroll pause/follow and dropped-output marker.
5. Fix reconnect replay semantics.
6. Fix mobile input target.

### Phase 3: Operator UI

1. Replace HUD with silent status rail.
2. Convert `More` to compact icon menu with groups.
3. Add operator overlays for USB off, reconnecting, input locked, session replaced, locked.
4. Remove persistent `Stream OK` text unless degraded.

### Phase 4: Diagnostics and WiFi Rotation

1. Redesign diagnostics as health board.
2. Add missing JSON fields and copy/download.
3. Replace credentials page with WiFi rotation wizard.
4. Require physical/local confirmation where needed.
5. Keep HTTP responses secret-free.

### Phase 5: Documentation Alignment

1. Update `docs/ui-ux-design.md`.
2. Update `docs/feature-checklist.md`.
3. Update `docs/production-hardening.md`.
4. Mark web password/pairing requirements as removed or replaced by AP credential + physical presence + session controls.

## Acceptance Gate

Antes de considerar la WebUI lista:

- Host C tests pasan.
- Python harness tests pasan.
- Playwright harness pasa.
- `idf.py build` pasa sin warnings nuevos.
- El dispositivo flasheado permite abrir `/`, ver terminal, usar teclado, hacer scroll, abrir diagnostics y ejecutar emergency lock sin autocrear sesion.
- No se pide al usuario probar manualmente un flujo que el harness pueda cubrir.

## Decision Pendiente

Hay una decision de producto que debe cerrarse antes de implementar Phase 1:

- Opcion A: segundo cliente ve `Console in use` y debe confirmar `Take over`.
- Opcion B: ultimo cliente gana automaticamente, pero con notificacion visible y cierre inmediato de sockets anteriores.

Recomendacion del equipo: Opcion A. Es menos sorprendente y evita tomar control en silencio durante una operacion critica.
