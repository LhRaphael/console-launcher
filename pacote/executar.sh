#!/usr/bin/env bash
# executar.sh — abre o Console Launcher. Se ele ainda não estiver instalado nesta
# máquina, roda o instalar.sh antes.
#
#   bash executar.sh             tela cheia (modo console)
#   bash executar.sh --janela    numa janela, para testar
#   bash executar.sh --primeiro-plano   não solta o terminal (vê as mensagens do programa)
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP=console-launcher

args=(); FG=0
for a in "$@"; do
    case "$a" in
        --janela|--windowed) args+=(--windowed) ;;
        --primeiro-plano) FG=1 ;;
        -h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) args+=("$a") ;;
    esac
done

[ "$(id -u)" -ne 0 ] || { echo "Rode como o seu usuário normal, sem sudo." >&2; exit 1; }

if ! command -v $APP >/dev/null; then
    echo "O Console Launcher ainda não está instalado nesta máquina."
    read -r -p "Instalar agora? [S/n] " r
    [[ "${r:-s}" =~ ^[sSyY] ]] || exit 1
    bash "$HERE/instalar.sh" --nao-abrir || exit 1
fi

if [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    echo "Sem sessão gráfica: entre na área de trabalho e rode de novo (ou abra pelo menu)." >&2
    exit 1
fi

# Já aberto? Só traz para a frente (a segunda instância repassa para a primeira).
if pgrep -u "$(id -un)" -f '^(/usr/(local/)?bin/)?console-launcher( |$)' >/dev/null; then
    exec $APP "${args[@]+"${args[@]}"}"
fi

# Emuladores sem permissão de execução (cópia por pendrive FAT/NTFS, por exemplo)
if [ -d "$HOME/emulators" ]; then
    shopt -s nullglob nocaseglob
    for f in "$HOME"/emulators/*.appimage; do [ -x "$f" ] || chmod a+x "$f" 2>/dev/null; done
    shopt -u nocaseglob
fi

# Se uma sessão anterior caiu no meio de um jogo, devolve os apps pausados pelo modo jogo.
$APP --resume-apps >/dev/null 2>&1 || true

if [ $FG = 1 ]; then
    exec $APP "${args[@]+"${args[@]}"}"
fi
setsid -f $APP "${args[@]+"${args[@]}"}" >/dev/null 2>&1 < /dev/null
echo "Console Launcher aberto."
