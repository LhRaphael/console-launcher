#!/usr/bin/env bash
# setup-emuladores.sh — ajusta as permissões de cada emulador baixado.
#
# O que faz:
#   1. Para cada *.AppImage da pasta (maiúsculas ou minúsculas, ex.: o mGBA vem
#      como .appimage): confere se é ELF da arquitetura desta máquina
#      e dá permissão de execução (chmod a+x). Arquivo de arquitetura errada é
#      pulado, sem alterar permissão.
#   2. Confere se o FUSE 2 (libfuse.so.2), exigido pelos AppImages, está instalado.
#   3. Dolphin (.flatpak): instala para o usuário (sem sudo) e libera acesso a
#      dispositivos (controles, adaptador de GameCube), que o Flatpak bloqueia.
#
# Uso (rode com bash, não precisa de sudo):
#   bash setup-emuladores.sh                 # usa a pasta onde o script está
#   bash setup-emuladores.sh /outra/pasta
#   bash setup-emuladores.sh --check         # só mostra o que faria
#   bash setup-emuladores.sh --no-dolphin    # não mexe no Flatpak do Dolphin
#
# Emuladores esperados (o launcher acha a versão mais nova pelo nome):
#   Snes9x-*.AppImage, DuckStation-*.AppImage, pcsx2-*.AppImage, rpcs3-*.AppImage,
#   shadPS4QtLauncher*.AppImage, xemu-*.AppImage, Eden-Linux-*.AppImage,
#   RMG*.AppImage (Nintendo 64), mGBA-*.appimage (Game Boy Advance), dolphin-*.flatpak
set -u

CHECK=0
DOLPHIN=1
DIR=""
for arg in "$@"; do
  case "$arg" in
    --check)       CHECK=1 ;;
    --no-dolphin)  DOLPHIN=0 ;;
    -h|--help)     sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*)            echo "Opção desconhecida: $arg" >&2; exit 2 ;;
    *)             DIR="$arg" ;;
  esac
done
[ -n "$DIR" ] || DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -d "$DIR" ] || { echo "Pasta não encontrada: $DIR" >&2; exit 1; }

case "$(uname -m)" in
  x86_64)          HOST_ARCH="x86_64";  HOST_MACHINE="3e00" ;;  # e_machine 0x003e
  aarch64|arm64)   HOST_ARCH="aarch64"; HOST_MACHINE="b700" ;;  # e_machine 0x00b7
  *) echo "Arquitetura não suportada: $(uname -m)" >&2; exit 1 ;;
esac

ok=0; skipped=0; failed=0
say() { printf '%s\n' "$*"; }

elf_arch() {  # imprime x86_64 | aarch64 | outro | nao-elf
  local f="$1" magic machine
  magic="$(od -An -tx1 -N4 "$f" 2>/dev/null | tr -d ' \n')"
  [ "$magic" = "7f454c46" ] || { echo "nao-elf"; return; }
  machine="$(od -An -tx1 -j18 -N2 "$f" 2>/dev/null | tr -d ' \n')"
  case "$machine" in
    3e00) echo "x86_64" ;;
    b700) echo "aarch64" ;;
    *)    echo "outro" ;;
  esac
}

say "Pasta: $DIR"
say "Máquina: $HOST_ARCH"
[ "$CHECK" -eq 1 ] && say "(modo --check: nada será alterado)"
say ""

shopt -s nullglob nocaseglob
appimages=("$DIR"/*.AppImage)
shopt -u nocaseglob
if [ "${#appimages[@]}" -eq 0 ]; then
  say "Nenhum .AppImage encontrado em $DIR"
fi

for f in "${appimages[@]}"; do
  name="$(basename "$f")"
  arch="$(elf_arch "$f")"
  if [ "$arch" = "nao-elf" ]; then
    say "[ERRO]  $name — não é um executável ELF (download incompleto ou corrompido?)"
    failed=$((failed+1)); continue
  fi
  if [ "$arch" != "$HOST_ARCH" ]; then
    say "[PULOU] $name — arquitetura $arch, esta máquina é $HOST_ARCH (baixe a versão $HOST_ARCH)"
    skipped=$((skipped+1)); continue
  fi
  if [ "$CHECK" -eq 1 ]; then
    if [ -x "$f" ]; then say "[ok]    $name — já executável"; else say "[faria] $name — chmod a+x"; fi
    ok=$((ok+1)); continue
  fi
  if chmod a+x "$f"; then
    say "[ok]    $name — executável ($arch)"
    ok=$((ok+1))
  else
    say "[ERRO]  $name — chmod falhou (permissão na pasta?)"
    failed=$((failed+1))
  fi
done

# --- FUSE 2 (AppImages precisam dele; Debian 13: pacote libfuse2t64) ---------
say ""
if ldconfig -p 2>/dev/null | grep -q 'libfuse\.so\.2' || \
   ls /usr/lib/*/libfuse.so.2 /lib/*/libfuse.so.2 >/dev/null 2>&1; then
  say "[ok]    FUSE 2 instalado"
else
  say "[AVISO] FUSE 2 (libfuse.so.2) não encontrado — os AppImages não abrirão."
  say "        Instale:  sudo apt install libfuse2t64   (Debian 13 / Ubuntu 24.04)"
  say "                  sudo apt install libfuse2      (Debian 12 e anteriores)"
  say "        Sem FUSE, dá para testar um a um com:  ./arquivo.AppImage --appimage-extract-and-run"
fi

# --- Dolphin (Flatpak) -------------------------------------------------------
dolphin_files=("$DIR"/dolphin-*.flatpak)
if [ "$DOLPHIN" -eq 1 ] && [ "${#dolphin_files[@]}" -gt 0 ]; then
  say ""
  dfile="${dolphin_files[${#dolphin_files[@]}-1]}"
  if ! command -v flatpak >/dev/null 2>&1; then
    say "[AVISO] flatpak não está instalado — Dolphin não foi instalado."
    say "        Instale:  sudo apt install flatpak   e rode este script de novo."
    skipped=$((skipped+1))
  elif [ "$CHECK" -eq 1 ]; then
    say "[faria] Dolphin — flatpak install --user $(basename "$dfile") + liberar dispositivos"
  else
    say "Dolphin: instalando $(basename "$dfile") para o usuário…"
    # o bundle precisa do runtime, que vem do Flathub
    flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo \
      || say "[AVISO] não consegui adicionar o Flathub (sem internet?)"
    if flatpak install --user -y "$dfile"; then
      flatpak override --user --device=all org.DolphinEmu.dolphin-emu \
        && say "[ok]    Dolphin instalado; acesso a dispositivos (controles) liberado"
      ok=$((ok+1))
    else
      say "[ERRO]  Dolphin — falha na instalação do .flatpak"
      failed=$((failed+1))
    fi
  fi
fi

say ""
say "Resumo: $ok ok, $skipped pulado(s), $failed erro(s)"
[ "$failed" -eq 0 ] || exit 1
