#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

C_FILES=$(find "${ROOT}/src" "${ROOT}/include" -type f \( -name "*.c" -o -name "*.h" \))
if [[ -n "${C_FILES}" ]]; then
  clang-format --dry-run --Werror ${C_FILES}
fi

SH_FILES=$(find "${ROOT}/scripts" -type f -name "*.sh")
if [[ -n "${SH_FILES}" ]]; then
  shellcheck ${SH_FILES}
fi

rpmlint -f "${ROOT}/packaging/rpm/rpmlint.conf" "${ROOT}/packaging/rpm/k10-barrel-emulator.spec"
