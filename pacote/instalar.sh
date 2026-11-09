#!/usr/bin/env bash
# instalar.sh — instala o Console Launcher completo nesta máquina: o programa, as
# dependências e os emuladores que vieram no pacote.
#
# Uso (como o seu usuário normal; a senha de administrador é pedida só para o apt):
#   bash instalar.sh                  instala tudo e abre o launcher no fim
#   bash instalar.sh --sem-emuladores só o programa e as dependências
#   bash instalar.sh --sem-dolphin    não instala o Dolphin (Flatpak; precisa de internet)
#   bash instalar.sh --compilar       compila a partir do código em vez de usar o .deb pronto
#   bash instalar.sh --autostart      abre o launcher sozinho ao entrar na sessão (modo console)
#   bash instalar.sh --nao-abrir      não abre o launcher no fim
#   bash instalar.sh --sim            não pergunta nada (aceita o padrão)
#
# Funciona em distros baseadas em Debian com Qt 6.4 ou mais novo: Debian 12/13,
# Ubuntu 24.04+, Linux Mint 22+, Pop!_OS 24.04, Zorin 18 e afins (x86_64).
# Nunca instala BIOS, firmware, chaves ou jogos: eles vêm do seu próprio console.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP=console-launcher

WITH_EMUS=1; WITH_DOLPHIN=1; COMPILE=0; AUTOSTART=0; OPEN=1; YES=0
for a in "$@"; do
    case "$a" in
        --sem-emuladores) WITH_EMUS=0 ;;
        --sem-dolphin) WITH_DOLPHIN=0 ;;
        --compilar) COMPILE=1 ;;
        --autostart) AUTOSTART=1 ;;
        --nao-abrir) OPEN=0 ;;
        --sim|-y) YES=1 ;;
        -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Opção desconhecida: $a (veja: bash instalar.sh --help)" >&2; exit 2 ;;
    esac
done

# ------------------------------------------------------------------ saída
if [ -t 1 ]; then B=$'\e[1m'; G=$'\e[32m'; Y=$'\e[33m'; R=$'\e[31m'; N=$'\e[0m'; else B=; G=; Y=; R=; N=; fi
step() { printf '\n%s==> %s%s\n' "$B" "$*" "$N"; }
ok()   { printf '  %s✓%s %s\n' "$G" "$N" "$*"; }
warn() { printf '  %s!%s %s\n' "$Y" "$N" "$*"; WARNINGS+=("$*"); }
die()  { printf '\n%sErro:%s %s\n' "$R" "$N" "$*" >&2; [ -n "${LOG:-}" ] && echo "Registro completo: $LOG" >&2; exit 1; }
ask()  { # ask "pergunta" padrão(s/n)
    local def="$2" r
    [ $YES = 1 ] || [ ! -t 0 ] && { [ "$def" = s ]; return; }
    read -r -p "  $1 [$( [ "$def" = s ] && echo S/n || echo s/N )] " r
    r="${r:-$def}"; [[ "$r" =~ ^[sSyY] ]]
}
WARNINGS=()

# ------------------------------------------------------------------ usuário
if [ "$(id -u)" -eq 0 ]; then
    [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ] \
        || die "rode como o seu usuário normal (sem sudo): os emuladores e as configurações ficam na sua pasta pessoal."
    REAL_USER="$SUDO_USER"; SUDO=""
else
    REAL_USER="$(id -un)"; SUDO="sudo"
fi
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"
as_user() { if [ "$(id -un)" != "$REAL_USER" ]; then sudo -u "$REAL_USER" -H "$@"; else "$@"; fi; }

STATE="$REAL_HOME/.local/state/$APP"
as_user mkdir -p "$STATE"
LOG="$STATE/instalacao-$(date +%Y%m%d-%H%M%S).log"
as_user touch "$LOG"
exec > >(tee -a "$LOG") 2>&1

VERSION="$(cat "$HERE/VERSAO" 2>/dev/null || echo '?')"
printf '%sConsole Launcher %s — instalação%s\n' "$B" "$VERSION" "$N"
echo "Usuário: $REAL_USER ($REAL_HOME)"

# ------------------------------------------------------------------ verificações
step "Verificando a máquina"
[ "$(uname -m)" = x86_64 ] || die "este pacote é para x86_64 (64 bits); esta máquina é $(uname -m)."
command -v apt-get >/dev/null && command -v dpkg >/dev/null \
    || die "não encontrei o apt: este instalador é para distros baseadas em Debian/Ubuntu."
. /etc/os-release 2>/dev/null || true
ok "Sistema: ${PRETTY_NAME:-desconhecido}"
if [ -n "$SUDO" ]; then
    command -v sudo >/dev/null || die "o sudo não está instalado. Instale-o como root (apt install sudo) e adicione seu usuário ao grupo sudo."
    echo "  A senha de administrador é pedida uma vez, para o apt:"
    sudo -v || die "sem permissão de administrador (sudo)."
    # mantém o sudo ativo durante a instalação
    ( while kill -0 $$ 2>/dev/null; do sudo -n true 2>/dev/null; sleep 50; done ) &
fi

free_kb="$(df -Pk "$REAL_HOME" | awk 'NR==2 {print $4}')"
need_kb="$(du -sk "$HERE/emuladores" 2>/dev/null | cut -f1)"; need_kb=$(( ${need_kb:-0} + 600000 ))
[ "${free_kb:-0}" -ge "$need_kb" ] || warn "pouco espaço livre em $REAL_HOME ($((free_kb/1024)) MB; recomendado $((need_kb/1024)) MB)."

if [ -f "$HERE/SHA256SUMS" ]; then
    if (cd "$HERE" && sha256sum --quiet -c SHA256SUMS >/dev/null 2>&1); then
        ok "Arquivos do pacote íntegros (SHA-256)"
    else
        (cd "$HERE" && sha256sum --quiet -c SHA256SUMS 2>&1 | sed 's/^/    /' | head -10)
        die "há arquivos corrompidos no pacote (cópia incompleta?). Copie o .tar.gz de novo e descompacte."
    fi
fi

# ------------------------------------------------------------------ dependências
step "Atualizando a lista de pacotes (apt)"
$SUDO apt-get update -q || warn "o apt update falhou (sem internet?). Tentando com a lista que já existe."

has_pkg() { [ -n "$(apt-cache policy "$1" 2>/dev/null | awk '/Candidate:/ {print $2}' | grep -v '(none)')" ]; }
pick() { local p; for p in "$@"; do has_pkg "$p" && { echo "$p"; return 0; }; done; return 1; }

step "Instalando as dependências"
want=()
add() { local p; p="$(pick "$@")" && want+=("$p") || warn "pacote indisponível nesta distro: $*"; }
add libfuse2t64 libfuse2            # os AppImages precisam do FUSE 2
add gamemode                        # modo jogo (CPU no desempenho)
add qml6-module-qtmultimedia        # papel de parede em vídeo
add gstreamer1.0-plugins-good
add gstreamer1.0-libav
add mesa-vulkan-drivers             # Vulkan (RPCS3, shadPS4, Eden, PCSX2…)
add libvulkan1
add 7zip p7zip-full                 # importar jogos compactados
add unar
add unzip
add xdg-utils
add qt6-wayland
[ $WITH_EMUS = 1 ] && [ $WITH_DOLPHIN = 1 ] && compgen -G "$HERE/emuladores/dolphin-*.flatpak" >/dev/null && add flatpak
if [ ${#want[@]} -gt 0 ]; then
    $SUDO apt-get install -y "${want[@]}" || warn "algumas dependências não foram instaladas (veja o registro)."
fi
ok "${#want[@]} dependência(s) verificada(s)"

# ------------------------------------------------------------------ programa
install_deb() {
    local deb="$1" tmp
    # o apt lê o .deb como usuário _apt: copia para um lugar que ele alcança
    tmp="$(mktemp -d /tmp/$APP.XXXXXX)"; chmod 755 "$tmp"
    cp "$deb" "$tmp/"; chmod 644 "$tmp"/*.deb
    $SUDO apt-get install -y --reinstall "$tmp/$(basename "$deb")"
    local rc=$?; rm -rf "$tmp"; return $rc
}

compile_and_install() {
    step "Compilando a partir do código-fonte"
    [ -d "$HERE/codigo" ] || die "o código-fonte não veio no pacote (pasta codigo/)."
    local qtv; qtv="$(apt-cache policy qt6-declarative-dev 2>/dev/null | awk '/Candidate:/ {print $2}')"
    if [ -n "$qtv" ] && [ "$qtv" != "(none)" ] && dpkg --compare-versions "$qtv" lt 6.4; then
        die "esta distro tem Qt $qtv; o Console Launcher precisa do Qt 6.4 ou mais novo (Debian 12+, Ubuntu 24.04+, Mint 22+)."
    fi
    local src; src="$(mktemp -d /tmp/$APP-src.XXXXXX)"
    cp -a "$HERE/codigo/." "$src/"
    chown -R "$REAL_USER" "$src" 2>/dev/null || true
    bash "$src/scripts/install-deps.sh" || die "não consegui instalar as dependências de compilação."
    as_user bash "$src/scripts/package-deb.sh" --build || die "a compilação falhou (veja o registro)."
    install_deb "$(ls -t "$src"/dist/${APP}_*.deb | head -1)" || die "não consegui instalar o pacote compilado."
    rm -rf "$src"
}

step "Instalando o Console Launcher"
DEB="$(ls -t "$HERE"/pacotes/${APP}_*.deb 2>/dev/null | head -1)"
if [ $COMPILE = 1 ] || [ -z "$DEB" ]; then
    compile_and_install
elif install_deb "$DEB"; then
    ok "$(basename "$DEB")"
else
    warn "o pacote pronto não instalou nesta distro; vou compilar do código."
    compile_and_install
fi
command -v $APP >/dev/null || die "o console-launcher não ficou instalado."
ok "Instalado: $(dpkg-query -W -f='${Version}' $APP 2>/dev/null) em $(command -v $APP)"

# ------------------------------------------------------------------ emuladores
MANIFEST="$REAL_HOME/.local/share/$APP/emuladores-instalados.txt"
if [ $WITH_EMUS = 1 ] && [ -d "$HERE/emuladores" ]; then
    step "Copiando os emuladores para ~/emulators"
    EMU="$REAL_HOME/emulators"
    as_user mkdir -p "$EMU" "$(dirname "$MANIFEST")"
    as_user touch "$MANIFEST"
    shopt -s nullglob nocaseglob
    files=("$HERE"/emuladores/*.appimage "$HERE"/emuladores/*.flatpak)
    shopt -u nocaseglob
    for f in "${files[@]}"; do
        name="$(basename "$f")"
        [ $WITH_DOLPHIN = 0 ] && [[ "$name" == dolphin-*.flatpak ]] && continue
        if [ -f "$EMU/$name" ] && cmp -s "$f" "$EMU/$name"; then
            ok "$name (já estava lá)"
        else
            as_user install -m 755 "$f" "$EMU/$name" || { warn "não consegui copiar $name"; continue; }
            ok "$name"
        fi
        grep -qxF "$name" "$MANIFEST" 2>/dev/null || echo "$name" | as_user tee -a "$MANIFEST" >/dev/null
    done
    as_user install -m 755 "$HERE/emuladores/setup-emuladores.sh" "$EMU/setup-emuladores.sh"

    step "Preparando os emuladores"
    dol=(); [ $WITH_DOLPHIN = 0 ] && dol=(--no-dolphin)
    as_user bash "$EMU/setup-emuladores.sh" "$EMU" ${dol[@]+"${dol[@]}"} 2>&1 | sed 's/^/  /' \
        || warn "algum emulador não ficou pronto (veja acima)."
fi

# ------------------------------------------------------------------ início automático
if [ $AUTOSTART = 0 ] && [ $YES = 0 ] && [ -t 0 ]; then
    ask "Abrir o Console Launcher sozinho ao entrar na sessão (modo console)?" n && AUTOSTART=1
fi
if [ $AUTOSTART = 1 ]; then
    dir="$REAL_HOME/.config/autostart"
    as_user mkdir -p "$dir"
    as_user tee "$dir/$APP.desktop" >/dev/null <<'EOF'
[Desktop Entry]
Type=Application
Name=Console Launcher
Comment=Abre a interface de console ao entrar na sessão
Exec=console-launcher
Icon=console-launcher
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
    ok "Início automático ativado ($dir/$APP.desktop)"
fi

# ------------------------------------------------------------------ resumo
step "Pronto"
echo "  Programa:   $(command -v $APP) — no menu como \"Console Launcher\""
[ $WITH_EMUS = 1 ] && echo "  Emuladores: ~/emulators"
echo "  Jogos:      ~/ROMs/<console> (snes, n64, gba, ps1, ps2, ps3, ps4, xbox, gc, wii, switch)"
echo "              ou pela aba do console ▸ Importar (F4)"
echo "  BIOS:       menu lateral (F1) ▸ BIOS e firmware — extraídas do seu próprio console"
echo "  Abrir:      bash $(printf '%q' "$HERE")/executar.sh    (ou: console-launcher)"
echo "  Registro:   $LOG"
if [ ${#WARNINGS[@]} -gt 0 ]; then
    printf '\n%sAvisos:%s\n' "$Y" "$N"; printf '  - %s\n' "${WARNINGS[@]}"
fi

if [ $OPEN = 1 ]; then
    if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
        if ask "Abrir o Console Launcher agora?" s; then
            as_user setsid -f $APP >/dev/null 2>&1 < /dev/null && echo "  Abrindo…"
        fi
    else
        echo
        echo "Sem sessão gráfica neste terminal: entre na área de trabalho e abra pelo menu."
    fi
fi
exit 0
