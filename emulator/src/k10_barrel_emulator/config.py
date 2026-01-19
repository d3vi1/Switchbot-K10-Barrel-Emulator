from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml


@dataclass(frozen=True)
class EmulatorConfig:
    adapter: str
    local_name: str

    fw_major: int
    fw_minor: int

    company_id: int
    manufacturer_mac_label: bytes | None

    include_tx_power: bool
    advertise_fd3d_service_data: bool
    fd3d_service_data: bytes

    advertise_service_uuids: list[str]


def _parse_int(value: Any) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        v = value.strip().lower()
        if v.startswith("0x"):
            return int(v, 16)
        return int(v, 10)
    raise TypeError(f"Expected int-like value, got {type(value)}")


def _parse_hex_bytes(value: Any, *, expected_len: int | None = None) -> bytes:
    if value is None:
        return b""
    if isinstance(value, bytes):
        data = value
    elif isinstance(value, str):
        cleaned = value.strip().replace(":", "").replace("-", "").replace(" ", "")
        data = bytes.fromhex(cleaned)
    else:
        raise TypeError(f"Expected hex string/bytes, got {type(value)}")

    if expected_len is not None and len(data) != expected_len:
        raise ValueError(f"Expected {expected_len} bytes, got {len(data)}")
    return data


def load_config(path: Path) -> EmulatorConfig:
    raw = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    node = raw.get("k10_emulator") or {}

    adapter = str(node.get("adapter") or "hci0")
    local_name = str(node.get("local_name") or "WoS1MB")

    fw_major = int(node.get("fw_major") or 1)
    fw_minor = int(node.get("fw_minor") or 0)

    company_id = _parse_int(node.get("company_id") or "0x0969")

    mac_label_raw = node.get("manufacturer_mac_label")
    manufacturer_mac_label = _parse_hex_bytes(mac_label_raw, expected_len=6) if mac_label_raw else None

    include_tx_power = bool(node.get("include_tx_power", True))
    advertise_fd3d_service_data = bool(node.get("advertise_fd3d_service_data", True))
    fd3d_service_data = _parse_hex_bytes(node.get("fd3d_service_data_hex") or "00")

    advertise_service_uuids = [str(x) for x in (node.get("advertise_service_uuids") or [])]

    return EmulatorConfig(
        adapter=adapter,
        local_name=local_name,
        fw_major=fw_major,
        fw_minor=fw_minor,
        company_id=company_id,
        manufacturer_mac_label=manufacturer_mac_label,
        include_tx_power=include_tx_power,
        advertise_fd3d_service_data=advertise_fd3d_service_data,
        fd3d_service_data=fd3d_service_data,
        advertise_service_uuids=advertise_service_uuids,
    )

