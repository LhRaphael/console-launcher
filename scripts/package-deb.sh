#!/usr/bin/env bash
# Gera o pacote .deb do Console Launcher.
#
#   bash scripts/package-deb.sh               empacota o binário pronto em bin/console-launcher-x86_64
#   bash scripts/package-deb.sh --build       compila o código-fonte (build.sh) e empacota o resultado
#   bash scripts/package-deb.sh --binary ARQ  empacota o executável ARQ
#
# Saída: dist/console-launcher_<versão>-<rev>_<arquitetura>.deb
#
# Layout instalado:
#   /usr/bin/console-launcher
#   /usr/share/applications/console-launcher.desktop
#   /usr/share/icons/hicolor/scalable/apps/console-launcher.svg
#   /usr/share/console-launcher/systems.json          configuração padrão (referência)
#   /usr/share/doc/console-launcher                   README, copyright, changelog
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_SRC=""
do_build=0
while [ $# -gt 0 ]; do
    case "$1" in
        --build) do_build=1 ;;
        --binary) BIN_SRC="${2:?--binary exige um caminho}"; shift ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "opção desconhecida: $1" >&2; exit 1 ;;
    esac
    shift
done

for tool in dpkg-deb dpkg-shlibdeps; do
    command -v "$tool" >/dev/null || { echo "Falta '$tool' (sudo apt install dpkg-dev)." >&2; exit 1; }
done

APP=console-launcher
VERSION="$(sed -n 's/^project(console-launcher VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "Não consegui ler a versão em CMakeLists.txt." >&2; exit 1; }
REVISION="${CL_DEB_REVISION:-1}"
FULLVER="$VERSION-$REVISION"
DEB_ARCH="$(dpkg --print-architecture)"

MAINT_NAME="${DEBFULLNAME:-$(git config user.name 2>/dev/null || true)}"
MAINT_MAIL="${DEBEMAIL:-$(git config user.email 2>/dev/null || true)}"
MAINTAINER="${MAINT_NAME:-Console Launcher} <${MAINT_MAIL:-console-launcher@localhost}>"

# ------------------------------------------------------------------ binário
if [ $do_build = 1 ]; then
    bash "$ROOT/build.sh"
    BIN_SRC="$ROOT/build/$APP"
elif [ -z "$BIN_SRC" ]; then
    case "$(uname -m)" in
        x86_64) BIN_SRC="$ROOT/bin/$APP-x86_64" ;;
        *) echo "Sem binário pronto para $(uname -m): use --build." >&2; exit 1 ;;
    esac
fi
[ -f "$BIN_SRC" ] || { echo "Binário não encontrado: $BIN_SRC (use --build para compilar)." >&2; exit 1; }
head -c4 "$BIN_SRC" | grep -q 'ELF' || { echo "$BIN_SRC não é um executável ELF." >&2; exit 1; }
echo ">> Binário: $BIN_SRC"

# ------------------------------------------------------------------ montagem
DIST="$ROOT/dist"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$DIST"
rm -f "$DIST"/${APP}_*.deb
umask 022

PKG="$TMP/$APP"
mkdir -p "$PKG/DEBIAN"
DOC=/usr/share/doc/$APP
put() { install -D -m "$1" "$2" "$3"; }

echo ">> Montando o pacote $APP $FULLVER ($DEB_ARCH)"
put 755 "$BIN_SRC" "$PKG/usr/bin/$APP"
strip --strip-unneeded --remove-section=.comment --remove-section=.note "$PKG/usr/bin/$APP"
put 644 "$ROOT/data/$APP.desktop" "$PKG/usr/share/applications/$APP.desktop"
put 644 "$ROOT/data/$APP.svg" "$PKG/usr/share/icons/hicolor/scalable/apps/$APP.svg"
put 644 "$ROOT/data/systems.json" "$PKG/usr/share/$APP/systems.json"

gzip -9nc "$ROOT/README.md" > "$TMP/README.md.gz"
put 644 "$TMP/README.md.gz" "$PKG$DOC/README.md.gz"
{
    echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
    echo "Upstream-Name: $APP"
    echo
    echo "Files: *"
    echo "Copyright: $(date +%Y) ${MAINT_NAME:-Console Launcher}"
    if [ -f "$ROOT/LICENSE" ]; then
        echo "License: see-below"
        sed -e 's/^$/./' -e 's/^/ /' "$ROOT/LICENSE"
    else
        echo "License: unspecified"
        echo " Nenhum arquivo LICENSE foi encontrado no projeto."
    fi
    # fontes dos temas (embutidas no programa)
    for lic in "$ROOT"/data/fonts/*-OFL.txt "$ROOT"/data/fonts/*-LICENSE.txt; do
        [ -f "$lic" ] || continue
        name="$(basename "$lic" | sed -E 's/-(OFL|LICENSE)\.txt$//')"
        echo
        echo "Files: data/fonts/$name*"
        case "$lic" in *OFL.txt) echo "License: OFL-1.1" ;; *) echo "License: Apache-2.0" ;; esac
        sed -e 's/^$/./' -e 's/^/ /' "$lic"
    done
} > "$TMP/copyright"
put 644 "$TMP/copyright" "$PKG$DOC/copyright"
printf '%s (%s) unstable; urgency=medium\n\n  * Pacote gerado por scripts/package-deb.sh.\n\n -- %s  %s\n' \
    "$APP" "$FULLVER" "$MAINTAINER" "$(LC_ALL=C date -R)" | gzip -9n > "$TMP/changelog.gz"
put 644 "$TMP/changelog.gz" "$PKG$DOC/changelog.Debian.gz"
printf '%s: initial-upload-closes-no-bugs\n' "$APP" > "$TMP/override"
put 644 "$TMP/override" "$PKG/usr/share/lintian/overrides/$APP"

# Bibliotecas ligadas (Qt, SDL2) via dpkg-shlibdeps; os módulos QML são
# carregados em tempo de execução e precisam ser listados à mão.
mkdir -p "$TMP/shlibs/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$APP" "$APP" > "$TMP/shlibs/debian/control"
DEPENDS="$(cd "$TMP/shlibs" && dpkg-shlibdeps -O --warnings=0 -e"$PKG/usr/bin/$APP" 2>/dev/null \
    | sed -n 's/^shlibs:Depends=//p' || true)"
if [ -z "$DEPENDS" ]; then
    DEPENDS="$(ldd "$PKG/usr/bin/$APP" | awk '/=> \// {print $3}' | xargs -r readlink -f \
        | xargs -r dpkg -S 2>/dev/null | cut -d: -f1 | sort -u | paste -sd, | sed 's/,/, /g' || true)"
fi
[ -n "$DEPENDS" ] || { echo "Não consegui determinar as dependências do pacote." >&2; exit 1; }
# Debian 13 / Ubuntu 24.04 renomearam as bibliotecas para "…t64" (só muda nas arquiteturas
# de 32 bits; em amd64 a ABI é a mesma). Aceitar também o nome antigo deixa o pacote
# instalar no Debian 12, que tem o mesmo Qt 6.4.
DEPENDS="$(printf '%s' "$DEPENDS" | sed -E 's/(lib[a-z0-9.+-]+)t64( \([^)]*\))?/\1t64\2 | \1\2/g')"
DEPENDS="$DEPENDS, qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, libqt6svg6"

{
    echo "Package: $APP"
    echo "Version: $FULLVER"
    echo "Architecture: $DEB_ARCH"
    echo "Maintainer: $MAINTAINER"
    echo "Installed-Size: $(du -sk --exclude=DEBIAN "$PKG" | cut -f1)"
    echo "Depends: $DEPENDS"
    echo "Recommends: libfuse2t64 | libfuse2, gamemode, qml6-module-qtmultimedia, gstreamer1.0-plugins-good, gstreamer1.0-libav, flatpak, 7zip | p7zip-full, unar, unzip, pulseaudio-utils | wireplumber, bluez, network-manager, qt6-wayland, pciutils, shared-mime-info, xdg-utils"
    echo "Suggests: linosd"
    echo "Section: games"
    echo "Priority: optional"
    echo "Description: interface de console para jogos emulados"
    echo " Interface principal da distro: jogos em abas por console (SNES, N64,"
    echo " GBA, PS1, PS2, PS3, PS4, Xbox, GameCube, Wii, Switch) e uma aba de apps,"
    echo " tudo navegável por controle (SDL2). Configura cada emulador e cada jogo"
    echo " pela própria interface, importa jogos de qualquer pasta (extraindo e"
    echo " limpando os compactados), tem atalhos globais de teclado e controle e"
    echo " um menu lateral com apps abertos, som, Bluetooth, rede, tela e energia,"
    echo " além de gerenciador de arquivos e informações do computador."
} > "$PKG/DEBIAN/control"

(cd "$PKG" && find usr -type f -print0 | sort -z | xargs -0 md5sum) > "$PKG/DEBIAN/md5sums"
chmod 644 "$PKG/DEBIAN/control" "$PKG/DEBIAN/md5sums"
find "$PKG" -type d -exec chmod 755 {} +
OUT="$DIST/${APP}_${FULLVER}_${DEB_ARCH}.deb"
# xz: o dpkg do Debian não lê zstd (padrão do Ubuntu)
dpkg-deb --root-owner-group -Zxz --build "$PKG" "$OUT" >/dev/null
echo ">> Pacote criado: $OUT"
echo "   Depends: $DEPENDS"
