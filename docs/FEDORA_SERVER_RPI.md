# Fedora Server on Raspberry Pi (headless) — flashing + SSH

This emulator is happiest on Linux/BlueZ. Fedora Server on Raspberry Pi works well, and you can prepare it without a keyboard/HDMI by injecting an SSH key at flash time.

## What you need

- A Linux machine to flash the SD cards (Fedora is easiest).
- `arm-image-installer`
- Two SD cards (one per Pi)
- Your SSH public key (example: `~/.ssh/id_ed25519.pub`)

## 1) Install `arm-image-installer`

On Fedora:

```bash
sudo dnf install -y arm-image-installer xz
```

## 2) Download a Fedora Server image for aarch64

Download a Fedora Server raw image for **aarch64** (Raspberry Pi 3/4/5 are 64-bit capable).

You should end up with a file like:

`Fedora-Server-<VERSION>.aarch64.raw.xz`

## 3) Flash SD card + inject SSH key

1) Identify the SD card block device:

```bash
lsblk
```

2) Flash (example for Raspberry Pi 4):

```bash
sudo arm-image-installer \
  --image=Fedora-Server-<VERSION>.aarch64.raw.xz \
  --target=rpi4 \
  --media=/dev/sdX \
  --resizefs \
  --addkey ~/.ssh/id_ed25519.pub
```

Notes:
- Replace `/dev/sdX` with the correct device (not a partition like `/dev/sdX1`).
- Run `arm-image-installer --help` to confirm the correct `--target` for your Pi model.

Repeat for the second SD card.

## 4) Boot and SSH in

1) Plug ethernet in (simplest for first boot) and power the Pi.
2) In UniFi, find the new client and set a DHCP reservation.
3) SSH in (try these in order):

```bash
ssh root@<pi-ip>
ssh fedora@<pi-ip>
```

## 5) Set hostname (recommended)

Once you can SSH in:

```bash
sudo hostnamectl set-hostname rPI1
```

Repeat for `rPI2`.

## 6) Deploy the emulator with Ansible

Back on your dev machine:

1) Update `ansible/inventories/dev/hosts.yml` with the Pi IPs and SSH user (`root` or `fedora`).
2) Run:

```bash
cd ansible
ansible-playbook playbooks/site.yml
```
