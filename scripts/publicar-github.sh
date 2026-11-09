#!/usr/bin/env bash
# publicar-github.sh — envia o código do Console Launcher para um repositório no GitHub.
#
#   1. Crie um repositório VAZIO no GitHub (sem README, sem licença): github.com/new
#   2. Rode:
#        bash scripts/publicar-github.sh https://github.com/USUARIO/REPOSITORIO.git
#      ou, com chave SSH configurada:
#        bash scripts/publicar-github.sh git@github.com:USUARIO/REPOSITORIO.git
#
#   Depois, para mandar alterações novas:  bash scripts/publicar-github.sh
#
#   --release   também cria a release v<versão> com o .deb (precisa do gh: sudo apt install gh)
#
# Vai só o código. Ficam de fora (.gitignore): build/, dist/, bin/, emuladores, BIOS,
# jogos e compactados. O script confere isso antes de enviar.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

URL=""; RELEASE=0
for a in "$@"; do
    case "$a" in
        --release) RELEASE=1 ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "Opção desconhecida: $a" >&2; exit 2 ;;
        *) URL="$a" ;;
    esac
done

command -v git >/dev/null || { echo ">> Instalando o git"; sudo apt-get install -y git; }
VERSION="$(sed -n 's/^project(console-launcher VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"

# ------------------------------------------------------------------ repositório
if [ ! -d .git ]; then
    git init -q -b main
    echo ">> Repositório git criado"
fi
if [ -z "$(git config user.name || true)" ] || [ -z "$(git config user.email || true)" ]; then
    echo "O git precisa de um nome e e-mail para assinar os commits (ficam públicos no GitHub)."
    read -r -p "  Nome: " n; read -r -p "  E-mail (pode ser o noreply do GitHub): " e
    git config user.name "$n"; git config user.email "$e"
fi

git add -A

# ------------------------------------------------------------------ conferência
bad="$(git diff --cached --name-only --diff-filter=AM -z | xargs -0 -r -I{} sh -c '
    s=$(stat -c %s "{}" 2>/dev/null || echo 0)
    [ "$s" -gt 10485760 ] && echo "{} ($((s/1048576)) MB)"' || true)"
if [ -n "$bad" ]; then
    echo "Arquivos grandes demais para o repositório (o GitHub recusa > 100 MB; aqui o limite é 10 MB):" >&2
    echo "$bad" | sed 's/^/  /' >&2
    echo "Tire-os da pasta ou acrescente ao .gitignore." >&2
    git reset -q; exit 1
fi
if git diff --cached --name-only | grep -Eiq '\.(iso|chd|nsp|xci|nca|keys|pup|sfc|smc|z64|n64|gba|gbc|bin|appimage|flatpak)$'; then
    echo "Há arquivos com cara de BIOS/jogo/emulador no que seria enviado:" >&2
    git diff --cached --name-only | grep -Ei '\.(iso|chd|nsp|xci|nca|keys|pup|sfc|smc|z64|n64|gba|gbc|bin|appimage|flatpak)$' | sed 's/^/  /' >&2
    git reset -q; exit 1
fi

if git diff --cached --quiet; then
    echo ">> Nada novo para registrar"
else
    first=0; git rev-parse -q --verify HEAD >/dev/null || first=1
    if [ $first = 1 ]; then msg="Console Launcher $VERSION"; else
        read -r -p "Descreva a alteração (Enter = \"Console Launcher $VERSION\"): " msg
        msg="${msg:-Console Launcher $VERSION}"
    fi
    git commit -q -m "$msg"
    echo ">> Commit: $(git log -1 --format='%h %s')"
fi

# ------------------------------------------------------------------ envio
if [ -n "$URL" ]; then
    if git remote get-url origin >/dev/null 2>&1; then git remote set-url origin "$URL"; else git remote add origin "$URL"; fi
fi
git remote get-url origin >/dev/null 2>&1 || {
    echo "Falta o endereço do repositório: bash scripts/publicar-github.sh https://github.com/USUARIO/REPO.git" >&2; exit 1; }

echo ">> Enviando para $(git remote get-url origin)"
if ! git push -u origin main; then
    cat >&2 <<'EOF'

O envio falhou. O GitHub não aceita a senha da conta pelo HTTPS. Opções:
  - gh:  sudo apt install gh && gh auth login   (e rode este script de novo)
  - SSH: ssh-keygen -t ed25519, cadastre ~/.ssh/id_ed25519.pub em github.com/settings/keys
         e use o endereço git@github.com:USUARIO/REPO.git
  - Token: crie em github.com/settings/tokens e use-o como senha quando o git pedir.
Se o repositório no GitHub não estava vazio, crie outro vazio ou rode: git pull --rebase origin main
EOF
    exit 1
fi

if [ $RELEASE = 1 ]; then
    command -v gh >/dev/null || { echo "Para --release instale o gh: sudo apt install gh && gh auth login" >&2; exit 1; }
    deb="$(ls -t dist/console-launcher_"$VERSION"-*_amd64.deb 2>/dev/null | head -1 || true)"
    [ -n "$deb" ] || bash scripts/package-deb.sh
    deb="$(ls -t dist/console-launcher_"$VERSION"-*_amd64.deb | head -1)"
    git tag -f "v$VERSION" && git push -f origin "v$VERSION"
    gh release create "v$VERSION" "$deb" --title "Console Launcher $VERSION" \
        --notes "Pacote .deb para Debian 12/13 e Ubuntu 24.04+ (x86_64). Os emuladores, BIOS e jogos não vêm junto." \
        || gh release upload "v$VERSION" "$deb" --clobber
fi
echo ">> Pronto: $(git remote get-url origin | sed -E 's#^git@github.com:#https://github.com/#; s#\.git$##')"
