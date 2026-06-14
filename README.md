# esp32-kvm

Consola de rescate para servidores Linux headless sobre Waveshare ESP32-S3 Touch AMOLED, ESP-IDF 5.x y LVGL 9.x.

## Estado actual

- AP WiFi propio con DHCP en `192.168.4.1`.
- Credenciales AP efímeras si NVS no contiene configuración válida.
- Pantalla SH8601 inicializada con LVGL 9.5.
- Táctil FT5x06 inicializado con gating por `INT`.
- UI local con estado, password local, terminal, teclado virtual y botones rápidos.
- Servidor HTTP con página de estado y terminal web en `/terminal`.
- WebSocket en `/ws`.
- `terminal_bridge` para fan-in/fan-out entre USB, UI local y web.
- USB implementado con el transporte oficial USB-Serial/JTAG del ESP32-S3.
- Host tests para configuración y ring buffer.

## Seguridad

- No hay password AP fija de fábrica.
- La password efímera no se escribe en logs.
- La password se muestra solo en pantalla local.
- Si la password es efímera y la pantalla no inicializa, el AP no arranca.
- `storage_save_config()` rechaza guardar credenciales WiFi si `CONFIG_NVS_ENCRYPTION` no está activo.

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
2. Lee en la AMOLED el SSID `ESP32-KVM-...` y la password.
3. Conéctate al AP desde móvil o portátil.
4. Abre `http://192.168.4.1`.
5. Usa `/terminal` para la terminal web.

## Nota USB

La implementación actual usa USB-Serial/JTAG como CDC device. Es la ruta que hace que el servidor Linux vea un `/dev/ttyACM*` al conectar el ESP32-S3. En producción conviene reducir o redirigir logs para que no se mezclen con la consola de rescate.
