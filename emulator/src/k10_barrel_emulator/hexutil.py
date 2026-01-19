from __future__ import annotations


def hex_encode(data: bytes) -> str:
    return data.hex()


def mac_bytes_to_text(data: bytes) -> str:
    if len(data) != 6:
        return hex_encode(data)
    return ":".join(f"{b:02X}" for b in data)

