#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

C_FILES=$(find src include -type f \( -name "*.c" -o -name "*.h" \))
if [[ -n "${C_FILES}" ]]; then
  clang-format -style=file --dry-run --Werror ${C_FILES}
fi

SH_FILES=$(find scripts -type f -name "*.sh")
if [[ -n "${SH_FILES}" ]]; then
  shellcheck ${SH_FILES}
fi

rpmlint -f "${ROOT}/packaging/rpm/rpmlint.conf" "${ROOT}/packaging/rpm/k10-barrel-emulator.spec"
