# Serialite UI/UX Design

Fecha: 2026-06-14

Este documento define la experiencia de usuario para una consola serie de rescate local basada en ESP32-S3 Touch AMOLED. El producto no es un KVM completo: no hay HDMI, HID, virtual media ni control de energia. La web es la interfaz operativa principal; la pantalla ESP32 es un panel local de estado y presencia fisica.

## Principios UI/UX

### 1. Emergencia antes que configuracion

La interfaz existe para momentos donde SSH, red o automatizacion han fallado. Debe reducir decisiones, no abrir ramas de configuracion. Cada pantalla debe responder rapido:

- donde conectarse;
- si el servidor esta conectado por USB;
- si hay alguien usando la consola;
- si el operador puede escribir de forma segura;
- como bloquear o cerrar la sesion.

### 2. Fallo cerrado y visible

Los estados inseguros o ambiguos no deben parecer normales. Si falla autenticacion, USB, memoria, WebSocket o configuracion, la UI debe mostrarlo con lenguaje directo y accionable. El sistema no debe ocultar fallos criticos tras iconos decorativos.

### 3. Sesion unica sin estados ambiguos

La consola de rescate debe operar como una terminal directa. La web no debe separar observadores y escritores ni pedir `take/release control`. Si hay una sesion web valida, esa sesion puede escribir. Si otro cliente inicia sesion, reemplaza la sesion anterior.

Entrar a la pagina o reconectar nunca debe enviar datos al servidor.

### 4. Presencia fisica como control de riesgo

El AP local y la pantalla en rack son parte del modelo de seguridad. Las acciones de alto riesgo pueden requerir presencia fisica, pero la pantalla pequena no debe pedir al operador que haga trabajo complejo. Buenas acciones locales:

- revelar credenciales durante una ventana corta;
- desbloquear una ventana de acceso;
- ejecutar bloqueo de emergencia;
- entrar en setup/reset seguro.

Malas acciones locales:

- escribir en la consola;
- cambiar passwords largos desde teclado tactil;
- gestionar sesiones en detalle;
- editar parametros avanzados.

### 5. Texto operativo, no marketing

El tono debe ser sobrio y especifico. Evitar mensajes como "Todo listo" si hay condiciones parciales. Preferir:

- `USB console connected`;
- `Web session active`;
- `Session replaced`;
- `Locked locally`;
- `Password hidden. Hold to reveal 30s`.

### 6. Minimalismo con jerarquia fuerte

La UI debe ser visualmente moderna, pero no ornamental. En emergencia, la estetica sirve para jerarquia:

- estado critico arriba;
- terminal como superficie dominante en web;
- diagnostico separado;
- colores reservados para severidad;
- animaciones solo para cambios de estado importantes.

## Flujos Principales

### Flujo 1: Operador llega al rack

1. Mira la pantalla ESP32.
2. Confirma que el dispositivo es `KVM`, que el AP esta activo y que USB esta conectado o desconectado.
3. Si la password esta oculta, hace una accion fisica para revelarla temporalmente o escanea un QR temporal si existe.
4. Conecta portatil o movil al AP `KVM`.
5. Abre `http://192.168.4.1/` o la URL mostrada.
6. Abre la sesion web local.

La pantalla debe dar suficiente informacion para empezar, pero no debe mostrar controles de consola.

### Flujo 2: Terminal operativa

1. Tras abrir sesion, la web muestra la terminal.
2. La terminal muestra salida reciente si hay buffer en RAM.
3. La sesion activa puede escribir directamente.
4. El operador se autentica en el login real de Linux/getty cuando aparezca.

Este debe ser el flujo por defecto para reducir pasos durante incidentes.

### Flujo 3: Reemplazo de sesion

1. Un segundo cliente abre sesion.
2. La sesion anterior queda invalidada.
3. La terminal anterior muestra expiracion/reconexion fallida.
4. La nueva sesion obtiene control.

La regla operativa es simple: si estas dentro, tienes la consola.

### Flujo 4: Diagnostico durante incidente

1. El operador abre `/diagnostics`.
2. Revisa USB, uptime, heap, version firmware, reset reason, clientes, contadores de drops y eventos recientes.
3. Exporta logs de diagnostico si necesita adjuntarlos.
4. Vuelve a la terminal sin perder contexto.

Diagnostico debe ser legible en movil y no mezclar secretos ni transcripcion completa por defecto.

### Flujo 5: Bloqueo o cierre

1. El operador pulsa `Lock terminal` o `Logout`.
2. La web invalida sesion y cierra WebSocket.
3. Si hay emergencia, una accion fisica local invalida sesiones, corta escritura y opcionalmente desactiva AP segun configuracion.
4. La pantalla muestra `Locked` y numero de clientes restantes.

El cierre debe ser mas facil que la configuracion.

## Arquitectura de Informacion Web

La web debe ser pequena, local y usable sin Internet. Propuesta de rutas:

### `/login`

Pantalla de autenticacion obligatoria.

Contenido:

- nombre del dispositivo;
- advertencia corta de alcance: `Serial rescue console. Not HDMI KVM.`;
- campo de password web;
- estado AP/USB no sensible;
- bloqueo temporal por rate limit;
- enlace a limites del producto o `/about` si esta permitido antes de login.

No debe mostrar secretos, logs ni terminal.

### `/`

Dashboard operativo compacto tras login.

Bloques:

- estado superior: USB, WiFi, clientes, sesion, bateria/alimentacion, lock;
- accion primaria: abrir terminal;
- accion secundaria: diagnostico;
- riesgo actual: sesion activa, sesion expirada, USB desconectado, lock local;
- instrucciones de emergencia de una linea.

En desktop puede ser una barra superior sobre la terminal. En movil debe aparecer como una tarjeta colapsable para dejar espacio vertical a la consola.

### `/terminal`

Superficie principal.

Elementos:

- terminal monoespaciada de alto contraste;
- indicador de stream/sesion y menu minimo;
- botones moviles para `Esc`, `Ctrl+C`, `Ctrl+D`, `Tab`, flechas y `Enter`;
- control de paste seguro con confirmacion para pegados grandes;
- copy claro y seleccion facil;
- indicador de reconexion WebSocket;
- aviso si el USB esta desconectado.

Reglas:

- modo inicial con control activo para la sesion valida;
- escribir requiere sesion valida y USB conectado;
- reconectar solo recupera terminal si la sesion sigue siendo la activa;
- ningun macro se ejecuta automaticamente;
- si el buffer descarta datos, mostrar contador discreto pero visible.

### `/diagnostics`

Pagina de estado tecnico.

Contenido:

- firmware version/hash corto;
- uptime;
- reset reason;
- heap libre/minimo;
- estado USB;
- estado WiFi/AP;
- clientes WiFi y web;
- escritor activo;
- contadores de bytes, drops, reconexiones, rate limits;
- eventos recientes en RAM;
- boton `Export diagnostics`.

No debe incluir passwords ni transcripciones completas por defecto.

### `/settings`

Solo si existe configuracion persistente segura. Debe ser conservadora y separada de la terminal.

Contenido permitido:

- rotar password web;
- rotar password AP;
- brillo/pantalla;
- timeout de sesion;
- activar/desactivar requisito de presencia fisica;
- factory reset seguro.

Cada accion mutadora requiere CSRF, sesion valida y confirmacion clara. Cambios criticos pueden requerir codigo mostrado en pantalla o accion fisica.

### `/about`

Informacion no sensible:

- que hace y que no hace el dispositivo;
- version firmware;
- estado de seguridad de build si esta disponible;
- instrucciones breves de uso;
- limites conocidos.

## Diseno de Pantalla ESP32

La pantalla AMOLED local debe ser un panel de orientacion para rack, no una consola. Debe funcionar en apaisado, con lectura rapida a poca distancia y bajo consumo.

### Layout recomendado

```text
+------------------------------------------------+
| KVM SERIAL RESCUE        USB OK     87% USB    |
|                                                |
| AP: KVM                         Clients: 2     |
| URL: http://192.168.4.1                         |
| Web: locked / auth required                     |
|                                                |
| WiFi password: hidden                           |
| Hold BOOT to reveal 30s                         |
|                                                |
| Terminal: session active / locked               |
|                                                |
| Last event: USB reconnected 00:14 ago           |
+------------------------------------------------+
```

### Informacion visible por defecto

- identificador del dispositivo;
- estado USB: conectado, desconectado, error;
- AP activo y SSID;
- URL local;
- bateria/alimentacion;
- numero de clientes;
- estado web: locked, auth required, active;
- estado terminal: session active, session expired, locked;
- ultimo evento importante.

### Secretos en pantalla

Passwords completas deben estar ocultas por defecto. Opciones aceptables:

- revelar password AP durante 30 segundos tras pulsacion larga fisica;
- mostrar solo sufijo de 4 caracteres para verificar que el operador tiene la credencial correcta;
- QR temporal con expiracion visual clara;
- pairing code corto para confirmar presencia fisica durante login o cambio critico.

No mostrar password web persistente en pantalla salvo flujo de setup inicial controlado.

### Interaccion local recomendada

La pantalla tactil, si se usa, debe limitarse a acciones de bajo texto:

- despertar pantalla;
- alternar vista estado/diagnostico resumido;
- revelar credenciales temporalmente con confirmacion fisica;
- bloquear terminal/sesiones;
- confirmar pairing;
- entrar en setup/reset con gesto largo y confirmacion.

El boton fisico debe tener prioridad para acciones de seguridad porque es mas dificil de activar por accidente desde la pantalla.

### Lo que no debe hacer la pantalla

- no renderizar terminal completa;
- no aceptar input de consola;
- no editar passwords complejas;
- no permitir escritura al host;
- no mostrar transcripciones seriales completas;
- no esconder un fallo critico bajo modo ahorro.

## Controles Web-Only vs Fisicos/Pantalla

### Web-only

- login web;
- terminal interactiva;
- solicitud/liberacion de escritura;
- copy/paste;
- teclas moviles especiales;
- diagnostico completo;
- export de logs;
- rotacion de credenciales;
- configuracion de timeouts;
- OTA local protegida si se implementa;
- lectura de runbook integrado.

Motivo: requieren texto, precision, contexto, confirmaciones y espacio visual.

### Fisicos o pantalla local

- revelar password AP temporal;
- mostrar URL/SSID/estado;
- pairing inicial o confirmacion de presencia;
- emergency lock;
- reset/setup seguro;
- despertar/apagar pantalla;
- vista de diagnostico resumido.

Motivo: prueban presencia fisica y siguen siendo operables cuando la web aun no esta disponible.

### Doble confirmacion recomendada

Acciones que deberian combinar web y presencia fisica segun modo de seguridad:

- primer login;
- cambio de password AP o web;
- desactivar requisito de presencia fisica;
- factory reset;
- habilitar escritura si el modo rescate lo exige;
- desbloquear tras emergency lock.

## Estados Visuales Criticos

Usar color con texto, no solo color. Reservar colores:

- Verde: conectado y sano.
- Azul: informacion o solo lectura.
- Amarillo: degradado, reconectando, buffer drops, bateria baja.
- Rojo: bloqueado, error critico, auth fallida, USB perdido si se esperaba conexion.
- Gris: desactivado o desconocido.

Estados obligatorios:

### `Auth required`

Web sin sesion. Terminal y WebSocket no disponibles.

### `Session active`

Cliente autenticado puede escribir en la consola.

### `Session expired`

La sesion fue reemplazada, cerro sesion o se invalido por emergency lock.

### `USB disconnected`

La terminal puede seguir visible, pero input debe estar deshabilitado o en cola solo si el diseno lo permite explicitamente. Recomendado: no encolar escritura cuando USB esta desconectado.

### `Reconnecting`

WebSocket caido o recuperandose. No debe parecer terminal activa.

### `Locked locally`

Emergency lock activado desde dispositivo. Invalidar sesiones y bloquear escritura hasta accion definida.

### `Degraded`

Heap bajo, drops de buffer, rate limit, watchdog recovery o config parcial. Mostrar diagnostico recomendado.

## Recomendaciones Responsive/Mobile

La web debe asumir que el operador puede usar un movil en rack.

- Priorizar terminal a pantalla completa en movil.
- Usar barra inferior fija para teclas especiales.
- Mantener `Stream OK`, `USB disconnected` y `Session expired` visibles.
- Evitar modales largos que tapen terminal durante incidentes.
- Hacer botones tactiles de al menos 44 px.
- Permitir zoom razonable sin romper layout.
- Usar fuente monoespaciada legible y tamano ajustable.
- Ofrecer contraste alto para salas con poca luz.
- Evitar dependencia de hover.
- Trocear o confirmar paste grande, especialmente en movil.
- Mantener diagnostico en tarjetas de una columna en pantallas pequenas.
- No esconder logout/lock en menus profundos.

En desktop, la vista ideal es terminal dominante con panel lateral compacto de estado y diagnostico resumido.

## Anti-Patrones a Evitar

- Convertir la pantalla AMOLED en terminal tactil.
- Mostrar passwords completas permanentemente.
- Unir password AP y password web.
- Abrir terminal con escritura activa por defecto.
- Ejecutar macros o comandos automaticos.
- Ocultar errores bajo iconos ambiguos.
- Depender de Internet, CDN, fuentes remotas o servicios cloud.
- Crear dashboards densos que compitan con la terminal.
- Poner controles destructivos cerca de teclas frecuentes.
- Permitir escritura multiusuario simultanea.
- Guardar transcripciones completas por defecto.
- Mostrar logs sensibles en pantalla local.
- Usar SSID oculto como sustituto de seguridad real.
- Hacer configuracion avanzada desde la pantalla pequena.
- Requerir lectura de documentacion externa durante un incidente.

## Cambios o Requisitos UI para el Checklist

Estos items deberian incorporarse o detallarse en `docs/feature-checklist.md`:

- [ ] Web arranca con sesion activa unica.
  Criterio: no hay botones de `take/release control`; el nuevo login invalida la sesion anterior.
- [ ] Indicador persistente de terminal.
  Criterio: stream, USB disconnected, session expired y locked son visibles sin abrir diagnostico.
- [ ] Flujo explicito de cierre/bloqueo.
  Criterio: logout y PWR emergency lock invalidan sesion y cierran WebSockets.
- [ ] Pantalla local con secretos ocultos por defecto.
  Criterio: password completa o QR solo aparece tras accion fisica y con expiracion visible.
- [ ] Emergency lock accesible fisicamente.
  Criterio: invalida sesiones, cierra WebSockets y bloquea escritura de forma visible en web y pantalla.
- [ ] Pantalla local muestra auditoria minima.
  Criterio: clientes conectados, escritor activo y ultimo evento critico sin mostrar transcripcion.
- [ ] Web movil operable en rack.
  Criterio: terminal usable en movil con teclas especiales, botones tactiles y estado persistente.
- [ ] Paste seguro con confirmacion.
  Criterio: pegados grandes o multilinea requieren confirmacion o troceo antes de enviarse.
- [ ] Diagnostico exportable sin secretos.
  Criterio: export incluye estado y eventos recientes, pero no passwords ni transcripcion completa.
- [ ] Runbook corto integrado.
  Criterio: web explica en una pantalla como conectar, observar, tomar escritura y bloquear.
