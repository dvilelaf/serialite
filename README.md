# esp32-kvm

Consola de rescate para servidores Linux headless sobre ESP32-S3, ESP-IDF 5.x, LVGL 9.x, TinyUSB, `esp_http_server` y FreeRTOS.

Este repositorio empieza con una fase de diseño y scaffold mínimo. No contiene todavía la implementación del firmware.

## Estado actual

- Repo creado.
- Estructura base preparada para los módulos del firmware.
- Documento de arquitectura y plan en `docs/architecture-and-plan.md`.

## Alcance de la fase 1

- Crear un punto de acceso WiFi propio.
- Exponer una interfaz web local con estado y terminal WebSocket.
- Leer y escribir por USB CDC ACM sin bloquear.
- Mostrar estado y últimas líneas en la pantalla LVGL.
- Mantener toda la comunicación asíncrona mediante colas FreeRTOS.

## Siguiente paso

Revisar y aprobar el plan antes de empezar el scaffold de firmware.
