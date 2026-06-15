# esp32-kvm

Consola de rescate para servidores Linux headless sobre Waveshare ESP32-S3 Touch AMOLED, ESP-IDF 5.x y LVGL 9.x.

## Estado actual

- AP WiFi propio con DHCP en `192.168.4.1`.
- Publica `http://kvm.local` por mDNS cuando el cliente lo soporta; `http://192.168.4.1` sigue siendo el fallback operativo.
- Credenciales AP efímeras si NVS no contiene configuración válida.
- Sesión web local sin password adicional; la frontera inicial es WPA2 fuerte + presencia física + autenticación Linux en la consola.
- Terminal web con una única sesión activa; el usuario conectado tiene control de escritura automáticamente.
- Pantalla SH8601 inicializada con LVGL 9.5.
- Táctil FT5x06 no se inicializa en la UI actual porque la pantalla local es solo informativa.
- UI local apaisada, oscura y solo informativa con batería, estado AP, SSID, WiFi password oculta por defecto y URL.
- QR local en pantalla para conectarse al AP WiFi; no contiene tokens de sesión.
- La pantalla local muestra estado USB, clientes WiFi/web, sesión/input y drops del bridge sin renderizar logs.
- La pantalla se apaga tras 3 minutos y se reactiva con el botón BOOT/GPIO0.
- El AP se apaga automáticamente tras 10 minutos sin clientes WiFi ni clientes web.
- Mantener `BOOT` durante 3 segundos invalida la sesión web y cierra WebSockets.
- Mantener `BOOT` durante 10 segundos borra la configuración NVS del proyecto y reinicia con credenciales efímeras nuevas.
- Servidor HTTP con página de estado y terminal web en `/terminal`.
- Terminal web móvil fullscreen con HUD flotante, estado de stream y teclas táctiles rápidas.
- La vista web filtra secuencias ANSI/OSC básicas para que logs de arranque con color o control de cursor no ensucien el terminal.
- Endpoint autenticado `/about` con versión, límites del producto y resumen de seguridad local.
- WebSocket en `/ws`.
- Diagnóstico local autenticado en `/diagnostics` y export JSON en `/diagnostics.json`.
- Actualización firmware local en `/ota`: subida manual autenticada, protegida por CSRF/Origin, validada por ESP-IDF OTA y con reboot explícito.
- Rotación local de credenciales en `/credentials`: genera nueva WiFi password human-readable, no la devuelve por HTTP, la muestra en AMOLED y exige NVS Encryption para persistir WiFi.
- Export/import local de configuración no secreta en `/config`: JSON autenticado con schema y checksum; nunca exporta passwords, hashes ni salts.
- Log circular de eventos en RAM para auth, WebSocket, backpressure y estado operativo.
- Task watchdog explícito para tareas críticas propias (`usb_rx`, `usb_tx`, `web_tx`, `lvgl_ui`).
- `terminal_bridge` para fan-in/fan-out entre USB y terminal web.
- Buffer reciente de consola en RAM para que nuevos clientes vean contexto inicial sin escribir transcripciones en flash.
- USB implementado con el transporte oficial USB-Serial/JTAG del ESP32-S3.
- Tabla de particiones con dos slots OTA (`ota_0`/`ota_1`) para rollback.
- Host tests para configuración, ring buffer, seguridad web, política de input, diagnóstico y secretos.

## Seguridad

- No hay password AP fija de fábrica.
- La password WiFi efímera usa seis palabras inglesas aleatorias y no se escribe en logs.
- Las credenciales WiFi persistentes exigen una passphrase de producción de al menos 20 bytes.
- La terminal WebSocket exige sesión autenticada y valida `Origin`.
- El login web crea una sesión local aleatoria `HttpOnly`/`SameSite=Strict`; un nuevo login reemplaza la sesión anterior.
- La escritura hacia la consola exige una sesión web válida y única.
- La entrada WebSocket tiene límite por frame y presupuesto por ventana para evitar DoS básico.
- Las rutas HTTP están limitadas a endpoints conocidos con métodos y tamaños de body esperados.
- La OTA local requiere sesión web, CSRF, `Origin` válido, tamaño compatible con slot OTA y no reinicia automáticamente tras la subida.
- La rotación de credenciales requiere sesión web, CSRF, `Origin` válido, pantalla local operativa y NVS Encryption para persistencia.
- La exportación de configuración omite secretos; la importación valida schema/checksum, conserva passwords existentes y falla si no puede guardar de forma segura.
- Si el servicio web/auth falla tras arrancar WiFi, el AP se apaga para no dejar una red expuesta sin consola segura.
- El paste web grande o multilínea requiere confirmación y se trocea antes de enviarse.
- Los logs internos no almacenan passwords ni transcripción serial completa.
- Las passwords se muestran solo en pantalla local tras presencia física y durante una ventana temporal.
- El QR de pantalla contiene solo la URL local, nunca credenciales.
- Si la password es efímera y la pantalla no inicializa, el AP no arranca.
- `storage_save_config()` rechaza guardar credenciales WiFi si `CONFIG_NVS_ENCRYPTION` no está activo.
- Los buffers temporales de credenciales se borran explícitamente tras ser copiados por WiFi o UI.
- Si la configuración persistente está corrupta o incompleta, el firmware no la usa: regenera credenciales efímeras y exige exposición por pantalla local para continuar.

Para despliegues reales, ver [`docs/production-hardening.md`](docs/production-hardening.md). Un firmware sin Secure Boot, Flash Encryption, NVS Encryption para secretos persistentes y JTAG/debug cerrado debe considerarse build de laboratorio, no producción. El perfil de laboratorio no quema eFuses; el perfil `sdkconfig.prod.defaults` exige clave privada externa al repo para builds firmados.

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
3. Pulsa `BOOT` con la pantalla encendida para revelar durante 30 segundos la password WiFi.
4. Conéctate al AP desde móvil o portátil.
5. Escanea el QR de pantalla o abre `http://kvm.local` si tu sistema soporta mDNS, o `http://192.168.4.1`.
6. Pulsa `Open console`; autentícate después en el login real de Linux si el getty lo solicita.
7. Usa `/terminal`; la sesión web activa puede escribir directamente en la consola.
8. Usa `/diagnostics` para estado técnico y eventos recientes sin secretos.
9. Usa `/credentials` para rotar la WiFi password. La respuesta web no contiene secretos: pulsa `BOOT` y lee la nueva password en la AMOLED.
10. Tras rotar credenciales, reinicia desde `/credentials` para aplicar la nueva WiFi password.
11. Usa `/config` para exportar o importar configuración no secreta. La importación requiere reboot para aplicar cambios de AP.
12. Usa `/ota` solo para cargar una imagen completa `.bin` generada por ESP-IDF para esta placa. En producción debe estar firmada con la clave configurada en `sdkconfig.prod.defaults`.
13. Tras una OTA aceptada, pulsa `Reboot to pending image` cuando estés listo para reiniciar el bridge.
14. Mantén `BOOT` durante 3 segundos para cortar sesiones web si pierdes control operacional.
15. Mantén `BOOT` durante 10 segundos para factory reset si necesitas regenerar credenciales.

## Nota USB

La implementación actual usa USB-Serial/JTAG como CDC device. Es la ruta que hace que el servidor Linux vea un `/dev/ttyACM*` al conectar el ESP32-S3. La consola secundaria USB-Serial/JTAG de ESP-IDF está desactivada para no mezclar logs de firmware con la consola de rescate; el driver USB-Serial/JTAG sigue habilitado para el bridge.

## Límites

Este proyecto es una consola serie local de rescate. No implementa captura HDMI, teclado HID remoto, power cycle, virtual media, automatización de comandos, acceso por Internet ni funciones cloud. Estos límites son deliberados para reducir superficie de ataque.
