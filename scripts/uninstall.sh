#!/usr/bin/env bash
# Remove o Console Launcher.
#   bash scripts/uninstall.sh           remove o programa e o início automático
#   bash scripts/uninstall.sh --purge   também apaga configuração e histórico (~/.config/console-launcher)
# As pastas de ROMs (~/ROMs) e os emuladores nunca são apagados.
set -euo pipefail
SUDO=""; [ "$(id -u)" -eq 0 ] || SUDO="sudo"
REAL_USER="${SUDO_USER:-$(id -un)}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"

pkill -u "$REAL_USER" -f '^(/usr/(local/)?bin/)?console-launcher( |$)' 2>/dev/null || true
if dpkg -s console-launcher >/dev/null 2>&1; then
    $SUDO apt-get remove -y console-launcher
fi
rm -f "$REAL_HOME/.config/autostart/console-launcher.desktop"
if [ "${1:-}" = "--purge" ]; then
    rm -rf "$REAL_HOME/.config/console-launcher" "$REAL_HOME/.local/state/console-launcher"
    echo "Configuração e histórico removidos."
fi
echo "Console Launcher removido."
