# ESP32-KVM

Consola serie de rescate para servidores Linux sin pantalla.

El ESP32-KVM se conecta por USB al servidor y crea una WiFi local propia. Desde un portátil o móvil puedes abrir una terminal web y usar la consola serie del servidor aunque SSH o la red estén rotos.

No es un KVM de vídeo: no hay HDMI, teclado HID remoto ni virtual media. Es una consola serie de emergencia.

## Uso Rápido

1. Conecta el ESP32-KVM al servidor por USB.

2. Conecta tu portátil o móvil a la WiFi:

   ```text
   SSID: KVM
   Password: mostrada en la pantalla del ESP32
   ```

3. Abre:

   ```text
   http://192.168.4.1
   ```

4. Usa la terminal.

Si la terminal queda vacía, el servidor probablemente no tiene consola serie activa. En el servidor, habilítala una vez:

```bash
sudo systemctl enable --now serial-getty@ttyACM0.service
```

Después refresca la web.

## Botones

- `BOOT`: despierta la pantalla y revela temporalmente la password WiFi.
- `PWR` 3 segundos: bloquea o desbloquea el input de la terminal.
- `BOOT` 10 segundos: factory reset de la configuración del ESP32-KVM.

## Web UI

- La pantalla principal es la terminal.
- El botón `+` abre acciones rápidas: teclas útiles, ajustar tamaño de TTY, diagnostics, rotar WiFi y emergency lock.
- `Emergency lock` bloquea el input web. Para desbloquear, mantén `PWR` 3 segundos.
- `Rotate WiFi` genera una nueva password, la muestra en la pantalla del ESP32 y reinicia el AP.

## Seguridad Operativa

- La WiFi usa WPA2 y una password generada por el dispositivo.
- Solo se permite un cliente WiFi.
- Solo hay una sesión web activa.
- No hay password web adicional: la autenticación real ocurre en la consola Linux.
- Los secretos no se devuelven por HTTP; se muestran físicamente en la pantalla del dispositivo.

## Desarrollo

Verificar:

```bash
source /home/david/esp-idf/export.sh
./scripts/verify.sh
```

Compilar y flashear:

```bash
source /home/david/esp-idf/export.sh
idf.py -p /dev/ttyACM0 flash
```

Si el flasheo falla por permisos o ruido USB, reenchufa la placa y usa un baudrate bajo:

```bash
idf.py -p /dev/ttyACM0 -b 115200 flash
```

## Producción

Para despliegues reales, revisa:

- [`docs/production-hardening.md`](docs/production-hardening.md)
- [`docs/security-test-matrix.md`](docs/security-test-matrix.md)
