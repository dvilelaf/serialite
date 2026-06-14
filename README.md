# esp32-kvm

Consola de rescate para servidores Linux headless sobre Waveshare ESP32-S3 Touch AMOLED, ESP-IDF 5.x y LVGL 9.x.

## Estado actual

- AP WiFi propio con DHCP en `192.168.4.1`.
- Credenciales AP efímeras si NVS no contiene configuración válida.
- Autenticación web obligatoria con password temporal separada de la password WiFi.
- Terminal web en modo solo lectura por defecto; escritura requiere tomar control explícitamente.
- Pantalla SH8601 inicializada con LVGL 9.5.
- Táctil FT5x06 no se inicializa en la UI actual porque la pantalla local es solo informativa.
- UI local apaisada, oscura y solo informativa con batería, estado AP, SSID, passwords ocultas por defecto y URL.
- La pantalla local muestra estado USB, clientes WiFi/web y drops del bridge sin renderizar logs.
- La pantalla se apaga tras 3 minutos y se reactiva con el botón BOOT/GPIO0.
- Mantener `BOOT` durante 3 segundos invalida sesiones web, libera escritura activa y cierra WebSockets.
- Mantener `BOOT` durante 10 segundos borra la configuración NVS del proyecto y reinicia con credenciales efímeras nuevas.
- Servidor HTTP con página de estado y terminal web en `/terminal`.
- Terminal web móvil con barra de estado fija, estados `Read-only`/`Write active`/`Writer busy`/`USB disconnected`/`Locked` y teclas táctiles rápidas.
- Endpoint autenticado `/about` con versión, límites del producto y resumen de seguridad local.
- WebSocket en `/ws`.
- Diagnóstico local autenticado en `/diagnostics` y export JSON en `/diagnostics.json`.
- Log circular de eventos en RAM para auth, WebSocket, backpressure y estado operativo.
- Task watchdog explícito para tareas críticas propias (`usb_rx`, `usb_tx`, `web_tx`, `lvgl_ui`).
- `terminal_bridge` para fan-in/fan-out entre USB y terminal web.
- Buffer reciente de consola en RAM para que nuevos clientes vean contexto inicial sin escribir transcripciones en flash.
- USB implementado con el transporte oficial USB-Serial/JTAG del ESP32-S3.
- Host tests para configuración, ring buffer, seguridad web, política de input, diagnóstico y secretos.

## Seguridad

- No hay password AP fija de fábrica.
- La password efímera usa cuatro palabras inglesas aleatorias y no se escribe en logs.
- Las credenciales WiFi persistentes exigen una passphrase de producción de al menos 20 bytes.
- La password WiFi y la password web efímeras usan cuatro palabras inglesas aleatorias y no se escriben en logs.
- La password web es distinta de la password WiFi.
- La password web se deriva con sal y PBKDF2-HMAC-SHA256 antes de validar login; no se guarda en claro en el estado de auth.
- La terminal WebSocket exige sesión autenticada y valida `Origin`.
- La escritura hacia la consola exige lock explícito de un único cliente.
- La entrada WebSocket tiene límite por frame y presupuesto por ventana para evitar DoS básico.
- Las rutas HTTP están limitadas a endpoints conocidos con métodos y tamaños de body esperados.
- Si el servicio web/auth falla tras arrancar WiFi, el AP se apaga para no dejar una red expuesta sin consola segura.
- El paste web grande o multilínea requiere confirmación y se trocea antes de enviarse.
- Los logs internos no almacenan passwords ni transcripción serial completa.
- Las passwords se muestran solo en pantalla local tras presencia física y durante una ventana temporal.
- Si la password es efímera y la pantalla no inicializa, el AP no arranca.
- `storage_save_config()` rechaza guardar credenciales WiFi si `CONFIG_NVS_ENCRYPTION` no está activo.
- Los buffers temporales de credenciales se borran explícitamente tras ser copiados por WiFi, UI o autenticación web.
- Si la configuración persistente está corrupta o incompleta, el firmware no la usa: regenera credenciales efímeras y exige exposición por pantalla local para continuar.

Para despliegues reales, ver [`docs/production-hardening.md`](docs/production-hardening.md). Un firmware sin Secure Boot, Flash Encryption, NVS Encryption para secretos persistentes y JTAG/debug cerrado debe considerarse build de laboratorio, no producción.

## Verificación

```bash
source /home/david/esp-idf/export.sh
./scripts/verify.sh
```

Esto ejecuta pruebas host y `idf.py build`.

## Flasheo

```bash
source /home/david/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash monitor
```

Si `idf_monitor` no tiene TTY:

```bash
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor
```

## Uso

1. Flashea la placa.
2. Lee en la AMOLED el SSID `KVM`.
3. Pulsa `BOOT` con la pantalla encendida para revelar durante 30 segundos la password WiFi y la password web temporal.
4. Conéctate al AP desde móvil o portátil.
5. Abre `http://192.168.4.1`.
6. Autentica con la password web.
7. Usa `/terminal`; por defecto es solo lectura hasta pulsar `Request write`. La barra superior muestra si puedes escribir, si otro cliente tiene el lock o si USB está desconectado.
8. Usa `/diagnostics` para estado técnico y eventos recientes sin secretos.
9. Mantén `BOOT` durante 3 segundos para cortar sesiones web si pierdes control operacional.
10. Mantén `BOOT` durante 10 segundos para factory reset si necesitas regenerar credenciales.

## Nota USB

La implementación actual usa USB-Serial/JTAG como CDC device. Es la ruta que hace que el servidor Linux vea un `/dev/ttyACM*` al conectar el ESP32-S3. La consola secundaria USB-Serial/JTAG de ESP-IDF está desactivada para no mezclar logs de firmware con la consola de rescate; el driver USB-Serial/JTAG sigue habilitado para el bridge.

## Límites

Este proyecto es una consola serie local de rescate. No implementa captura HDMI, teclado HID remoto, power cycle, virtual media, automatización de comandos, acceso por Internet ni funciones cloud. Estos límites son deliberados para reducir superficie de ataque.
