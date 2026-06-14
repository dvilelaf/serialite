# ESP32-KVM Architecture and Plan

Fecha: 2026-06-14

## Objetivo

Construir una consola de rescate para servidores Linux headless con una placa Waveshare ESP32-S3 Touch AMOLED.

El sistema debe:

- crear un AP WiFi propio;
- exponer una web local para estado y terminal;
- actuar como puente bidireccional entre USB CDC ACM, la terminal local LVGL y la terminal web;
- mostrar estado básico del sistema en la pantalla AMOLED;
- guardar configuración básica en NVS.

## Decisión de alcance

El proyecto es viable en ESP32-S3 si se trata como una consola de rescate local, no como un KVM completo.

La primera versión debe limitarse a:

1. AP WiFi + HTTP/WebSocket.
2. Bridge USB CDC ACM.
3. UI LVGL como monitor y terminal simple.
4. Persistencia mínima.

La función de vídeo o captura de pantalla no entra en este proyecto.

## Arquitectura general

```text
Servidor Linux
    ^
    | USB CDC ACM
    v
ESP32-S3
    ^
    |
WiFi AP
    |
Móvil / Portátil
```

El firmware se organiza en módulos con responsabilidades separadas:

- `board/platform`: inicialización de placa, reloj, pantalla, táctil, USB, power y storage.
- `core`: estado global del sistema y encaminamiento de eventos.
- `usb_console`: lectura/escritura CDC ACM y detección de conexión.
- `wifi_ap`: AP, DHCP y configuración de red.
- `web_server`: HTTP, WebSocket y página de estado/terminal.
- `terminal_bridge`: fan-out/fan-in entre USB, web y UI.
- `lvgl_ui`: pantalla principal, terminal local, botones rápidos y teclado.
- `storage`: NVS, configuración y registro histórico básico.

## Regla de oro de concurrencia

LVGL debe tener un único propietario:

- solo la tarea de UI llama a LVGL;
- USB y web nunca tocan widgets directamente;
- cualquier evento externo entra por cola FreeRTOS;
- la UI consume eventos y decide qué renderizar.

Esto evita bloqueos y carreras entre el stack web, USB y el renderer.

## Diseño de tareas FreeRTOS

### 1. `core_task`

Responsabilidad:

- mantener el estado de alto nivel;
- recibir eventos de USB, web, UI y storage;
- distribuir comandos a los módulos correctos.

### 2. `usb_task`

Responsabilidad:

- inicializar CDC ACM;
- leer sin bloquear;
- escribir desde buffer de salida;
- publicar eventos de conexión y datos recibidos.

### 3. `web_task`

Responsabilidad:

- servir la UI web;
- mantener WebSocket para terminal;
- convertir mensajes de clientes en comandos del core;
- emitir telemetría de estado.

### 4. `ui_task`

Responsabilidad:

- inicializar LVGL;
- pintar estado WiFi, USB y últimas líneas;
- procesar input táctil;
- enviar teclas rápidas al bridge;
- renderizar terminal local.

### 5. `storage_task` o acceso sincronizado a NVS

Responsabilidad:

- leer y escribir configuración;
- persistir brillo, SSID, contraseña e historial mínimo.

## Flujo de datos

### Entrada desde USB

1. `usb_task` recibe bytes del CDC ACM.
2. Publica un evento en `core_task`.
3. `core_task` reenvía el dato a:
   - `ui_task` para pintar terminal;
   - `web_task` para fans out por WebSocket;
   - métricas y contadores.

### Entrada desde web

1. Un cliente WebSocket envía datos.
2. `web_task` convierte el mensaje a evento.
3. `core_task` lo manda al `usb_task`.

### Entrada desde UI local

1. Un botón rápido, teclado o gesto táctil genera evento.
2. `ui_task` lo manda al `core_task`.
3. `core_task` lo convierte en bytes hacia `usb_task`.

## Interfaces públicas previstas

### `usb_console`

- `usb_console_init()`
- `usb_console_start()`
- `usb_console_write()`
- `usb_console_get_state()`

### `wifi_ap`

- `wifi_ap_init()`
- `wifi_ap_start()`
- `wifi_ap_get_status()`
- `wifi_ap_set_config()`

### `web_server`

- `web_server_init()`
- `web_server_start()`
- `web_server_broadcast_terminal()`
- `web_server_get_clients()`

### `terminal_bridge`

- `terminal_bridge_init()`
- `terminal_bridge_push_rx()`
- `terminal_bridge_push_tx()`
- `terminal_bridge_next_event()`

### `lvgl_ui`

- `lvgl_ui_init()`
- `lvgl_ui_start()`
- `lvgl_ui_set_status()`
- `lvgl_ui_append_terminal_line()`
- `lvgl_ui_enqueue_key()`

### `storage`

- `storage_init()`
- `storage_load_config()`
- `storage_save_config()`
- `storage_append_history()`

## Persistencia

En NVS se guardará:

- SSID;
- contraseña WPA2;
- brillo;
- tamaño de fuente;
- historial mínimo de conexiones.

La persistencia no debe bloquear el flujo de consola.

## Robustez

Requisitos de diseño:

- reconexión automática cuando el USB desaparece;
- buffers limitados y cola con backpressure;
- watchdog habilitado;
- logging detallado;
- no perder el control de la UI aunque falle el USB o la red.

## Decisiones de plataforma actuales

### USB

La implementación actual usa el periférico oficial USB-Serial/JTAG del ESP32-S3 como CDC device.

Motivo:

- el caso `/dev/ttyACM0` en el servidor Linux implica que el ESP actúa como dispositivo USB;
- la placa ya expone este puerto para flasheo y monitor;
- evita requerir que el servidor Linux actúe como USB gadget.

Riesgo:

- los logs por USB pueden mezclarse con la consola de rescate.
- antes de producción debe bajarse el nivel de logs o redirigirlos si se usa el mismo CDC como consola.

### RAM

PSRAM queda desactivada por defecto.

Motivo:

- el hardware probado reiniciaba con `PSRAM ID read error`;
- LVGL usa buffers parciales DMA en RAM interna, no framebuffer completo.

### Web

`esp_http_server` se usará como control plane y transporte terminal.

La web no renderiza la UI completa del firmware, solo estado y terminal.

## Validación mínima

Antes de ampliar funciones, se debe probar:

1. AP levanta con IP por defecto `192.168.4.1`.
2. Un cliente web puede abrir el estado.
3. USB recibe y reenvía bytes.
4. LVGL muestra estado y últimas líneas.
5. El sistema no bloquea con buffers llenos.

## Verificacion de desarrollo

El repo debe conservar un entrypoint reproducible:

```bash
source /path/to/esp-idf/export.sh
./scripts/verify.sh
```

Este comando ejecuta:

- pruebas host de componentes puros;
- build ESP-IDF para `esp32s3`.

## Politica de secretos

No se deben introducir credenciales compartidas de fabrica.

Mientras NVS encryption no este activa, el firmware puede generar credenciales WiFi efimeras, pero no debe persistir secretos en NVS. La persistencia de SSID/password solo queda permitida con almacenamiento cifrado configurado.

## Fases propuestas

### Fase 1

- crear AP WiFi: implementado;
- servidor web con estado: implementado;
- WebSocket terminal: implementado;
- bridge USB CDC ACM via USB-Serial/JTAG: implementado;
- pantalla LVGL de monitor: implementado;
- terminal local LVGL básica: implementado;
- persistencia básica en NVS.

### Fase 2

- terminal LVGL interactiva;
- scrollback configurable;
- teclado virtual y botones rápidos;
- mejoras de UX.

### Fase 3

- historial de conexiones;
- endurecimiento;
- diagnósticos;
- acciones de rescate más completas.

## Estructura de directorios

```text
esp32-kvm/
  README.md
  .gitignore
  docs/
    architecture-and-plan.md
  main/
    app_main.c
  components/
    usb_console/
    wifi_ap/
    web_server/
    terminal_bridge/
    lvgl_ui/
    storage/
```

## Criterio de aprobación del plan

El plan queda aprobado si:

- el alcance de fase 1 cabe en un firmware pequeño y verificable;
- LVGL tiene un único dueño;
- USB, web y UI se conectan solo por colas/eventos;
- no se presupone framebuffer completo ni KVM de vídeo;
- el repo arranca con documentación clara antes del código.
