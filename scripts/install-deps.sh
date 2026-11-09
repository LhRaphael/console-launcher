#!/usr/bin/env bash
# Instala as dependências para compilar o Console Launcher (Debian 12/13, Ubuntu 24.04+).
# Pacotes que mudaram de nome entre versões são escolhidos pelo que a distro oferece.
set -euo pipefail
SUDO=""; [ "$(id -u)" -eq 0 ] || SUDO="sudo"
$SUDO apt-get update || true

has_pkg() { [ -n "$(apt-cache policy "$1" 2>/dev/null | awk '/Candidate:/ {print $2}' | grep -v '(none)')" ]; }
pick() { local p; for p in "$@"; do has_pkg "$p" && { echo "$p"; return 0; }; done; return 1; }

pkgs=(build-essential cmake pkg-config dpkg-dev
      qt6-base-dev qt6-declarative-dev libsdl2-dev libgl-dev libxcb1-dev
      qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtqml-workerscript
      libqt6svg6 unar unzip)
opt=()
for alt in "7zip p7zip-full" "qml6-module-qtmultimedia" \
           "libfuse2t64 libfuse2"; do   # FUSE 2: os AppImages precisam dele para abrir
    # shellcheck disable=SC2086
    p="$(pick $alt)" && opt+=("$p") || echo "aviso: nenhum destes pacotes existe aqui: $alt" >&2
done
$SUDO apt-get install -y "${pkgs[@]}" "${opt[@]}"
