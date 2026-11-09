#!/usr/bin/env bash
# desinstalar.sh — remove o Console Launcher desta máquina.
#
#   bash desinstalar.sh            remove o programa e o início automático
#   bash desinstalar.sh --tudo     também os emuladores que o instalar.sh copiou para
#                                  ~/emulators, o Dolphin (Flatpak), as configurações
#                                  e o histórico do launcher
#
# Nunca apaga: jogos (~/ROMs), BIOS, saves e configurações dos emuladores.
set -uo pipefail
APP=console-launcher
ALL=0
case "${1:-}" in
    --tudo) ALL=1 ;;
    "") ;;
    -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "Opção desconhecida: $1" >&2; exit 2 ;;
esac
[ "$(id -u)" -ne 0 ] || { echo "Rode como o seu usuário normal, sem sudo." >&2; exit 1; }

$APP --resume-apps >/dev/null 2>&1 || true
pkill -u "$(id -un)" -f '^(/usr/(local/)?bin/)?console-launcher( |$)' 2>/dev/null || true
if dpkg -s $APP >/dev/null 2>&1; then
    sudo apt-get remove -y $APP
fi
rm -f "$HOME/.config/autostart/$APP.desktop"
echo "Programa removido."

if [ $ALL = 1 ]; then
    MANIFEST="$HOME/.local/share/$APP/emuladores-instalados.txt"
    if [ -f "$MANIFEST" ]; then
        while IFS= read -r name; do
            [ -n "$name" ] && [ -f "$HOME/emulators/$name" ] && rm -f "$HOME/emulators/$name" && echo "  removido: ~/emulators/$name"
        done < "$MANIFEST"
        rm -f "$MANIFEST"
    fi
    if command -v flatpak >/dev/null && flatpak info --user org.DolphinEmu.dolphin-emu >/dev/null 2>&1; then
        flatpak uninstall --user -y org.DolphinEmu.dolphin-emu && echo "  Dolphin removido"
    fi
    rm -rf "$HOME/.config/$APP" "$HOME/.local/state/$APP" "$HOME/.local/share/$APP" "$HOME/.cache/$APP"
    echo "Emuladores copiados, configurações e histórico do launcher removidos."
    echo "Seus jogos (~/ROMs), BIOS e saves continuam onde estavam."
fi
