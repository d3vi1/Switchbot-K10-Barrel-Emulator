#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

mapfile -t c_files < <(find src include -type f \( -name "*.c" -o -name "*.h" \))
if (( ${#c_files[@]} )); then
  clang-format -style=file --dry-run --Werror "${c_files[@]}"
fi

mapfile -t sh_files < <(find scripts -type f -name "*.sh")
if (( ${#sh_files[@]} )); then
  shellcheck "${sh_files[@]}"
fi

rpmlint -c "${ROOT}/packaging/rpm/rpmlint.conf" "${ROOT}/packaging/rpm/k10-barrel-emulator.spec"
