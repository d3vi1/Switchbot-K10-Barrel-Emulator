# SwitchBot K10 Barrel (Dock) Emulator — Linux + Raspberry Pi

This repository contains:

- An Ansible bootstrap to prepare Raspberry Pi hosts (`rPI1`, `rPI2`) for Bluetooth LE development.
- A Linux (BlueZ) **dock/barrel emulator** that can advertise and expose a GATT server for:
  - **App-facing** control characteristics (`CBA20002` write, `CBA20003` notify/write).
  - **Sweeper-facing** discovery channels (`B001..B004`) as a starting point for capturing the on-device robot↔dock link.

The goal is to produce a debugging environment that can:

- Log all GATT reads/writes/subscriptions.
- Emit minimal “plausible” responses (e.g. `GetInfoResponse`) to keep clients talking.
- Eventually support MITM / relay setups (Pi acts as dock for the sweeper while relaying to a real dock).

## Quick start (Ansible)

1) Edit `ansible/inventories/dev/hosts.yml` and set `ansible_host` / `ansible_user` for `rPI1` and `rPI2`.

2) Run:

```bash
cd ansible
ansible-playbook playbooks/site.yml
```

This installs BlueZ + Python deps, deploys the emulator to `/opt/switchbot-k10-barrel-emulator`, writes config to `/etc/switchbot-k10-barrel-emulator/config.yml`, and enables `k10-barrel-emulator.service`.

### OS support

- Debian family (Raspberry Pi OS, Ubuntu): supported
- Fedora Server (os_family `RedHat`): supported (package names differ; handled automatically)

## Running / logs

- Service: `sudo systemctl status k10-barrel-emulator`
- Logs: `sudo journalctl -u k10-barrel-emulator -f`
- HCI monitor (optional): `sudo btmon`

## Notes

- macOS CoreBluetooth heavily restricts custom advertising fields; Linux/BlueZ is preferred for faithful emulation.
- Everything committed to git must be in English.

## Fedora Server on Raspberry Pi (headless)

See `docs/FEDORA_SERVER_RPI.md`.
