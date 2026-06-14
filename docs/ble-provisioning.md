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

The policy component does not start a BLE stack, advertise services, persist credentials, or replace web/AP setup. It exists to keep any future BLE implementation fail-closed and testable.
