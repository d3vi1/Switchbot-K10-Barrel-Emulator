from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .config import load_config
from .emulator import run_emulator


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="SwitchBot K10 dock/barrel emulator (BlueZ)")
    parser.add_argument(
        "--config",
        default="/etc/switchbot-k10-barrel-emulator/config.yml",
        help="Path to YAML config file",
    )
    args = parser.parse_args(argv)

    config_path = Path(args.config)
    if not config_path.exists():
        print(f"[k10-emulator] config not found: {config_path}", file=sys.stderr)
        print("[k10-emulator] tip: copy emulator/config/default.yml to that path", file=sys.stderr)
        return 2

    config = load_config(config_path)
    run_emulator(config)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

