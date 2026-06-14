# Politica de produccion y limites operativos

Fecha: 2026-06-14

Este documento define el minimo aceptable para tratar un build de `esp32-kvm` como apto para despliegue controlado en servidores reales. El firmware actual es una consola serie de rescate local; no es un KVM completo.

## Perfiles de build

### Laboratorio

Uso permitido:

- desarrollo local;
- validacion de pantalla, AP, login y puente serie;
- pruebas con servidores no criticos o bancos de test.

Configuracion aceptable:

- Secure Boot desactivado;
- Flash Encryption desactivado;
- logs de firmware habilitados por UART;
- JTAG/USB debug disponible;
- passwords efimeras mostrables tras presencia fisica.

Este perfil no debe instalarse en granjas de servidores ni dejarse conectado a hosts sensibles.

### Produccion

Uso permitido:

- consola serie de rescate en hosts Linux headless;
- acceso local por AP WiFi durante incidentes;
- diagnostico offline sin dependencias cloud.

Requisitos de configuracion:

- Secure Boot v2 habilitado.
- Flash Encryption habilitado en modo release.
- NVS Encryption habilitado antes de persistir credenciales.
- Tabla de particiones OTA con `otadata`, `ota_0` y `ota_1` para rollback.
- JTAG de produccion deshabilitado o protegido por eFuse.
- Consola secundaria por USB-Serial/JTAG deshabilitada.
- Nivel de log reducido; no emitir secretos ni transcripciones seriales.
- Claves de firma y cifrado generadas fuera del repo y custodiadas por el operador.
- `sdkconfig` de produccion versionado o reproducible mediante CI.
- Binario firmado y verificado antes de flashear.
- El perfil `sdkconfig.prod.defaults` debe usarse solo en CI/build de produccion con clave privada externa al repo.

Un build que no cumpla estos puntos debe considerarse build de laboratorio aunque compile y funcione.

## Firma y actualizaciones

El firmware usa dos slots OTA en `partitions.csv`. Esto permite instalar una imagen nueva sin destruir la imagen anterior y habilita rollback del bootloader cuando `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`.

La interfaz `/ota` acepta una imagen de aplicación completa mediante POST local autenticado. El upload exige sesión web, CSRF, `Origin` válido, tamaño no superior al slot OTA y una sola actualización en curso. Tras `esp_ota_end()` y `esp_ota_set_boot_partition()` correctos, el dispositivo queda en estado `pending reboot`; el reinicio se hace con `/api/reboot` de forma explícita. En builds de producción, la validación de firma la aplica ESP-IDF/Secure Boot durante `esp_ota_end()` y el arranque posterior.

El perfil de laboratorio no activa Secure Boot ni Flash Encryption para evitar quemar eFuses durante desarrollo. El perfil de produccion esta separado en `sdkconfig.prod.defaults` y exige:

- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`;
- `CONFIG_SECURE_BOOT=y`;
- `CONFIG_SECURE_BOOT_V2_ENABLED=y`;
- `CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y`;
- `CONFIG_SECURE_SIGNED_APPS=y`;
- `CONFIG_SECURE_SIGNED_ON_UPDATE=y`;
- `CONFIG_SECURE_FLASH_ENC_ENABLED=y`;
- `CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE=y`;
- `CONFIG_NVS_ENCRYPTION=y`;
- clave de firma externa absoluta en `/run/secrets/esp32-kvm/prod-kvm-signing-key.pem`.

La ruta de la clave debe estar fuera del checkout y gestionada por la infraestructura de CI o por un secreto montado en runtime. No se aceptan claves de ejemplo, rutas relativas al repo ni claves compartidas entre entornos.

## Politica de secretos

- La password WiFi y la password web deben ser distintas.
- La password WiFi persistente debe tener al menos 8 palabras human-readable, 20 bytes o mas, y ser unica por dispositivo.
- No debe existir password maestra, recovery password universal ni backdoor.
- Las passwords efimeras solo se muestran en pantalla local tras presencia fisica.
- El primer login web debe exigir pairing local de un solo uso mostrado en pantalla o equivalente de presencia fisica.
- El pairing local debe tener bloqueo por intentos fallidos para impedir fuerza bruta si la password web ya fue comprometida.
- La rotacion de credenciales debe mostrarse solo en pantalla local; la interfaz web puede iniciar la accion pero no devuelve passwords.
- La password web debe validarse mediante KDF con sal; no debe almacenarse reversible.
- La password web rotada debe persistir como hash+sal en NVS cifrado, nunca como plaintext.
- Credenciales persistentes solo son aceptables con NVS Encryption activo.
- Export/import de configuracion solo puede transportar campos no secretos con schema/checksum; passwords, hashes y salts no deben exportarse.
- El QR visible en pantalla solo debe codificar la URL local; no debe incluir passwords, pair code, tokens ni configuracion sensible.
- Los buffers temporales con passwords deben limpiarse cuando WiFi, UI o auth ya hayan tomado su propia copia.
- Configuracion persistente corrupta o incompleta no debe reutilizarse ni degradar a AP abierto; debe entrar en setup fisico con credenciales nuevas.
- El AP debe limitar su ventana de exposicion; el firmware de base lo apaga tras 10 minutos sin clientes WiFi ni WebSocket.
- Los logs de RAM deben redactar eventos sensibles antes de exponerse por web.

## Superficie USB

La implementacion actual usa USB-Serial/JTAG como CDC device. Esto permite que el servidor Linux vea el ESP32 como `/dev/ttyACM*`.

Requisitos:

- ESP-IDF no debe usar USB-Serial/JTAG como consola secundaria de logs.
- El bridge USB debe ser el unico consumidor operativo del canal CDC.
- El firmware no debe enviar bytes automaticos al host al arrancar, reconectar o abrir la web.
- Backpressure y drops deben ser visibles en diagnostico.

Limitacion conocida: el dispositivo actua como consola serie CDC. No captura video, no emula teclado HID y no proporciona virtual media.

## Superficie WiFi y web

Requisitos:

- AP WPA2, nunca abierto por defecto.
- IP local por defecto `192.168.4.1`.
- Web local autenticada antes de exponer terminal, diagnostico o WebSocket.
- Si auth/web no arranca, el AP debe apagarse o no iniciarse.
- Sesiones con timeout e invalidacion.
- CSRF en acciones mutadoras.
- Validacion de `Origin` y `Host` en WebSocket y rutas sensibles.
- Escritura de terminal en modo single-writer.
- Terminal en solo lectura tras login hasta solicitar control explicitamente.
- Rate limits y limites de payload para login, HTTP y WebSocket.
- Cliente WiFi/STA desactivado por defecto. Si se implementa en runtime, requiere presencia fisica, aceptacion explicita del riesgo, NVS cifrado y mantener el AP como ruta de recuperacion.

Limitacion conocida: HTTP local no equivale a transporte confidencial frente a un atacante ya conectado al AP. Para entornos con amenaza WiFi elevada, usar aislamiento fisico, ventana AP corta o TLS local con fingerprint operativo.

## Despliegue fisico

Requisitos:

- Etiquetar el dispositivo como consola serie privilegiada.
- Documentar el host al que esta conectado.
- Evitar conectarlo a puertos USB de hosts fuera de alcance operativo.
- Verificar que el servidor no expone una shell sin control de acceso salvo que ese sea el objetivo explicito de rescate.
- Mantener el dispositivo en zona fisica controlada.

## Limites del producto

`esp32-kvm` no implementa:

- captura HDMI o video BIOS;
- HID remoto;
- power cycle del servidor;
- virtual media o montaje ISO;
- acceso remoto por Internet;
- automatizacion de comandos;
- IDS/IPS WiFi;
- almacenamiento de transcripciones completas en flash;
- funciones cloud.

Estas limitaciones son deliberadas para reducir superficie de ataque y mantener el producto como consola serie local de rescate.

## Criterio de aceptacion para produccion

Antes de declarar un build apto para produccion:

1. `./scripts/verify.sh` debe pasar.
2. `git diff --check` no debe reportar errores.
3. El firmware debe flashear y enumerar como `/dev/ttyACM*`.
4. El AP `KVM` debe aparecer protegido por WPA2.
5. Login, WebSocket, read-only y write-lock deben validarse manualmente.
6. `/diagnostics` y `/about` no deben exponer secretos.
7. Secure Boot, Flash Encryption y NVS Encryption deben estar activos en el perfil de produccion.
8. JTAG/debug deben estar deshabilitados o justificados por una excepcion documentada.
