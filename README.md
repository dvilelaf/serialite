# esp32-kvm

Consola de rescate para servidores Linux headless sobre ESP32-S3, ESP-IDF 5.x, LVGL 9.x, TinyUSB, `esp_http_server` y FreeRTOS.

Este repositorio empieza con una fase de diseño y scaffold mínimo. No contiene todavía la implementación del firmware.

## Estado actual

- Repo creado.
- Estructura base preparada para los módulos del firmware.
- Documento de arquitectura y plan en `docs/architecture-and-plan.md`.
- Scaffold ESP-IDF compilable para `esp32s3`.
- Pruebas host para validación de configuración y ring buffer.

## Alcance de la fase 1

- Crear un punto de acceso WiFi propio.
- Exponer una interfaz web local con estado.
- Leer y escribir por USB CDC ACM sin bloquear.
- Mostrar estado y últimas líneas en la pantalla LVGL.
- Mantener toda la comunicación asíncrona mediante colas FreeRTOS.

## Siguiente paso

Revisar y aprobar el plan antes de empezar el scaffold de firmware.

## Verificacion

Con ESP-IDF en el entorno:

```bash
source /path/to/esp-idf/export.sh
./scripts/verify.sh
```

Comandos equivalentes:

```bash
cmake -S tests/host -B build-host-tests -G Ninja
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
idf.py build
```

Para flashear:

```bash
source /path/to/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash monitor
```

## Seguridad actual

- No hay credenciales AP compartidas por defecto.
- Si no hay configuracion valida en NVS, el firmware genera SSID/PSK efimeros por arranque.
- `storage_save_config()` rechaza guardar credenciales WiFi si `CONFIG_NVS_ENCRYPTION` no esta activo.
- El servidor HTTP solo arranca si el AP arranco correctamente.
