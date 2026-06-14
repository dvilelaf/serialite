# ESP32-KVM Feature Checklist

Fecha: 2026-06-14

Este checklist trata el proyecto como una consola serie de rescate sobre WiFi local. No es un KVM completo: no hay captura HDMI, HID remoto, BIOS video, virtual media ni power control.

Referencia UI/UX: ver `docs/ui-ux-design.md` para flujos operativos, reparto web/pantalla, estados visuales y requisitos recomendados para checklist.

## Must-Have

- [x] Terminal web robusta.
  Criterio: soporta ANSI básico, reconexión automática, copy/paste, latencia baja y no bloquea si el cliente o USB se atascan.
- [x] Backpressure y límites de buffers en el puente serial.
  Criterio: los buffers tienen tamaño acotado, contadores de drops y no provocan deadlocks.
- [x] Autenticación web obligatoria.
  Criterio: ninguna página sensible ni WebSocket de terminal funciona sin sesión autenticada.
- [x] Hash seguro de password web.
  Criterio: la password web se guarda con sal y KDF apropiado para ESP32; nunca en claro ni reversible.
- [x] Timeout de sesión web.
  Criterio: una sesión inactiva expira y debe autenticarse de nuevo.
- [x] Tokens de sesión aleatorios e invalidables.
  Criterio: los tokens dejan de funcionar tras logout, timeout, reboot o cambio de password.
- [x] Rate limit básico para login.
  Criterio: intentos fallidos repetidos quedan temporalmente bloqueados.
- [x] Rate limit para HTTP y WebSocket.
  Criterio: muchas conexiones o frames repetidos no agotan memoria ni bloquean la tarea web.
- [x] Límites y rate limit de entrada WebSocket.
  Criterio: frames de consola desde web tienen tamaño máximo y presupuesto por ventana antes de entrar al bridge.
- [x] AP protegido con WPA2.
  Criterio: no hay AP abierto por defecto.
- [x] Password AP no fija de fábrica.
  Criterio: si NVS no tiene configuración, se genera una password temporal legible.
- [x] Password AP de producción larga y única.
  Criterio: el modo persistente usa passphrase suficientemente larga y no compartida entre dispositivos.
- [x] Password web separada de la password WiFi.
  Criterio: el acceso al AP no implica acceso automático a la consola.
- [x] Protección CSRF en acciones mutadoras.
  Criterio: POST autenticado externo sin token válido no puede hacer logout, cambiar configuración ni abrir/cerrar escritura de terminal. El login pre-auth se protege con rate limit y password, no con CSRF.
- [x] Validación de `Origin` y `Host`.
  Criterio: WebSocket y endpoints sensibles rechazan orígenes/hosts inesperados.
- [x] Cookies de sesión seguras para entorno local.
  Criterio: cookies con `HttpOnly`, `SameSite` y alcance mínimo; no exponen token a JavaScript innecesariamente.
- [x] Control de escritura de consola.
  Criterio: solo un cliente web puede escribir; otros clientes son solo lectura o rechazados.
- [x] Web arranca en modo solo lectura tras login.
  Criterio: ninguna entrada de teclado o paste se envia al host hasta solicitar control de escritura.
- [x] Flujo explicito para solicitar y liberar escritura.
  Criterio: hay confirmacion de riesgo, un unico escritor y liberacion manual, por timeout o por logout.
- [x] Indicador persistente de modo terminal.
  Criterio: `Read-only`, `Write active`, `Writer busy`, `USB disconnected` y `Locked` son visibles sin abrir diagnostico.
- [x] No inyección automática en consola.
  Criterio: abrir terminal, reconectar o cargar la web no envía bytes al servidor sin acción explícita.
- [x] Banner de riesgo antes de escribir.
  Criterio: la UI web indica que escribir en la terminal equivale a acceso físico/privilegiado al servidor.
- [x] UI local informativa.
  Criterio: muestra SSID, password, URL local y estado general sin intentar ser terminal local.
- [x] Secretos en pantalla enmascarados por defecto.
  Criterio: passwords completos solo aparecen tras acción física y durante una ventana corta.
- [x] Presencia física para modo rescate.
  Criterio: una acción local puede requerirse para mostrar credenciales, habilitar terminal o ampliar ventana de acceso.
- [x] Estado de batería en pantalla.
  Criterio: muestra porcentaje/USB/carga si AXP2101 responde, o `--%` sin bloquear si no responde.
- [x] Estado USB visible en pantalla.
  Criterio: indica claramente si la consola USB está disponible.
- [x] Clientes conectados visibles en pantalla.
  Criterio: muestra al menos número de clientes WiFi/web activos.
- [x] Errores principales visibles en pantalla.
  Criterio: muestra fallo crítico de USB, WiFi, auth o memoria sin saturar la UI.
- [x] Watchdog operacional.
  Criterio: cubre tareas críticas y reinicia o recupera WiFi, WebSocket, USB bridge o loop principal si se cuelgan.
- [x] Logs internos en RAM.
  Criterio: guarda eventos recientes de boot, USB, WiFi, auth, WebSocket y resets sin desgaste de flash.
- [x] Logs internos seguros frente a concurrencia.
  Criterio: escrituras y snapshots desde varias tareas mantienen contadores, orden y retencion consistentes.
- [x] Logs de firmware no mezclados con la consola CDC de rescate.
  Criterio: USB-Serial/JTAG queda disponible para el bridge, pero no configurado como consola secundaria de ESP-IDF.
- [x] Modo reset/setup físico.
  Criterio: permite restaurar credenciales o entrar en setup sin reflashear.
- [x] Funcionamiento offline/local.
  Criterio: UI web, terminal y estado funcionan sin Internet, DNS externo ni backend.
- [x] Hardening HTTP/WebSocket.
  Criterio: límites de payload, validación de rutas, cierre limpio de sesiones y endpoints mínimos.
- [x] Fallo cerrado en seguridad.
  Criterio: fallo de auth, sesión, config corrupta o storage no concede acceso ni arranca AP abierto.
- [x] Configuración corrupta recuperable sin modo inseguro.
  Criterio: config inválida entra en setup seguro o requiere reset físico, nunca bypass de auth.
- [x] Factory reset seguro.
  Criterio: borra passwords, sesiones, logs sensibles y claves; credenciales antiguas dejan de funcionar.
- [x] Firmware firmado para actualizaciones.
  Criterio: imágenes no firmadas o firmadas con clave incorrecta se rechazan.
- [x] Política de build de producción.
  Criterio: documenta Secure Boot, Flash Encryption, logs por USB y debug/JTAG; producción no expone depuración innecesaria.
- [x] Documentación de límites.
  Criterio: README y UI explican que es consola serie de rescate, no KVM completo.

## Should-Have

- [x] Modo solo lectura.
  Criterio: permite observar salida serial sin riesgo de enviar bytes al host.
- [x] Buffer reciente de consola en RAM.
  Criterio: nuevos clientes pueden ver las últimas N KB sin usar flash intensivamente.
- [x] Página `/diagnostics`.
  Criterio: muestra uptime, heap libre, versión firmware, causa de reset, clientes, estado USB y contadores.
- [x] Exportar logs de diagnóstico.
  Criterio: descarga texto/JSON de eventos internos recientes desde la web autenticada.
- [x] Mejor terminal móvil.
  Criterio: botones web para `Esc`, `Ctrl+C`, `Ctrl+D`, `Tab`, flechas y `Enter`.
- [x] Web movil operable en rack.
  Criterio: terminal usable en movil con estado persistente, botones tactiles grandes y sin depender de hover.
- [x] Paste seguro.
  Criterio: el pegado grande se limita, trocea o confirma antes de enviarse a consola.
- [x] Timeout o activación física del AP.
  Criterio: puede limitar ventana de exposición WiFi o requerir botón para activar AP.
- [x] Emergency lock físico.
  Criterio: un botón o gesto local invalida sesiones, cierra WebSockets y opcionalmente apaga/bloquea el AP.
- [x] Rotación/generación controlada de credenciales.
  Criterio: permite renovar passwords y mostrar la nueva en pantalla local.
- [x] OTA local protegida.
  Criterio: actualización firmware local, manual, autenticada, firmada y con rollback si está disponible.
- [x] Endpoint `/about`.
  Criterio: muestra versión firmware, hash corto, estado de seguridad de build y sin secretos.
- [x] Auditoría visible en pantalla.
  Criterio: muestra cliente activo, terminal activa y estado lock/auth sin saturar la UI.
- [x] Pairing local inicial.
  Criterio: primer login o cambio crítico requiere código mostrado en pantalla o pulsación física.
- [x] Export/import de configuración seguro.
  Criterio: export no incluye secretos en claro; import valida versión/checksum y falla cerrado.
- [x] Tests de seguridad automatizados.
  Criterio: cubren auth requerida, CSRF/origin, rate limit, límites de payload y config corrupta.
- [x] Runbook integrado.
  Criterio: web incluye instrucciones cortas de uso y recuperación durante incidente.
- [x] Configuración de modo serial.
  Criterio: documenta o permite ajustar parámetros si se soportan transportes no CDC en el futuro.

## Nice-To-Have

- [x] QR en pantalla.
  Criterio: facilita abrir URL/AP desde móvil sin escribir datos largos.
- [x] Modo demo/simulador serial.
  Criterio: permite probar la web sin servidor conectado; no escribe al USB real y se auto-detiene si aparece USB.
- [x] mDNS local.
  Criterio: nombre local opcional sin depender de DNS externo.
- [x] HTTPS fingerprint policy gate.
  Criterio: helper probado para formatear SHA-256 y bloquear activacion salvo certificado, fingerprint visible localmente y confirmacion del operador.
- [x] HTTPS local runtime con fingerprint en pantalla.
  Criterio: TLS local opcional con listener real, fingerprint visible en AMOLED; no depende de CA externa ni induce a ignorar warnings.
- [x] Cliente WiFi opcional además de AP.
  Criterio: desactivado por defecto y documentado como aumento de superficie de ataque.
- [x] BLE provisioning policy gate.
  Criterio: helper probado, desactivado por defecto, exige presencia fisica, pairing local, NVS cifrado y ventanas de tiempo.
- [x] BLE provisioning runtime.
  Criterio: opcional; no reemplaza el flujo offline básico y no activa radio salvo durante setup fisico acotado.
- [x] Macros seguras.
  Criterio: desactivadas por defecto, visibles, nunca ejecutadas automáticamente y bloqueadas salvo estado seguro.
- [x] Carcasa/etiqueta operacional.
  Criterio: indica propósito, URL, botón de reset/setup y advertencias básicas.

## No-Go / Posponer

- [x] No implementar captura HDMI en esta placa.
- [x] No implementar power cycle del servidor sin hardware externo aislado y diseñado para ello.
- [x] No implementar virtual media o montaje de ISO en esta fase.
- [x] No exponer el ESP32 directamente a Internet.
- [x] No permitir escritura multiusuario simultánea.
- [x] No convertir la UI local AMOLED en una terminal completa.
- [x] No persistir secretos en claro.
- [x] No depender de servicios cloud.
- [x] No ejecutar comandos automáticos o funciones tipo AI ops.
- [x] No escribir logs intensivos en flash.
- [x] No implementar backdoor, recovery password universal ni credencial maestra.
- [x] No tratar SSID oculto como control de seguridad real.
- [x] No implementar IDS/IPS WiFi complejo en esta fase.
- [x] No intentar bloquear comandos Linux por análisis semántico.
- [x] No hacer OTA remota automática.
- [x] No almacenar transcripciones seriales completas por defecto.
- [x] No hacer MFA/TOTP obligatorio en el flujo base.

## Siguiente Bloque Recomendado

- [x] Autenticación web obligatoria.
- [x] Sesiones seguras, CSRF y validación de `Origin`.
- [x] Control de escritura único.
- [x] Modo solo lectura.
- [x] Secretos en pantalla enmascarados y presencia física.
- [x] Logs internos en RAM.
- [x] Página `/diagnostics`.
- [x] Límites anti-DoS HTTP/WebSocket.
