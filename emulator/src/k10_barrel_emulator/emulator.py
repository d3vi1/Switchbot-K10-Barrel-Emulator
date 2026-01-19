from __future__ import annotations

import os
import signal
import sys
from typing import Any

import dbus
import dbus.exceptions
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

from .config import EmulatorConfig
from .hexutil import hex_encode, mac_bytes_to_text


BLUEZ_SERVICE_NAME = "org.bluez"
DBUS_OM_IFACE = "org.freedesktop.DBus.ObjectManager"
DBUS_PROP_IFACE = "org.freedesktop.DBus.Properties"

GATT_MANAGER_IFACE = "org.bluez.GattManager1"
LE_ADVERTISING_MANAGER_IFACE = "org.bluez.LEAdvertisingManager1"

GATT_SERVICE_IFACE = "org.bluez.GattService1"
GATT_CHRC_IFACE = "org.bluez.GattCharacteristic1"
LE_ADVERTISEMENT_IFACE = "org.bluez.LEAdvertisement1"


def log(message: str) -> None:
    print(f"[k10-emulator] {message}", flush=True)


def _best_effort_host_mac_label() -> bytes:
    # Stable-ish default for debugging: use hostname bytes (padded/truncated) as a 6B label.
    host = os.uname().nodename.encode("utf-8", errors="ignore")
    padded = (host + b"\x00" * 6)[:6]
    return padded


def find_adapter(bus: dbus.Bus, *, preferred: str | None) -> str:
    obj = bus.get_object(BLUEZ_SERVICE_NAME, "/")
    om = dbus.Interface(obj, DBUS_OM_IFACE)
    objects = om.GetManagedObjects()

    preferred_path = f"/org/bluez/{preferred}" if preferred else None
    if preferred_path and preferred_path in objects:
        if "org.bluez.Adapter1" in objects[preferred_path]:
            return preferred_path

    for path, ifaces in objects.items():
        if "org.bluez.Adapter1" in ifaces:
            return path

    raise RuntimeError("No Bluetooth adapter found (org.bluez.Adapter1)")


class Advertisement(dbus.service.Object):
    PATH_BASE = "/org/bluez/k10_barrel_emulator/advertisement"

    def __init__(self, bus: dbus.Bus, index: int, config: EmulatorConfig) -> None:
        self.bus = bus
        self.path = f"{self.PATH_BASE}{index}"
        self.ad_type = "peripheral"
        self.local_name = config.local_name

        self.service_uuids = config.advertise_service_uuids or []

        mac_label = config.manufacturer_mac_label or _best_effort_host_mac_label()
        self.manufacturer_data: dict[dbus.UInt16, dbus.Array] = {
            dbus.UInt16(config.company_id): dbus.Array(mac_label, signature="y"),
        }

        self.service_data: dict[str, dbus.Array] | None = None
        if config.advertise_fd3d_service_data:
            # UUID16 in Service Data AD type 0x16.
            self.service_data = {
                "FD3D": dbus.Array(config.fd3d_service_data, signature="y"),
            }

        self.include_tx_power = config.include_tx_power

        super().__init__(bus, self.path)

    def get_path(self) -> dbus.ObjectPath:
        return dbus.ObjectPath(self.path)

    def get_properties(self) -> dict[str, dict[str, Any]]:
        props: dict[str, Any] = {
            "Type": dbus.String(self.ad_type),
            "LocalName": dbus.String(self.local_name),
            "ServiceUUIDs": dbus.Array(self.service_uuids, signature="s"),
            "ManufacturerData": dbus.Dictionary(self.manufacturer_data, signature="qv"),
        }

        if self.service_data is not None:
            props["ServiceData"] = dbus.Dictionary(self.service_data, signature="sv")

        if self.include_tx_power:
            props["Includes"] = dbus.Array(["tx-power"], signature="s")

        return {LE_ADVERTISEMENT_IFACE: props}

    @dbus.service.method(DBUS_PROP_IFACE, in_signature="ss", out_signature="v")
    def Get(self, interface: str, prop: str) -> Any:
        properties = self.get_properties().get(interface, {})
        if prop not in properties:
            raise dbus.exceptions.DBusException(f"Unknown property {prop}")
        return properties[prop]

    @dbus.service.method(DBUS_PROP_IFACE, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface: str) -> dict[str, Any]:
        return self.get_properties().get(interface, {})

    @dbus.service.method(DBUS_PROP_IFACE, in_signature="ssv")
    def Set(self, interface: str, prop: str, value: Any) -> None:
        raise dbus.exceptions.DBusException("Read-only properties")

    @dbus.service.method(LE_ADVERTISEMENT_IFACE, in_signature="", out_signature="")
    def Release(self) -> None:
        log("advertisement: released by BlueZ")


class Application(dbus.service.Object):
    PATH = "/org/bluez/k10_barrel_emulator"

    def __init__(self, bus: dbus.Bus) -> None:
        self.bus = bus
        self.path = self.PATH
        self.services: list[Service] = []
        super().__init__(bus, self.path)

    def add_service(self, service: "Service") -> None:
        self.services.append(service)

    def get_path(self) -> dbus.ObjectPath:
        return dbus.ObjectPath(self.path)

    @dbus.service.method(DBUS_OM_IFACE, out_signature="a{oa{sa{sv}}}")
    def GetManagedObjects(self) -> dict[dbus.ObjectPath, dict[str, dict[str, Any]]]:
        response: dict[dbus.ObjectPath, dict[str, dict[str, Any]]] = {}
        for service in self.services:
            response[service.get_path()] = service.get_properties()[GATT_SERVICE_IFACE]
            for chrc in service.characteristics:
                response[chrc.get_path()] = chrc.get_properties()[GATT_CHRC_IFACE]
        return response


class Service(dbus.service.Object):
    def __init__(self, bus: dbus.Bus, index: int, uuid: str, primary: bool) -> None:
        self.bus = bus
        self.path = f"/org/bluez/k10_barrel_emulator/service{index}"
        self.uuid = uuid
        self.primary = primary
        self.characteristics: list[Characteristic] = []
        super().__init__(bus, self.path)

    def get_path(self) -> dbus.ObjectPath:
        return dbus.ObjectPath(self.path)

    def add_characteristic(self, characteristic: "Characteristic") -> None:
        self.characteristics.append(characteristic)

    def get_properties(self) -> dict[str, dict[str, Any]]:
        return {
            GATT_SERVICE_IFACE: {
                "UUID": dbus.String(self.uuid),
                "Primary": dbus.Boolean(self.primary),
                "Characteristics": dbus.Array([c.get_path() for c in self.characteristics], signature="o"),
            }
        }

    @dbus.service.method(DBUS_PROP_IFACE, in_signature="ss", out_signature="v")
    def Get(self, interface: str, prop: str) -> Any:
        properties = self.get_properties().get(interface, {})
        if prop not in properties:
            raise dbus.exceptions.DBusException(f"Unknown property {prop}")
        return properties[prop]

    @dbus.service.method(DBUS_PROP_IFACE, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface: str) -> dict[str, Any]:
        return self.get_properties().get(interface, {})

    @dbus.service.method(DBUS_PROP_IFACE, in_signature="ssv")
    def Set(self, interface: str, prop: str, value: Any) -> None:
        raise dbus.exceptions.DBusException("Read-only properties")


class Characteristic(dbus.service.Object):
    def __init__(self, bus: dbus.Bus, index: int, uuid: str, flags: list[str], service: Service) -> None:
        self.bus = bus
        self.service = service
        self.path = f"{service.path}/char{index}"
        self.uuid = uuid
        self.flags = flags
        self.value: bytes = b""
        self.notifying = False
        super().__init__(bus, self.path)

    def get_path(self) -> dbus.ObjectPath:
        return dbus.ObjectPath(self.path)

    def get_properties(self) -> dict[str, dict[str, Any]]:
        return {
            GATT_CHRC_IFACE: {
                "Service": self.service.get_path(),
                "UUID": dbus.String(self.uuid),
                "Flags": dbus.Array(self.flags, signature="s"),
                "Value": dbus.Array(self.value, signature="y"),
            }
        }

    def _properties_changed(self) -> None:
        self.PropertiesChanged(
            GATT_CHRC_IFACE,
            {"Value": dbus.Array(self.value, signature="y")},
            [],
        )

    @dbus.service.method(DBUS_PROP_IFACE, in_signature="ss", out_signature="v")
    def Get(self, interface: str, prop: str) -> Any:
        properties = self.get_properties().get(interface, {})
        if prop not in properties:
            raise dbus.exceptions.DBusException(f"Unknown property {prop}")
        return properties[prop]

    @dbus.service.method(DBUS_PROP_IFACE, in_signature="s", out_signature="a{sv}")
    def GetAll(self, interface: str) -> dict[str, Any]:
        return self.get_properties().get(interface, {})

    @dbus.service.method(DBUS_PROP_IFACE, in_signature="ssv")
    def Set(self, interface: str, prop: str, value: Any) -> None:
        raise dbus.exceptions.DBusException("Read-only properties")

    @dbus.service.method(GATT_CHRC_IFACE, in_signature="a{sv}", out_signature="ay")
    def ReadValue(self, options: dict[str, Any]) -> dbus.Array:
        log(f"gatt: read uuid={self.uuid}")
        return dbus.Array(self.value, signature="y")

    @dbus.service.method(GATT_CHRC_IFACE, in_signature="aya{sv}", out_signature="")
    def WriteValue(self, value: dbus.Array, options: dict[str, Any]) -> None:
        data = bytes(value)
        dev = options.get("device")
        dev_s = str(dev) if dev is not None else "?"
        log(f"gatt: write uuid={self.uuid} dev={dev_s} len={len(data)} hex={hex_encode(data)}")

    @dbus.service.method(GATT_CHRC_IFACE, in_signature="", out_signature="")
    def StartNotify(self) -> None:
        self.notifying = True
        log(f"gatt: notify start uuid={self.uuid}")

    @dbus.service.method(GATT_CHRC_IFACE, in_signature="", out_signature="")
    def StopNotify(self) -> None:
        self.notifying = False
        log(f"gatt: notify stop uuid={self.uuid}")

    @dbus.service.signal(DBUS_PROP_IFACE, signature="sa{sv}as")
    def PropertiesChanged(self, interface: str, changed: dict[str, Any], invalidated: list[str]) -> None:
        # Implemented by dbus-python; body intentionally empty.
        ...


class DockWriteCharacteristic(Characteristic):
    def __init__(self, bus: dbus.Bus, index: int, service: Service, notify_char: "DockNotifyCharacteristic", cfg: EmulatorConfig) -> None:
        super().__init__(bus, index, "CBA20002-224D-11E6-9FB8-0002A5D5C51B", ["write", "write-without-response"], service)
        self.notify_char = notify_char
        self.cfg = cfg

    @dbus.service.method(GATT_CHRC_IFACE, in_signature="aya{sv}", out_signature="")
    def WriteValue(self, value: dbus.Array, options: dict[str, Any]) -> None:
        data = bytes(value)
        dev = options.get("device")
        dev_s = str(dev) if dev is not None else "?"

        extra = ""
        if data == bytes([0x57, 0x01, 0x00]):
            extra = " (GetInfo)"

        log(f"dock: write uuid=CBA20002 dev={dev_s} len={len(data)} hex={hex_encode(data)}{extra}")

        if data == bytes([0x57, 0x01, 0x00]):
            self.notify_char.send_get_info_response(self.cfg)


class DockNotifyCharacteristic(Characteristic):
    def __init__(self, bus: dbus.Bus, index: int, service: Service) -> None:
        super().__init__(bus, index, "CBA20003-224D-11E6-9FB8-0002A5D5C51B", ["notify", "write", "write-without-response"], service)

    def send_get_info_response(self, cfg: EmulatorConfig) -> None:
        if not self.notifying:
            log("dock: getInfoResponse skipped (no subscribers)")
            return

        major = cfg.fw_major & 0xFF
        minor = cfg.fw_minor & 0xFF
        payload = bytes([0x57, 0x81, 0x00, major, minor, 0x00])
        self.value = payload
        self._properties_changed()
        log(f"dock: notify getInfoResponse hex={hex_encode(payload)} (fw {major}.{minor})")


def build_services(bus: dbus.Bus, cfg: EmulatorConfig) -> Application:
    app = Application(bus)

    # App-facing service: CBA20D00... with CBA20002 (write) and CBA20003 (notify/write).
    dock_service = Service(bus, 0, "CBA20D00-224D-11E6-9FB8-0002A5D5C51B", True)
    dock_notify = DockNotifyCharacteristic(bus, 0, dock_service)
    dock_write = DockWriteCharacteristic(bus, 1, dock_service, dock_notify, cfg)
    dock_service.add_characteristic(dock_notify)
    dock_service.add_characteristic(dock_write)
    app.add_service(dock_service)

    # Sweeper-facing discovery placeholders (16-bit UUIDs).
    sweeper_service = Service(bus, 1, "B000", True)
    sweeper_service.add_characteristic(Characteristic(bus, 0, "B001", ["read", "write", "write-without-response"], sweeper_service))
    sweeper_service.add_characteristic(Characteristic(bus, 1, "B002", ["read", "write", "write-without-response"], sweeper_service))
    sweeper_service.add_characteristic(Characteristic(bus, 2, "B003", ["notify", "write", "write-without-response"], sweeper_service))
    sweeper_service.add_characteristic(Characteristic(bus, 3, "B004", ["notify", "write", "write-without-response"], sweeper_service))
    app.add_service(sweeper_service)

    return app


def run_emulator(cfg: EmulatorConfig) -> None:
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()

    adapter_path = find_adapter(bus, preferred=cfg.adapter)
    log(f"adapter: {adapter_path}")

    service_manager = dbus.Interface(bus.get_object(BLUEZ_SERVICE_NAME, adapter_path), GATT_MANAGER_IFACE)
    ad_manager = dbus.Interface(bus.get_object(BLUEZ_SERVICE_NAME, adapter_path), LE_ADVERTISING_MANAGER_IFACE)

    app = build_services(bus, cfg)
    adv = Advertisement(bus, 0, cfg)

    mainloop = GLib.MainLoop()

    def _quit(*_: Any) -> None:
        log("shutdown: requested")
        try:
            ad_manager.UnregisterAdvertisement(adv.get_path())
        except Exception:
            pass
        try:
            service_manager.UnregisterApplication(app.get_path())
        except Exception:
            pass
        mainloop.quit()

    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, _quit)

    def _reg_app_ok() -> None:
        log("gatt: application registered")

    def _reg_app_err(error: Exception) -> None:
        log(f"gatt: register failed: {error}")
        mainloop.quit()

    def _reg_adv_ok() -> None:
        mac_label = cfg.manufacturer_mac_label or _best_effort_host_mac_label()
        log(
            "adv: registered "
            f"name={cfg.local_name} company=0x{cfg.company_id:04X} mac_label={mac_bytes_to_text(mac_label)} "
            f"uuids={cfg.advertise_service_uuids}"
        )

    def _reg_adv_err(error: Exception) -> None:
        log(f"adv: register failed: {error}")
        mainloop.quit()

    log("starting…")
    service_manager.RegisterApplication(app.get_path(), {}, reply_handler=_reg_app_ok, error_handler=_reg_app_err)
    ad_manager.RegisterAdvertisement(adv.get_path(), {}, reply_handler=_reg_adv_ok, error_handler=_reg_adv_err)

    try:
        mainloop.run()
    except KeyboardInterrupt:
        _quit()
    log("stopped")

