# Architecture overview (C-based emulator)

## Goals

- Emulate a SwitchBot K10+ dock ("barrel") over BLE on Linux/BlueZ.
- Provide a stable control surface for tests and Cockpit UI via D-Bus.
- Persist config in `/etc` and allow runtime updates through D-Bus.
- Keep runtime dependencies minimal (C, systemd sd-bus/journald, BlueZ).
- Support SELinux and systemd hardening on Fedora.

## Runtime components

### Daemon

`k10-barrel-emulatord` is the primary process. It:

- Registers a GATT server over BlueZ D-Bus.
- Registers a LE advertisement with manufacturer/service data.
- Logs all GATT traffic and state changes to journald.
- Exposes a control API over D-Bus (see below).

Entry points:

- `src/daemon/main.c` -> `main()`
- `src/daemon/daemon.c` -> `k10_daemon_run()`
- `src/config/config.c` -> `k10_config_load()` / `k10_config_save()`
- `src/dbus/dbus.c` -> `k10_dbus_run()` / `k10_method_start()` / `k10_method_set_config()`
- `src/ble/advertising.c` -> `k10_adv_start()` / `k10_adv_stop()`
- `src/log/log.c` -> `k10_log_info()` / `k10_log_error()`

### Directory layout

- `src/daemon/` (lifecycle, systemd integration)
- `src/ble/` (BlueZ D-Bus: advertising + GATT)
- `src/dbus/` (public control API)
- `src/config/` (TOML load/save)
- `src/log/` (journald helpers)
- `src/cli/` (D-Bus client)
- `include/` (public and internal headers)
- `docs/` (protocol + architecture notes)
- `packaging/` (systemd, RPM)
- `selinux/` (policy sources)

### Control CLI

`k10-barrel-emulatorctl` is a thin D-Bus client for local scripting. It:

- Calls `StartAdvertising`, `StopAdvertising`, and config methods.
- Prints daemon status for diagnostics.

Entry points:

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

### Object tree

The daemon exposes one object path and multiple interfaces on the same object.
This keeps Cockpit and CLI implementations straightforward.

```
/ro/vilt/SwitchbotBleEmulator
  com.switchbot.SwitchbotBleEmulator.SweeperMini
  com.switchbot.SwitchbotBleEmulator.SweeperMiniBarrel
  com.switchbot.SwitchbotBleEmulator.Config
```

### Config interface

`com.switchbot.SwitchbotBleEmulator.Config` is responsible for reading and
writing config values. Writes **must** persist to `/etc` and trigger a live
reload when possible.

Methods:

- `GetConfig() -> a{sv}` (entire config)
- `SetConfig(a{sv} values) -> b` (batch update)
- `Reload() -> b` (re-read file)

Signals:

- `ConfigChanged(a{sv} values)`

Code paths:

- `src/dbus/dbus.c` -> `k10_method_get_config()` / `k10_method_set_config()`

### Control interface

Control methods live on the SweeperMini/SweeperMiniBarrel interfaces to keep
compatibility with the Cockpit UI and scripting use cases.

Methods:

- `Start() -> b`
- `Stop() -> b`
- `Reload() -> b` (re-read config)
- `GetStatus() -> a{sv}` (includes mode/adapter/running/advertising)

Signals:

- `StatusChanged(a{sv} status)`

Code paths:

- `src/dbus/dbus.c` -> `k10_method_start()` / `k10_method_stop()`

## Config file

- Path: `/etc/k10-barrel-emulator/config.toml`
- Must be writable by the daemon to support runtime updates.

Config keys (initial set):

- `adapter` (string, e.g. `"hci0"`)
- `local_name` (string)
- `company_id` (integer, hex allowed)
- `manufacturer_mac_label` (string, hex bytes) or empty for auto
- `sweeper_mfg_suffix` (string, hex bytes, appended after MAC; SEQ inserted automatically)
- `barrel_mfg_suffix` (string, hex bytes, appended after MAC; SEQ inserted automatically)
- `service_uuids` (array of strings)
- `fd3d_service_data_hex` (string hex)
- `sweeper_fd3d_service_data_hex` (string hex; overrides `fd3d_service_data_hex` in sweeper mode when set)
- `barrel_fd3d_service_data_hex` (string hex; overrides `fd3d_service_data_hex` in barrel mode when set)
- `include_tx_power` (bool)
- `fw_major` / `fw_minor` (int)

Config keys are exposed one-for-one over D-Bus. `Set()` must validate types,
persist to the file, and trigger a non-destructive reload (or a full restart if
required by BlueZ).

Code paths:

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

Planned systemd unit location:

- `packaging/systemd/k10-barrel-emulator.service`

Planned code paths:

- `selinux/k10-barrel-emulator.te`
- `packaging/systemd/k10-barrel-emulator.service`

## Packaging

- Provide an RPM spec in `packaging/rpm/`.
- CI builds artifacts and publishes them on GitHub releases.
