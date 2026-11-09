#!/usr/bin/env bash
# montar-pacote-completo.sh — gera o pacote único para instalar o sistema inteiro em
# outra máquina: o programa (.deb), o código-fonte, os emuladores e os scripts de
# instalação/execução.
#
#   bash scripts/montar-pacote-completo.sh                    usa os emuladores de ~/emulators
#   bash scripts/montar-pacote-completo.sh --emuladores DIR   de outra pasta
#   bash scripts/montar-pacote-completo.sh --sem-emuladores   só programa + código
#   bash scripts/montar-pacote-completo.sh --build            compila antes de empacotar
#   bash scripts/montar-pacote-completo.sh --saida DIR        onde gravar (padrão: dist/)
#
# Saída: console-launcher-completo-<versão>-x86_64.tar.gz
# Nunca inclui BIOS, firmware, chaves, jogos ou saves.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP=console-launcher
EMU_DIR="$HOME/emulators"; WITH_EMUS=1; BUILD=0; OUT="$ROOT/dist"
while [ $# -gt 0 ]; do
    case "$1" in
        --emuladores) EMU_DIR="${2:?--emuladores exige uma pasta}"; shift ;;
        --sem-emuladores) WITH_EMUS=0 ;;
        --build) BUILD=1 ;;
        --saida) OUT="${2:?--saida exige uma pasta}"; shift ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Opção desconhecida: $1" >&2; exit 2 ;;
    esac
    shift
done

VERSION="$(sed -n 's/^project(console-launcher VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
NAME="$APP-$VERSION"
ARCHIVE="$OUT/$APP-completo-$VERSION-x86_64.tar.gz"
mkdir -p "$OUT"

# ------------------------------------------------------------------ .deb
echo ">> Programa"
deb="$(ls -t "$ROOT"/dist/${APP}_"$VERSION"-*_amd64.deb 2>/dev/null | head -1 || true)"
if [ $BUILD = 1 ] || [ -z "$deb" ] || { [ -f "$ROOT/bin/$APP-x86_64" ] && [ "$ROOT/bin/$APP-x86_64" -nt "$deb" ]; }; then
    if [ $BUILD = 1 ]; then bash "$ROOT/scripts/package-deb.sh" --build; else bash "$ROOT/scripts/package-deb.sh"; fi
    deb="$(ls -t "$ROOT"/dist/${APP}_"$VERSION"-*_amd64.deb | head -1)"
fi
echo "   $(basename "$deb")"

# ------------------------------------------------------------------ montagem
# Os arquivos grandes entram como links e o tar copia o conteúdo (-h): não ocupa
# o dobro do espaço em disco enquanto monta.
STAGE="$(mktemp -d "$OUT/.montagem.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
P="$STAGE/$NAME"
mkdir -p "$P/pacotes" "$P/codigo"
install -m 755 "$ROOT"/pacote/instalar.sh "$ROOT"/pacote/executar.sh "$ROOT"/pacote/desinstalar.sh "$P/"
install -m 644 "$ROOT/pacote/LEIA-ME.txt" "$P/"
echo "$VERSION" > "$P/VERSAO"
ln -s "$deb" "$P/pacotes/$(basename "$deb")"

echo ">> Código-fonte"
# só o que está sob controle de versão, quando é um repositório git
if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git -C "$ROOT" ls-files -z --cached --others --exclude-standard \
        | (cd "$ROOT" && xargs -0 cp --parents -t "$P/codigo")
else
    (cd "$ROOT" && tar --exclude=./build --exclude=./dist --exclude=./bin --exclude=./.git \
        --exclude='./.montagem.*' -cf - .) | tar -C "$P/codigo" -xf -
fi
echo "   $(find "$P/codigo" -type f | wc -l) arquivos"

# ------------------------------------------------------------------ emuladores
if [ $WITH_EMUS = 1 ]; then
    echo ">> Emuladores ($EMU_DIR)"
    [ -d "$EMU_DIR" ] || { echo "Pasta não encontrada: $EMU_DIR (use --emuladores ou --sem-emuladores)" >&2; exit 1; }
    mkdir -p "$P/emuladores"
    install -m 755 "$ROOT/scripts/setup-emuladores.sh" "$P/emuladores/"
    shopt -s nullglob nocaseglob
    n=0
    for f in "$EMU_DIR"/*.appimage "$EMU_DIR"/*.flatpak; do
        name="$(basename "$f")"
        if [[ "$name" != *.flatpak ]] && [ "$(od -An -tx1 -N4 "$f" | tr -d ' \n')" != 7f454c46 ]; then
            echo "   pulei $name (não é um executável: download incompleto?)"; continue
        fi
        ln -s "$(readlink -f "$f")" "$P/emuladores/$name"
        printf '   %-48s %6s MB\n' "$name" "$(( $(stat -Lc %s "$f") / 1048576 ))"
        n=$((n+1))
    done
    shopt -u nocaseglob
    [ $n -gt 0 ] || echo "   nenhum emulador encontrado em $EMU_DIR"

    {
        echo "EMULADORES INCLUÍDOS"
        echo "===================="
        echo "Cada emulador pertence aos seus autores e segue a própria licença."
        echo "Este pacote é para uso pessoal. Confira os termos na página de cada projeto."
        echo
        for f in "$P"/emuladores/*; do
            name="$(basename "$f")"; lic=""; url=""
            case "${name,,}" in
                snes9x*)      lic="Licença do Snes9x (uso não comercial)"; url="https://github.com/snes9xgit/snes9x" ;;
                rmg*)         lic="GPL-3.0"; url="https://github.com/Rosalie241/RMG" ;;
                mgba*)        lic="MPL-2.0"; url="https://mgba.io" ;;
                duckstation*) lic="CC BY-NC-ND 4.0 (redistribuição só sem alterações e sem fins comerciais)"; url="https://github.com/stenzek/duckstation" ;;
                pcsx2*)       lic="GPL-3.0"; url="https://pcsx2.net" ;;
                rpcs3*)       lic="GPL-2.0"; url="https://rpcs3.net" ;;
                shadps4*)     lic="GPL-2.0"; url="https://github.com/shadps4-emu/shadPS4" ;;
                xemu*)        lic="GPL-2.0"; url="https://xemu.app" ;;
                dolphin*)     lic="GPL-2.0-or-later"; url="https://dolphin-emu.org" ;;
                eden*)        lic="GPL-3.0"; url="https://eden-emu.dev" ;;
                retroarch*)   lic="GPL-3.0"; url="https://www.retroarch.com" ;;
                setup-emuladores.sh|LICENCAS.txt) continue ;;
                *)            lic="veja a página do projeto" ;;
            esac
            printf '%-48s %s\n%-48s %s\n\n' "$name" "$lic" "" "${url:-}"
        done
    } > "$P/emuladores/LICENCAS.txt"
fi

# ------------------------------------------------------------------ verificação e tar
echo ">> Somas SHA-256"
(cd "$P" && find -L . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum | sed 's#  \./#  #') > "$STAGE/SHA256SUMS"
mv "$STAGE/SHA256SUMS" "$P/SHA256SUMS"

total_kb="$(du -skL "$P" | cut -f1)"
free_kb="$(df -Pk "$OUT" | awk 'NR==2 {print $4}')"
[ "$free_kb" -gt "$total_kb" ] || { echo "Espaço insuficiente em $OUT: precisa de ~$((total_kb/1024)) MB." >&2; exit 1; }

echo ">> Compactando ($((total_kb/1024)) MB)…"
rm -f "$ARCHIVE"
# os AppImages já são comprimidos: gzip rápido (-1) quase não perde nada e é bem mais veloz
tar -C "$STAGE" -h --owner=0 --group=0 --numeric-owner --sort=name -cf - "$NAME" \
    | gzip -1 > "$ARCHIVE.part"
mv "$ARCHIVE.part" "$ARCHIVE"
sha256sum "$ARCHIVE" | sed "s#  .*/#  #" > "$ARCHIVE.sha256"

echo
echo "Pacote pronto: $ARCHIVE ($(( $(stat -c %s "$ARCHIVE") / 1048576 )) MB)"
echo "Soma SHA-256:  $ARCHIVE.sha256"
echo
echo "Na outra máquina:"
echo "  tar -xzf $(basename "$ARCHIVE")"
echo "  cd $NAME && bash instalar.sh"
