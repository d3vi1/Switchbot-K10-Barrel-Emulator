# Architecture overview (C-based emulator)

## Goals

- Emulate a SwitchBot K10+ dock ("barrel") over BLE on Linux/BlueZ.
- Provide a stable control surface for tests and Cockpit UI via D-Bus.
- Persist config in `/etc` and allow runtime updates through D-Bus.
- Keep runtime dependencies minimal (C, GLib, D-Bus, BlueZ).
- Support SELinux and systemd hardening on Fedora.

## Runtime components

### Daemon

`k10-barrel-emulatord` is the primary process. It:

- Registers a GATT server over BlueZ D-Bus.
- Registers a LE advertisement with manufacturer/service data.
- Logs all GATT traffic and state changes to journald.
- Exposes a control API over D-Bus (see below).

Planned entry points:

- `src/daemon/main.c` -> `main()`
- `src/daemon/daemon.c` -> `k10_daemon_run()`

### Control CLI

`k10-barrel-emulatorctl` is a thin D-Bus client for local scripting. It:

- Calls `StartAdvertising`, `StopAdvertising`, and config methods.
- Prints daemon status for diagnostics.

Planned entry points:

- `src/cli/main.c` -> `main()`

### Cockpit plugin

A Cockpit plugin provides UI control for runtime config edits and status. It:

- Uses the D-Bus interface defined below.
- Never edits files directly; changes go through the daemon.

## BLE behavior

The emulator presents two "faces" as a single BLE Peripheral:

1) **App-facing dock control**
   - Service UUID: `CBA20D00-224D-11E6-9FB8-0002A5D5C51B`
   - Write characteristic: `CBA20002-224D-11E6-9FB8-0002A5D5C51B`
   - Notify characteristic: `CBA20003-224D-11E6-9FB8-0002A5D5C51B`

2) **Sweeper-facing robot↔dock link (placeholder)**
   - Service UUID: `B000` (16-bit)
   - Characteristics: `B001`, `B002`, `B003`, `B004` (16-bit)

The sweeper-facing characteristics are placeholders to capture traffic and will
be implemented iteratively as real protocol frames are observed.

Planned BLE code paths:

- `src/ble/advertising.c` -> `k10_adv_register()`
- `src/ble/gatt_app.c` -> `k10_gatt_register()`
- `src/ble/chrc_dock.c` -> `k10_chrc_dock_write()` / `k10_chrc_dock_notify()`
- `src/ble/chrc_sweeper.c` -> `k10_chrc_sweeper_write()`

## D-Bus API

Bus name:

- `ro.vilt.SwitchbotBleEmulator`

Object path:

- `/ro/vilt/SwitchbotBleEmulator`

Interfaces:

- `com.switchbot.SwitchbotBleEmulator.SweeperMini`
- `com.switchbot.SwitchbotBleEmulator.SweeperMiniBarrel`
- `com.switchbot.SwitchbotBleEmulator.Config`

### Config interface

`com.switchbot.SwitchbotBleEmulator.Config` is responsible for reading and
writing config values. Writes **must** persist to `/etc` and trigger a live
reload when possible.

Planned methods:

- `GetAll() -> a{sv}`
- `Get(s key) -> v`
- `Set(s key, v value) -> b` (returns success)
- `SetAll(a{sv} values) -> b`
- `Reload() -> b`

Planned signals:

- `ConfigChanged(a{sv} values)`

Planned code paths:

- `src/dbus/config_iface.c` -> `k10_dbus_config_get()` / `k10_dbus_config_set()`

### Control interface

Control methods live on the SweeperMini/SweeperMiniBarrel interfaces to keep
compatibility with the Cockpit UI and scripting use cases.

Planned methods:

- `StartAdvertising() -> b`
- `StopAdvertising() -> b`
- `GetStatus() -> a{sv}`

Planned signals:

- `StatusChanged(a{sv} status)`

Planned code paths:

- `src/dbus/control_iface.c` -> `k10_dbus_start_adv()` / `k10_dbus_stop_adv()`

## Config file

- Path: `/etc/k10-barrel-emulator/config.toml`
- Must be writable by the daemon to support runtime updates.

Planned config keys (initial set):

- `adapter` (string, e.g. `"hci0"`)
- `local_name` (string)
- `company_id` (integer, hex allowed)
- `manufacturer_mac_label` (string, hex bytes) or empty for auto
- `service_uuids` (array of strings)
- `fd3d_service_data_hex` (string hex)
- `include_tx_power` (bool)
- `fw_major` / `fw_minor` (int)

Planned code paths:

- `src/config/config.c` -> `k10_config_load()` / `k10_config_save()`

## Logging

- Use journald via `sd-journal` APIs.
- Tag: `k10-barrel-emulator`

Planned code paths:

- `src/log/log.c` -> `k10_log_info()` / `k10_log_error()`

## SELinux + hardening

- Provide a policy module allowing D-Bus access and BlueZ GATT operations.
- Ship policy sources in `selinux/` and an install script.
- Apply systemd hardening (e.g. `NoNewPrivileges=true`, `ProtectSystem=strict`)
  while preserving access to `/etc/k10-barrel-emulator/config.toml`.

Planned code paths:

- `selinux/k10-barrel-emulator.te`
- `packaging/systemd/k10-barrel-emulator.service`

## Packaging

- Provide an RPM spec in `packaging/rpm/`.
- CI builds artifacts and publishes them on GitHub releases.

