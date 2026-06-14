# BLE provisioning policy

BLE provisioning is an optional future channel and is disabled by default. The production-safe baseline remains the offline AP flow displayed on the AMOLED screen.

This project must not enable BLE just to make setup easier. BLE adds a radio surface that is attractive during incident response because operators may leave devices powered in racks for long periods.

BLE provisioning may only be enabled by a future implementation when all gates pass:

- explicit operator request;
- physical presence at the device;
- successful local pairing flow;
- encrypted NVS for any persisted secrets;
- the offline AP flow remains available;
- advertising is time-boxed to at most 180 seconds;
- the provisioning session is time-boxed to at most 600 seconds.

The runtime component is also fail-closed. `CONFIG_ESP32_KVM_BLE_PROVISIONING_ENABLE` is disabled by default. When enabled, startup still evaluates the policy gates before any radio callback can run. The current firmware does not include a BLE radio backend, so the integrated runtime cannot advertise services or persist credentials accidentally.

Any future backend must be wired only behind `ble_provisioning_runtime_start()`, must keep the offline AP flow available, and must stop advertising when the bounded setup window expires.
