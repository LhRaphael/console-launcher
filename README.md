# Console Launcher

Interface principal da distro de emulação. Tudo é navegável pelo controle:

- **Abas por console:** Recentes, SNES, N64, GBA, PS1, PS2, PS3, PS4, Xbox,
  GameCube, Wii, Switch e **Apps** (os aplicativos instalados, incluindo Flatpak).
- **Abrir um jogo:** você escolhe o jogo e o launcher abre o emulador certo, já
  com o jogo carregado. O emulador só roda o jogo.
- **Configurar cada console:** versão do emulador, tela cheia, renderizador,
  resolução, tela larga, overlay e argumentos extras, tudo na própria interface.
- **Configurar cada jogo:** qualquer opção do console pode ser trocada só para
  aquele jogo. Também dá para mudar o nome exibido.
- **Importar jogos:** traz jogos de qualquer pasta ou pendrive para a pasta do
  console, extrai os compactados e apaga os restos.
- **Atalhos em todo o sistema:** o controle e o teclado funcionam mesmo com um
  jogo ou aplicativo em foco.
- **Teclado na tela:** para digitar nomes, argumentos e senhas de Wi‑Fi com o controle.
- **Menu lateral** (Start, Guia ou ← na primeira coluna): apps abertos,
  arquivos, biblioteca, som, Bluetooth, Wi‑Fi e rede, tela, launcher, este
  computador e energia.
- **Gerenciador de arquivos** (menu lateral → Arquivos, ou F6): todas as pastas,
  prévia de imagens, espaço livre dos discos, copiar/mover/renomear/lixeira, e
  "Jogar" ou "Importar" direto de um arquivo de jogo.

## Instalar

Requisitos: distro baseada em Debian, x86_64, com Qt 6.4 ou mais novo (Debian
12/13, Ubuntu 24.04+, Linux Mint 22+, Pop!_OS 24.04 e afins).

### Em outra máquina, com os emuladores (pacote completo)

Na máquina onde os emuladores já estão em `~/emulators`, gere o pacote:

```bash
bash scripts/montar-pacote-completo.sh
```

Isso cria `dist/console-launcher-completo-<versão>-x86_64.tar.gz` e um arquivo
`.sha256` com a soma. O pacote traz:

- o programa (`.deb`) e o código-fonte;
- todos os AppImages e o `.flatpak` de `~/emulators`;
- os scripts `instalar.sh`, `executar.sh` e `desinstalar.sh`, com um `LEIA-ME.txt`.

BIOS, firmware, chaves, jogos e saves nunca entram. O pacote é para uso
pessoal: cada emulador segue a própria licença (veja `emuladores/LICENCAS.txt`),
então não o publique.

Na outra máquina, descompacte numa pasta do disco (não direto no pendrive) e
instale como o seu usuário normal:

```bash
tar -xzf console-launcher-completo-*.tar.gz
cd console-launcher-*/
bash instalar.sh               # ou: bash instalar.sh --autostart (modo console)
bash executar.sh               # abre; --janela para testar numa janela
```

O `instalar.sh` faz, nesta ordem:

1. Confere as somas SHA-256 dos arquivos.
2. Instala as dependências pelo nome que a distro usa (FUSE 2, Vulkan,
   GameMode, GStreamer, 7zip/unar, Flatpak).
3. Instala o `.deb`. Se a distro não aceitar o pacote pronto, compila a partir
   do código que veio junto.
4. Copia os emuladores para `~/emulators`, dá permissão de execução e instala o
   Dolphin (Flatpak).

Ele guarda um registro em `~/.local/state/console-launcher/instalacao-*.log`.
Para ver as opções: `bash instalar.sh --help`.

`bash desinstalar.sh --tudo` remove o programa, os emuladores que o instalador
copiou e as configurações. Jogos, BIOS e saves ficam.

### Na mesma máquina, a partir do código

```bash
bash scripts/install.sh                # usa o binário pronto em bin/
bash scripts/install.sh --build        # compila do código-fonte
bash scripts/install.sh --autostart    # e abre em tela cheia ao entrar na sessão
```

Para remover: `bash scripts/uninstall.sh` (com `--purge`, apaga também as
configurações).

Para importar arquivos `.7z` e `.rar`, instale as ferramentas de extração:
`sudo apt install 7zip unar` (o `.deb` já as recomenda).

## Atalhos

| Ação | Controle | Teclado |
|---|---|---|
| Navegar | D-pad / analógico | setas |
| Jogar / abrir app | A (✕) | Enter |
| Trocar de aba | LB / RB | Q / E |
| Opções do jogo | X (□) | F2 |
| Configurar o console | Y (△) | F3 |
| Importar jogos | Select / View / Share | F4 |
| Atualizar biblioteca | — (pelo menu) | F5 |
| Menu lateral | Start, Guia ou ← na 1ª coluna | F1 |
| Voltar / fechar painel | B (○) | Esc |
| **Com um jogo ou app aberto:** | | |
| Voltar para o launcher | toque no Guia | Super+Esc |
| Fechar o jogo | segurar Guia ou Start+Select (~1 s) | Super+Shift+Esc |

Os atalhos globais de teclado (Super+Esc) funcionam em sessões X11. No Wayland,
ou para usar outras teclas, crie um atalho no próprio sistema apontando para um
destes comandos:

```bash
console-launcher --action show            # traz o launcher para frente
console-launcher --action close-game      # fecha o jogo em execução
console-launcher --action menu
console-launcher --action toggle-overlay
```

Só existe uma instância do launcher. Abrir de novo apenas traz a que já está
aberta para frente.

## Menu lateral

| Seção | O que faz |
|---|---|
| **Abertos** | O jogo e os apps abertos pelo launcher. A volta para ele; X (duas vezes) fecha. |
| **Biblioteca** | Configurar e importar jogos do console atual; atualizar a biblioteca. |
| **Som** | Volume (← →), sem som e a saída de áudio (HDMI, fone, caixa). |
| **Bluetooth** | Liga/desliga, procura, pareia e conecta controles e fones; X esquece. |
| **Wi‑Fi e rede** | Estado do cabo e do Wi‑Fi, procurar e conectar (senha pelo teclado na tela); X esquece a rede. |
| **Tela** | Resolução e taxa de atualização. Volta sozinha em 15 s se você não confirmar. |
| **Launcher** | Overlay LinOSD, lista de atalhos, painel completo do sistema, sair para a área de trabalho. |
| **Energia** | Suspender, encerrar sessão, reiniciar e desligar (os três últimos pedem confirmação). |

Na coluna de opções: ↑↓ escolhe, ← → ajusta valores, A ativa, X faz a ação
extra do item, e B volta. As chaves liga/desliga só mudam com A, para não
desligar o Wi‑Fi ou o Bluetooth sem querer.

Ferramentas usadas, todas padrão do Debian:

- **Som:** `pactl` ou `wpctl` (PipeWire/WirePlumber).
- **Bluetooth:** `bluetoothctl` (pacote bluez).
- **Rede:** `nmcli` (NetworkManager).
- **Tela:** depende do ambiente.
  - GNOME: o próprio GNOME, pelo D-Bus.
  - KDE: `kscreen-doctor`.
  - Sway/labwc: `wlr-randr`.
  - X11: `xrandr`.

**Limitação do Wayland:** um programa não pode listar nem trazer para frente
as janelas de outros programas. Por isso "Abertos" mostra o que foi aberto
pelo launcher, e "voltar" minimiza o launcher para revelar o app.

## Gerenciador de arquivos

| Ação | Controle | Teclado |
|---|---|---|
| Abrir pasta / arquivo | A (ou →) | Enter |
| Subir uma pasta | B (ou ←) | Esc |
| Opções do item | X | F2 |
| Marcar vários | Y | F3 |
| Opções da pasta | Select | F4 |
| Trocar de local | LB / RB | Q / E |
| Fechar | Start | F1 |

- **Coluna da esquerda:** Pasta pessoal, Downloads, Documentos, Imagens,
  Vídeos, Músicas, ROMs, Emuladores, Lixeira, Sistema e pendrives/HDs. Os discos
  mostram uma barra com os GB livres.
- **Coluna da direita:** detalhes do item (tipo, tamanho, data, local,
  permissão) e a prévia de imagens.
- **Abrir um arquivo:**
  - Arquivos comuns abrem no aplicativo padrão do sistema.
  - Jogos (`.iso`, `.chd`, `.cue`…) e pastas de jogo de PS3/PS4 mostram as
    opções **Jogar no <console>** (direto no emulador, sem importar) e
    **Importar para <console>** (vai para a biblioteca, com limpeza).
- **Opções do item (X):** copiar, recortar, renomear (teclado na tela), mover
  para a lixeira e apagar permanentemente (pede confirmação).
- **Opções da pasta (Select):** colar, nova pasta, mostrar ocultos, ordenar por
  nome/data/tamanho e esvaziar a lixeira.
- **Cópias grandes:** rodam em segundo plano com barra de progresso, e o B
  cancela.
- **Proteções:** o launcher só altera arquivos na sua pasta pessoal e em discos
  externos. A pasta pessoal, Downloads, ROMs, emuladores e a raiz dos discos não
  podem ser apagadas nem renomeadas.

## Este computador

No menu lateral, a seção **Este computador** mostra:

- **Armazenamento:** os GB livres de cada disco, com barra que fica amarela
  acima de 75% e vermelha acima de 90%.
- **Memória:** RAM em uso e livre.
- **Computador:** sistema, kernel, processador, placa de vídeo, tempo ligado e
  sessão gráfica.

As informações se atualizam sozinhas enquanto a seção está aberta.

## Temas dos consoles

Cada aba tem uma estética que lembra o seu console: tipo de letra, cores, cartões
e um fundo desenhado (animado de leve). A troca de aba é suave. Nenhum logotipo
ou marca é reproduzido: são elementos genéricos de cada época.

| Aba | Tema | Letras | Fundo |
| --- | --- | --- | --- |
| SNES | 16 bits | Luckiest Guy, cada letra numa das quatro cores dos botões, com contorno e sombra | nuvens e faixa de blocos de pixel |
| N64 | 3D colorido | Titan One, letras gordas em vermelho/azul/verde/amarelo, cada uma com profundidade | piso quadriculado em perspectiva andando e poliedros chapados flutuando |
| GBA | Portátil 32 bits | Press Start 2P (pixel) com sombra dura | índigo com grade de LCD e pixels piscando |
| PS1 | Polígonos 32 bits | Exo 2, larga | preto e cinza com triângulos chapados girando |
| PS2 | Torres azuis | Orbitron, só o contorno, em degradê violeta→ciano | névoa azul e "torres" subindo |
| PS3 | Onda | Michroma, larga e prateada | fitas ondulando sobre uma cor que muda com o mês |
| PS4 | Azul profundo | Titillium fina e espaçada | azul profundo com luzes subindo |
| Xbox | Verde radioativo | Russo One com brilho verde | grade de hexágonos e brilho pulsando |
| GameCube | Cubo roxo | Bungee com profundidade (3D) | padrão de cubos isométricos |
| Wii | Canais brancos | Varela Round | claro, listras finas, cantos bem arredondados |
| Switch | Neon | Rubik, metade vermelha e metade azul | cinza-escuro com bordas vermelho/azul neon |

Cada nome de aba também aparece na letra do seu console.

**Desligar:** menu lateral ▸ Launcher ▸ **Temas dos consoles**. Lá também dá
para desligar só a animação do fundo.

**Com papel de parede:** o fundo desenhado do tema some, mas as letras e as
cores continuam.

**Fontes:** livres (SIL OFL 1.1, e Apache 2.0 na Luckiest Guy), em
`data/fonts`, com as licenças.

## Papel de parede

Fica em menu lateral ▸ **Tela** ▸ **Papel de parede**.

- **Fundo:**
  - O padrão do launcher.
  - Uma imagem ou um vídeo (sem som, em loop).
  - Uma pasta: o fundo troca sozinho de tempos em tempos (de 1 minuto a 1 hora,
    ou só quando você pedir), em sequência ou em ordem aleatória. "Trocar agora"
    passa para o próximo.
- **Geral ou por aba:** em **Editando**, escolha "Geral" (vale para todas as
  abas) ou "Só a aba …". Cada console, e também Recentes e Apps, pode ter o
  próprio fundo. "Usar o geral" volta ao comum.
- **Escurecer o fundo** e **Enquadramento** (preencher a tela ou mostrar
  inteiro) deixam os textos legíveis.
- **Atalho pelo gerenciador de arquivos:** o menu de uma imagem ou vídeo tem
  "Usar como papel de parede", e o menu da pasta tem "Usar esta pasta".
- **Transição suave:** toda troca (de aba, de arquivo ou automática) é um
  esmaecimento de ~1 s. A imagem nova só aparece quando já está carregada.
- **Durante o jogo:** o vídeo pausa e volta quando o launcher reaparece.

Para vídeo, é preciso o pacote `qml6-module-qtmultimedia` (o instalador já o
coloca). Sem ele, só imagens funcionam.

## Mouse

A interface inteira também funciona com mouse, de qualquer marca. Mouses com
botões laterais (como o HyperX Pulsefire) usam os dois botões extras para
voltar e avançar.

- **Passar por cima:** seleciona o jogo, a linha ou o arquivo.
- **Clique:**
  - Num jogo já selecionado, o jogo abre.
  - Nas listas, ativa a opção.
  - Nas dicas do rodapé ("Enter Jogar", "Esc Voltar"…), faz o mesmo que
    apertar o botão.
- **Botão direito:**
  - Num jogo: opções do jogo.
  - Num arquivo: menu do arquivo. No espaço vazio do gerenciador: menu da pasta.
  - Na importação: marca ou desmarca.
  - Numa opção de escolha: volta uma opção.
  - No espaço vazio da tela inicial: abre o menu lateral.
- **Roda:** rola as listas. Sobre as abas, ou inclinada para os lados, troca de
  console.
- **Ctrl+clique:** marca vários arquivos.
- **Volume e brilho:** clique ou arraste na barra para ajustar.
- **Botões laterais voltar/avançar:** andam pelo histórico. Também funcionam
  Alt+←/→, as teclas Voltar/Avançar de teclados multimídia e as setas ‹ ›
  no canto superior esquerdo (só aparecem quando o mouse está em uso).
- **Cursor:** some quando você volta a usar o controle ou o teclado, e reaparece
  ao mexer o mouse.

### Histórico (voltar/avançar)

O launcher guarda as telas por onde você passou numa pilha, como um navegador
de uma aba só:

- Cada aba de console, seção do menu lateral, pasta no gerenciador de arquivos,
  painel de configuração, pasta na tela de importação e registro aberto conta
  como uma "página".
- **Voltar** reabre a página anterior do jogo como estava; **avançar** refaz o
  caminho.
- Abrir uma página nova depois de voltar descarta o que estava "à frente".
- O histórico guarda as últimas 60 páginas.
- O teclado na tela, as confirmações e uma importação em andamento não são
  páginas: o botão voltar só os fecha.

## Modo jogo

Quando um jogo abre, a máquina fica só para ele. Fica em menu lateral ▸
Launcher ▸ **Modo jogo**.

- **Tela cheia sempre:** todo jogo abre em tela cheia, mesmo que um console ou
  jogo esteja com a opção desligada.
- **Programas em segundo plano:** são pausados enquanto o jogo roda e voltam
  sozinhos quando ele fecha. Nada se perde. Você escolhe por categoria:
  - Navegadores, terminais (e o que roda neles) e gerenciadores de arquivos.
  - Editores e escritório.
  - Indexação de arquivos.
  - Lojas e atualizações.
  - Papel de parede animado (VideoWallpaper).
  - Apps abertos pelo launcher.
  - "Comunicação" (Discord, Telegram…) vem desligada, porque muita gente usa
    voz durante o jogo.
- **"Fechar" em vez de "Pausar":** existe a opção, mas o que não estiver salvo
  nesses programas se perde.
- **O que nunca é pausado:** o emulador, o launcher, gravação de tela (OBS,
  GPU Screen Recorder, wf-recorder…), métricas (LinOSD, MangoHud), áudio
  (PipeWire), a área de trabalho e o Steam. O programa que abriu um deles também
  fica rodando (ex.: o terminal de onde você abriu o OBS).
- **Energia, tela e notificações:** durante o jogo o launcher pede o perfil de
  energia "desempenho" (se a máquina tiver), impede a tela de apagar e liga o
  "não perturbe".
- **GameMode (Feral):** se estiver instalado, o jogo é registrado nele (CPU e
  GPU no máximo).
- **Menu lateral ▸ Abertos:** mostra o que está pausado e tem **Retomar agora**.
  Voltar para um app pausado retoma tudo antes.
- **Se o launcher cair no meio do jogo:** o que ficou pausado volta na próxima
  vez que ele abrir. Também dá para retomar na hora com
  `console-launcher --resume-apps`.

## Registros dos jogos

Cada vez que um jogo ou emulador abre pelo launcher, tudo o que ele imprime fica
salvo num registro próprio, em `~/.local/state/console-launcher/logs/`.
O arquivo `ultimo-jogo.log` aponta para o mais recente. São guardados os 80
mais recentes.

- **Menu lateral ▸ Registros dos jogos:** lista as últimas execuções com o
  resultado (fechou normalmente, erro com código, fechado pelo launcher, em
  execução). Quando reconhece o erro, mostra a causa provável: FUSE, BIOS,
  chaves, firmware, Vulkan, biblioteca faltando, janela, travamento.
- **Abrir um registro:** mostra a saída completa. ↑↓ rolam e LB/RB passam
  página. A joga de novo, X apaga o registro (aperte duas vezes), Y abre a
  pasta e B fecha.
- **Quando um jogo fecha logo ao abrir:** o registro abre sozinho.

## Configuração por console e por jogo

Use **Y** para configurar o console da aba e **X** para configurar o jogo
selecionado.

- As opções de **tela cheia** e **abertura da BIOS** são passadas na linha de
  comando do emulador.
- **Renderizador, resolução e tela larga** são gravados no arquivo de
  configuração do emulador antes de cada jogo: `settings.ini` do DuckStation,
  `PCSX2.ini` e `config.yml` do RPCS3. No Dolphin elas vão pela linha de
  comando (`-C`).
  - "Padrão do emulador" não mexe no arquivo.
  - Se o arquivo ainda não existe, abra o emulador uma vez em
    *Configurar → Abrir o emulador*. O launcher nunca cria esses arquivos sozinho.
- Uma opção de jogo diferente de "Igual ao console" vale só para aquele jogo.
  Ao abrir outro jogo, o valor do console volta a ser gravado.
- *Abrir o emulador* abre a interface completa do emulador, para controles,
  BIOS e firmware, com o launcher esperando ele fechar.

As escolhas ficam em `~/.config/console-launcher/settings.json`. A lista de
consoles e de opções disponíveis fica em `~/.config/console-launcher/systems.json`.

## Importar jogos

Pressione **Select (F4)** na aba de um console.

1. **Navegar:** escolha a pasta. **LB/RB** trocam entre Downloads, a pasta
   pessoal e pendrives.
2. **Marcar:** marque com **X** ou marque tudo com **Y**. Pastas de jogo do
   PS3/PS4 podem ser marcadas inteiras.
3. **Importar:** pressione **Select** e confirme.

O que acontece:

- **Compactados** (`.zip`, `.7z`, `.rar`, `.tar.*`, inclusive divididos em
  partes `.001`/`.part1.rar`) são extraídos, e os jogos de dentro vão para a
  pasta do console.
- **Arquivos ligados ao jogo** vão junto: as faixas `.bin` do `.cue`, os discos
  do `.m3u`, o `.img/.sub` do `.ccd` e uma capa com o mesmo nome. Se o `.cue`
  escreve o nome com maiúsculas diferentes do arquivo real, o arquivo é
  renomeado para o nome do `.cue`.
- **Vários discos sem `.m3u`** (PS1) viram uma pasta do jogo mais uma playlist
  `.m3u`, e aparecem como um item só.
- **Limpeza** (pode ser desligada na confirmação):
  - Apaga o compactado depois que o jogo saiu dele sem erros.
  - Nas pastas escolhidas, apaga os restos (`.txt`, `.nfo`, `.url`, imagens,
    PDFs) e as pastas que ficarem vazias.
- **Não é apagado:**
  - Compactado com senha.
  - Compactado sem jogo daquele console.
  - Jogo que já existe na biblioteca.
  - Qualquer coisa que deu erro.
  - Esses casos aparecem no resumo final.
- **Pastas que não podem ser importadas inteiras:** a pasta pessoal, Downloads,
  Documentos e a raiz de um pendrive. Para essas, escolha os jogos dentro delas.
- **Outro disco:** de um pendrive, os arquivos são copiados e a origem só é
  apagada depois da cópia completa.
- **PS3/PS4 em `.pkg`:** precisam ser instalados pelo próprio RPCS3/shadPS4.

## BIOS e firmware

O launcher não traz nem baixa BIOS, chaves ou firmware. Faça o dump do seu
próprio console. O launcher identifica cada arquivo pelo conteúdo (o nome não
importa), copia para a pasta que o emulador lê e ajusta a configuração quando
precisa. Os originais ficam onde estão.

**Onde:**
- Configurar o console (Y/F3) ▸ seção **BIOS** ▸ Importar.
- Menu lateral ▸ **BIOS e firmware**: situação de todos os consoles.
- No gerenciador de arquivos, A ou X num arquivo de BIOS ▸ "Instalar no …".
- No terminal: `console-launcher --import-bios ps2 ~/Downloads/bios.zip` e
  `console-launcher --bios-status`.

Aceita arquivos soltos, pastas e compactados (zip, 7z, rar, tar).

| Console | Emulador | O que importar | Para onde vai |
| --- | --- | --- | --- |
| PS1 | DuckStation | BIOS 512 KB (reconhece os modelos SCPH pela soma MD5) | `~/.local/share/duckstation/bios` |
| PS2 | PCSX2 | BIOS 4 MB (lê versão e região) | `~/.config/PCSX2/bios`, e grava a escolhida no `PCSX2.ini` |
| PS3 | RPCS3 | `PS3UPDAT.PUP` | o RPCS3 instala (`--installfw`); feche-o ao terminar |
| PS4 | shadPS4 | módulos `.sprx` e as pastas `font`/`font2` (opcional) | `~/.local/share/shadPS4/sys_modules` e `fonts` |
| Xbox | xemu | MCPX 1.0 (512 B), BIOS flash (256 KB/1 MB), disco `.qcow2`, EEPROM | `~/.local/share/xemu/xemu`, e grava os caminhos no `xemu.toml` |
| GBA / GB / GBC | mGBA | `gba_bios.bin` 16 KB, `gb_bios.bin`, `gbc_bios.bin` (todas opcionais: o mGBA tem uma embutida) | `~/.config/mgba/bios`, e grava os caminhos no `config.ini` do mGBA |
| GameCube | Dolphin | IPL 2 MB (opcional) | `…/dolphin-emu/GC/USA·EUR·JAP/IPL.bin` |
| Switch | Eden | `prod.keys`, `title.keys` e o firmware (`.nca`, solto ou em zip) | `~/.local/share/eden/keys` e `nand/system/Contents/registered` |

Detalhes:
- **Pastas personalizadas:** se você mudou a pasta da BIOS no DuckStation ou no
  PCSX2, o launcher lê a configuração deles e usa a mesma pasta.
- **Arquivo repetido:** o que já está instalado com o mesmo conteúdo não é
  copiado de novo.
- **Mesmo nome, conteúdo diferente:** um arquivo de nome fixo (IPL.bin,
  prod.keys, eeprom.bin) que seria trocado é guardado antes como `.bak`.
- **Firmware do Switch:** é montado numa pasta nova e só substitui o antigo no
  fim, para o firmware antigo não se perder se a cópia falhar.
- **Abrir um jogo sem a BIOS:** o launcher avisa. Apertar de novo em até 5 s
  abre mesmo assim.

## Onde ficam os jogos

| Aba | Pasta | Formatos | Emulador |
|---|---|---|---|
| SNES | `~/ROMs/snes` | .sfc .smc .fig .swc .bs .zip | Snes9x |
| N64 | `~/ROMs/n64` | .z64 .n64 .v64 .ndd .d64 .zip .7z | RMG (Rosalie's Mupen GUI) |
| GBA | `~/ROMs/gba` | .gba .agb .gb .gbc .sgb .zip .7z | mGBA |
| PS1 | `~/ROMs/ps1` | .cue .chd .pbp .m3u .ecm .mds .ccd | DuckStation |
| PS2 | `~/ROMs/ps2` | .iso .chd .cso .zso | PCSX2 |
| PS3 | `~/ROMs/ps3/<pasta do jogo>/` | pasta com `PS3_GAME/USRDIR/EBOOT.BIN` | RPCS3 |
| PS4 | `~/ROMs/ps4/<pasta do jogo>/` | pasta com `eboot.bin` | shadPS4 |
| Xbox | `~/ROMs/xbox` | .iso (XISO) | xemu |
| GameCube | `~/ROMs/gc` | .iso .gcm .gcz .rvz .ciso | Dolphin (Flatpak) |
| Wii | `~/ROMs/wii` | .iso .wbfs .rvz .gcz .wia | Dolphin (Flatpak) |
| Switch | `~/ROMs/switch` | .nsp .xci | Eden |

**Emulador fora de `~/emulators`:** o N64 e o GBA também funcionam com o
emulador instalado pelo sistema (`mgba-qt`, `RMG`) ou pelo Flatpak
(`io.mgba.mGBA`, `com.github.Rosalie241.RMG`). O AppImage em `~/emulators` tem
preferência. No Flatpak, o launcher libera a pasta `~/ROMs` para o emulador ler
os jogos. Isso vale para qualquer console que tenha `"fallback"` no
`systems.json`.

As **capas** são imagens com o mesmo nome do jogo, ao lado do arquivo ou em
`<console>/media/covers/`. O PS3/PS4 usa o ícone do próprio jogo.

BIOS, firmware e chaves **não** vêm com o launcher. Elas devem ser extraídas do
seu próprio console.

## Aba Apps

A aba lê os atalhos `.desktop` do sistema, do usuário e do Flatpak. Ela se
atualiza sozinha quando você instala ou remove um programa. Ficam de fora os
atalhos ocultos (`NoDisplay`) e os de terminal.

Ao abrir um app, toque no **Guia** ou use **Super+Esc** para voltar ao launcher.

## Problemas

- **"… não encontrado em ~/emulators":** o nome do AppImage não bate com o
  padrão em `exe` (no `systems.json`). Também dá para escolher a versão em
  *Configurar → Versão do emulador*.
- **"sem permissão de execução":** rode `bash ~/emulators/setup-emuladores.sh`.
- **"fechou logo ao abrir":** o registro do jogo abre sozinho, com a causa
  provável em destaque. Geralmente falta BIOS ou firmware.
- **"AppImages require FUSE" / "error loading libfuse.so.2":** falta o FUSE 2.
  Instale com `sudo apt install libfuse2t64` (Debian 13) ou
  `sudo apt install libfuse2` (Debian 12). Enquanto isso, o launcher abre os
  AppImages no modo extrair-e-rodar: funciona, mas demora mais para abrir.
- **Opção "não aplicada":** o arquivo de configuração do emulador ainda não
  existe. Abra o emulador uma vez.
- **Atualização do `systems.json`:** quando a versão instalada traz um formato
  novo, o arquivo antigo é salvo como `systems.json.bak`.

## Desenvolvimento

```bash
bash scripts/install-deps.sh
bash build.sh
./build/console-launcher --windowed --size 1600x900 --tab ps2
```

### Publicar no GitHub

Crie um repositório **vazio** no GitHub e rode:

```bash
bash scripts/publicar-github.sh https://github.com/USUARIO/REPOSITORIO.git
```

Vai só o código. O script recusa arquivos grandes e qualquer coisa com cara de
BIOS, jogo ou emulador. Para enviar alterações depois, rode
`bash scripts/publicar-github.sh` sem o endereço. Com `--release`, ele também
cria a release `v<versão>` com o `.deb` (precisa do `gh`).

O código é em C++17, com Qt 6 (Qt Quick/QML), SDL2 e xcb.
