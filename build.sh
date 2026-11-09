#!/usr/bin/env bash
# Compila o Console Launcher em build/console-launcher.
#   bash build.sh            compila (Release)
#   bash build.sh --clean    apaga build/ antes
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"

[ "${1:-}" = "--clean" ] && rm -rf "$ROOT/build"

missing=()
command -v cmake >/dev/null || missing+=(cmake)
command -v g++ >/dev/null || missing+=(g++)
[ -f /usr/lib/x86_64-linux-gnu/cmake/Qt6Quick/Qt6QuickConfig.cmake ] \
    || [ -n "$(find /usr/lib* -path '*cmake/Qt6Quick/Qt6QuickConfig.cmake' -print -quit 2>/dev/null)" ] \
    || missing+=(qt6-declarative-dev)
if [ ${#missing[@]} -gt 0 ]; then
    echo "Faltam dependências de compilação: ${missing[*]}" >&2
    echo "Rode:  bash scripts/install-deps.sh" >&2
    exit 1
fi

cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build" -j"$(nproc)"
echo
echo "Pronto: $ROOT/build/console-launcher"
echo "Teste em janela:  $ROOT/build/console-launcher --windowed"
