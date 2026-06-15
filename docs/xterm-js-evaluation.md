# xterm.js Evaluation for ESP32-KVM

Fecha: 2026-06-15

## Objetivo

Evaluar si conviene usar `xterm.js` para la terminal web del ESP32-KVM en lugar de implementar un emulador VT propio.

## Librerias Evaluadas

- `@xterm/xterm@6.0.0`
- `@xterm/addon-fit@0.11.0`
- `@xterm/addon-serialize@0.14.0`

Referencias:

- https://xtermjs.org/
- https://github.com/xtermjs/xterm.js/
- https://www.npmjs.com/package/@xterm/xterm

Licencia: MIT.

## Medicion de Tamano

Medicion realizada con `npm install` y `esbuild --bundle --minify --format=iife --target=es2018`.

| Asset | Raw | Gzip |
| --- | ---: | ---: |
| `@xterm/xterm/lib/xterm.js` original | 488,663 B | no medido |
| `@xterm/xterm/css/xterm.css` | 7,112 B | 2,509 B |
| Bundle `@xterm/xterm + addon-fit` | 349,076 B | 89,257 B |
| Bundle `@xterm/xterm + addon-fit + addon-serialize` | 365,017 B | 93,853 B |

Binario firmware actual:

- `build/esp32_kvm.bin`: ~1.5 MB.
- Particion app minima: `0x1f0000`.
- Margen reciente reportado por `idf.py build`: ~25%.

Conclusion de flash: el tamano raw de ~356 KB JS+CSS cabe en flash si se embebe directamente. Precomprimido gzip seria mucho mejor para red y tiempo de carga, pero requiere servir `Content-Encoding: gzip`.

## Prueba de Navegador

Se cargo un HTML temporal con:

- `xterm-kvm.min.js`
- `xterm.css`
- `Terminal`
- `FitAddon`
- colores ANSI rojo/verde
- backspace
- clear screen `CSI 2J`

Resultado Playwright/Chromium:

- `loaded: true`
- init aproximado en desktop Chromium: `18.4 ms`
- la instancia se creo y acepto secuencias ANSI basicas.

Esto no prueba rendimiento en movil real conectado al AP ni memoria del ESP32. Si prueba que el bundle generado funciona offline sin CDN.

## Ventajas de Usar xterm.js

- Terminal real ya probada en produccion por muchas herramientas web.
- Soporte de colores ANSI, cursor, clear screen, line wrapping y scrollback.
- Mejor comportamiento con shell, getty, `journalctl`, `top`, prompts y redraws.
- API clara para `onData`, `write`, focus, paste y theme.
- Evita mantener un emulador VT propio incompleto.
- Facilita una UX profesional rapidamente.

## Costes y Riesgos

- Asset JS relativamente grande para ESP32: ~349 KB minificado.
- Sin gzip, primera carga por AP puede sentirse lenta en movil.
- Necesita nuevas rutas estaticas: `/assets/xterm.js`, `/assets/xterm.css`.
- CSP debe permitir scripts externos locales mediante `script-src 'self'`.
- Hay que decidir si embebemos assets en firmware o usamos SPIFFS/partition.
- Debe probarse en movil real: teclado virtual, scroll, copy/paste, rendimiento.
- Hay que conservar controles de seguridad propios: CSRF, Origin, session token, rate limit, emergency lock.
- No resuelve por si solo el problema de sesiones reemplazadas, sockets viejos, paste pacing o lock durable.

## Comparacion con VT Propio

### xterm.js

Pros:

- Mejor calidad terminal desde el primer dia.
- Colores y ANSI/cursor resueltos.
- Menos riesgo de bugs sutiles de terminal.
- Mejor compatibilidad futura con curses/fullscreen.

Contras:

- Mas flash y mas JS.
- Requiere pipeline de vendor/build.
- Requiere pruebas reales de carga y movil.

### VT Propio

Pros:

- JS pequeno.
- Control total del comportamiento.
- Menos dependencias.

Contras:

- Alto riesgo de quedarse en "parece terminal pero no lo es".
- Implementar bien ANSI/cursor/scrollback lleva tiempo.
- Cada edge case de Linux/getty/curses se convierte en deuda propia.

## Recomendacion

Usar `xterm.js` vendorizado para la WebUI, sin CDN.

Decision concreta:

1. Vendorizar `@xterm/xterm` y `@xterm/addon-fit`.
2. No incluir `addon-serialize` inicialmente.
3. Generar bundle minificado `xterm-kvm.min.js`.
4. Generar asset gzip precomprimido si el servidor puede enviarlo con `Content-Encoding: gzip`.
5. Servir assets como rutas estaticas locales.
6. Mantener el protocolo WebSocket actual de bytes binarios para `term.write(...)` y `term.onData(...)`.
7. Resolver antes o junto con la integracion:
   - emergency lock latched;
   - cerrar WebSockets de sesiones invalidadas;
   - paste pacing/feedback;
   - harness browser actualizado.

## Plan de Spike Recomendado

### Spike 1: Assets Offline

- Crear `components/web_server/assets/xterm-kvm.min.js`.
- Crear `components/web_server/assets/xterm.css`.
- Incluir assets con `EMBED_FILES` o `EMBED_TXTFILES`.
- Servir `/assets/xterm-kvm.min.js` y `/assets/xterm.css`.
- Si se usa gzip, servir `.gz` con `Content-Encoding: gzip`.

### Spike 2: Terminal Page

- Sustituir `#terminal` plano por `new Terminal(...)`.
- Conectar `term.onData(data => ws.send(data))`.
- Conectar `ws.onmessage(data => term.write(data))`.
- Usar `FitAddon` en load/resize.
- Theme oscuro AMOLED.

### Spike 3: Browser Harness

- Actualizar Playwright para cargar `/terminal`.
- Verificar que `xterm` aparece.
- Capturar bytes enviados para:
  - Enter -> `\r`
  - Backspace -> `\x7f`
  - Tab -> `\t`
  - Ctrl+C -> `\x03`
  - Ctrl+D -> `\x04`
  - Ctrl+L -> `\x0c`
- Verificar output ANSI con color.
- Verificar scrollback y copy behavior.

### Spike 4: Hardware Smoke

- Flashear.
- Conectar al AP.
- Medir tiempo de carga en movil.
- Probar login getty real.
- Probar `ls --color`, `clear`, `journalctl`, `top` y scroll.

## Go / No-Go

Go si:

- `idf.py build` sigue por debajo de particion con margen razonable.
- Playwright cubre input y ANSI.
- Movil carga la terminal en tiempo aceptable.
- No hay reboot ni heap pressure visible al servir assets.

No-Go si:

- El asset rompe el margen de particion.
- La carga en movil por AP es inaceptable sin gzip.
- El servidor HTTP no puede servir assets grandes de forma estable.
- El teclado movil con xterm no funciona de forma fiable.

## Conclusion

No conviene reinventar la rueda salvo que `xterm.js` falle en hardware real. La evaluacion inicial indica que es viable en tamano y funcionalidad. El siguiente paso correcto es un spike integrado con assets locales y Playwright antes de tocar mas UX manual.
