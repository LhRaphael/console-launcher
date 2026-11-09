#!/usr/bin/env bash
# Instala o Console Launcher como programa do sistema (pacote .deb via apt).
#
#   bash scripts/install.sh                 empacota o binário pronto (bin/) e instala
#   bash scripts/install.sh --build         compila antes (instala as dependências de compilação se faltarem)
#   bash scripts/install.sh --autostart     também abre o launcher em tela cheia ao entrar na sessão
#
# Pede a senha de administrador (sudo) só para o apt.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
pkg_args=()
autostart=0
for a in "$@"; do
    case "$a" in
        --build) pkg_args+=(--build) ;;
        --autostart) autostart=1 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "opção desconhecida: $a" >&2; exit 1 ;;
    esac
done

SUDO=""; [ "$(id -u)" -eq 0 ] || SUDO="sudo"
# Se rodou com sudo, compila e configura como o usuário real.
REAL_USER="${SUDO_USER:-$(id -un)}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"
as_user() { if [ "$(id -un)" != "$REAL_USER" ]; then sudo -u "$REAL_USER" -H "$@"; else "$@"; fi; }

command -v dpkg-shlibdeps >/dev/null || $SUDO apt-get install -y dpkg-dev
if [ ${#pkg_args[@]} -gt 0 ] && ! command -v cmake >/dev/null; then
    echo ">> Instalando dependências de compilação"
    bash "$ROOT/scripts/install-deps.sh"
fi

as_user bash "$ROOT/scripts/package-deb.sh" ${pkg_args[@]+"${pkg_args[@]}"}

DEB="$(ls -t "$ROOT"/dist/console-launcher_*.deb | head -1)"
# apt lê o arquivo como usuário _apt: copia para um lugar acessível.
STAGE="$(mktemp -d /tmp/console-launcher.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT
cp "$DEB" "$STAGE/"; chmod 755 "$STAGE"; chmod 644 "$STAGE"/*.deb
echo ">> Instalando $(basename "$DEB")"
$SUDO apt-get install -y --reinstall "$STAGE/$(basename "$DEB")"

if [ $autostart = 1 ]; then
    dir="$REAL_HOME/.config/autostart"
    as_user mkdir -p "$dir"
    as_user tee "$dir/console-launcher.desktop" >/dev/null <<'EOF'
[Desktop Entry]
Type=Application
Name=Console Launcher
Comment=Abre a interface de console ao entrar na sessão
Exec=console-launcher
Icon=console-launcher
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
    echo ">> Início automático ativado: $dir/console-launcher.desktop"
fi

# O launcher que já estava aberto continua sendo a versão antiga até reiniciar:
# fecha e abre de novo com os mesmos argumentos e o mesmo ambiente gráfico.
restart_launcher() {
    local pids p
    pids="$(pgrep -u "$REAL_USER" -f '^(/usr/(local/)?bin/)?console-launcher( |$)' || true)"
    [ -n "$pids" ] || return 0
    p="$(echo "$pids" | head -1)"
    local -a args=() envs=()
    mapfile -d '' -t args < "/proc/$p/cmdline" || true
    args=("${args[@]:1}")
    # instâncias de linha de comando (--action, --import-bios…) não são a interface
    for a in "${args[@]}"; do case "$a" in --action*|--import-bios*|--bios-status|--resume-apps|--screenshot*) return 0 ;; esac; done
    local kv
    while IFS= read -r -d '' kv; do
        case "$kv" in
            DISPLAY=*|WAYLAND_DISPLAY=*|XDG_RUNTIME_DIR=*|DBUS_SESSION_BUS_ADDRESS=*|XDG_CURRENT_DESKTOP=*|\
            XDG_SESSION_TYPE=*|XDG_SESSION_DESKTOP=*|XAUTHORITY=*|HOME=*|USER=*|LANG=*|LC_*=*|PATH=*|QT_*=*)
                envs+=("$kv") ;;
        esac
    done < "/proc/$p/environ"
    echo ">> Reiniciando o launcher aberto para usar a versão nova"
    kill -TERM $pids 2>/dev/null || true
    for _ in $(seq 1 30); do
        pgrep -u "$REAL_USER" -f '^(/usr/(local/)?bin/)?console-launcher( |$)' >/dev/null || break
        sleep 0.2
    done
    as_user env -i "${envs[@]}" setsid -f /usr/bin/console-launcher "${args[@]}" >/dev/null 2>&1 < /dev/null || \
        echo "   Não consegui abrir de novo: abra o Console Launcher pelo menu."
}
restart_launcher

echo
echo "Pronto. Abra pelo menu (\"Console Launcher\") ou rode: console-launcher"
echo "Para testar em janela: console-launcher --windowed"
