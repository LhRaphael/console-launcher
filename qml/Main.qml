import QtQuick
import QtQuick.Window
import "Themes.js" as Themes

Window {
    id: win
    width: launchOptions.width
    height: launchOptions.height
    visible: true
    visibility: launchOptions.windowed ? Window.Windowed : Window.FullScreen
    title: "Console Launcher"
    color: "#08090c"

    // Escala única para toda a interface (desenhada em 1920x1080).
    readonly property real s: Math.max(0.45, Math.min(width / 1920, height / 1080))
    readonly property real margin: 96 * s
    readonly property string padStyle: gamepad.style

    property var systems: library.systems
    property int tabIndex: 0
    property string currentId: ""
    readonly property var currentSystem: (tabIndex >= 0 && tabIndex < systems.length) ? systems[tabIndex] : null
    readonly property bool isApps: currentId === "apps"
    readonly property bool isConsole: currentSystem !== null && currentId !== "apps" && currentId !== "recent"
    property var games: []
    property var indexMemory: ({})      // posição na grade de cada aba
    readonly property var selectedGame: (grid.currentIndex >= 0 && grid.currentIndex < games.length) ? games[grid.currentIndex] : null
    // console usado por Y/Select/opções: o da aba, ou o do jogo selecionado em "Recentes"
    readonly property string actionSystem: isConsole ? currentId
                                          : (currentId === "recent" && selectedGame ? selectedGame.system : "")

    // ------------------------------------------------------------ tema da aba (Themes.js)
    readonly property bool themesOn: wallpapers.themes
    readonly property var theme: Themes.get(themesOn ? currentId : "")
    function themeOf(id) { return Themes.get(themesOn ? id : "") }
    FontLoader { id: fLucky; source: "qrc:/data/fonts/LuckiestGuy.ttf" }
    FontLoader { id: fExo; source: "qrc:/data/fonts/Exo2.ttf" }
    FontLoader { id: fOrbitron; source: "qrc:/data/fonts/Orbitron.ttf" }
    FontLoader { id: fMichroma; source: "qrc:/data/fonts/Michroma.ttf" }
    FontLoader { id: fTitillium; source: "qrc:/data/fonts/TitilliumWeb-Light.ttf" }
    FontLoader { id: fRusso; source: "qrc:/data/fonts/RussoOne.ttf" }
    FontLoader { id: fBungee; source: "qrc:/data/fonts/Bungee.ttf" }
    FontLoader { id: fVarela; source: "qrc:/data/fonts/VarelaRound.ttf" }
    FontLoader { id: fRubik; source: "qrc:/data/fonts/Rubik.ttf" }
    FontLoader { id: fPress; source: "qrc:/data/fonts/PressStart2P.ttf" }
    FontLoader { id: fTitan; source: "qrc:/data/fonts/TitanOne.ttf" }
    function fontFor(key) {
        const m = { lucky: fLucky, exo2: fExo, orbitron: fOrbitron, michroma: fMichroma, titillium: fTitillium,
                    russo: fRusso, bungee: fBungee, varela: fVarela, rubik: fRubik,
                    press: fPress, titan: fTitan }
        const l = m[key]
        return l && l.status === FontLoader.Ready ? l.name : Qt.application.font.family
    }
    readonly property string themeFont: fontFor(theme.font)
    property color fg: theme.fg
    property color fgDim: theme.fgDim
    property color fgMute: theme.fgMute
    Behavior on fg { ColorAnimation { duration: 500 } }
    Behavior on fgDim { ColorAnimation { duration: 500 } }
    Behavior on fgMute { ColorAnimation { duration: 500 } }

    property color accent: theme.accent ? theme.accent : (currentSystem ? currentSystem.color : "#4c8dff")
    Behavior on accent { ColorAnimation { duration: 400 } }

    // ------------------------------------------------------------ lógica
    function reloadGames() {
        const keep = indexMemory[currentId] || 0
        games = currentId ? library.games(currentId) : []
        grid.currentIndex = games.length ? Math.max(0, Math.min(keep, games.length - 1)) : -1
        if (grid.currentIndex >= 0)
            grid.positionViewAtIndex(grid.currentIndex, GridView.Contain)
        gridFade.restart()
        headingFade.restart()
    }

    function selectTab(i) {
        if (systems.length === 0)
            return
        i = (i + systems.length) % systems.length
        if (currentId)
            indexMemory[currentId] = grid.currentIndex
        tabIndex = i
        currentId = systems[i].id
        if (currentId !== "recent")
            library.lastSystem = currentId
        reloadGames()
    }

    // A lista de abas mudou (atualização, aba "Recentes" surgiu):
    // mantém a aba atual pelo id, não pela posição.
    function syncTabs() {
        let idx = -1
        for (let i = 0; i < systems.length; ++i)
            if (systems[i].id === currentId) idx = i
        if (idx < 0) {
            const want = launchOptions.tab || library.lastSystem
            for (let i = 0; i < systems.length; ++i)
                if (systems[i].id === want) idx = i
            if (idx < 0) idx = 0
            currentId = systems.length ? systems[idx].id : ""
        }
        tabIndex = idx
    }

    function systemName(id) {
        for (const sy of systems) if (sy.id === id) return sy.name
        return id
    }
    function systemDirText(id) {
        for (const sy of systems) if (sy.id === id) return sy.dir
        return ""
    }

    function launchSelected() {
        const g = selectedGame
        if (!g) return
        if (g.system === "apps") library.launchApp(g.path)
        else if (biosOk(g.system, g.path)) library.launch(g.system, g.path, g.title, g.cover)
    }

    function openGameOptions() {
        const g = selectedGame
        if (!g || g.system === "apps") return
        options.mode = "game"
        options.gameSystem = g.system
        options.gamePath = g.path
        options.open(g.title, systemName(g.system) + "  ·  " + g.file,
                     () => library.gameOptions(options.gameSystem, options.gamePath))
    }
    // ------------------------------------------------------------ histórico (voltar/avançar)
    // Funciona como um navegador de uma aba só: cada "página" (aba de console, seção do
    // menu lateral, pasta no gerenciador de arquivos, painel de opções, pasta na importação,
    // registro aberto) vira uma entrada. Botões laterais do mouse, Alt+←/→, as teclas
    // Voltar/Avançar e as setas do cabeçalho andam por ela.
    function snapshotState() {
        if (runner.running || displayConfirm.shown) return null
        if (importPanel.shown && importPanel.stage !== "browse") return null
        const st = { tab: win.currentId }
        if (sideBar.shown) st.side = sideBar.section
        if (fileManager.shown) st.files = fileManager.listing.path || ""
        if (options.shown) st.opt = { mode: options.mode, sys: options.gameSystem, path: options.gamePath,
                                      title: options.title, sub: options.subtitle }
        if (importPanel.shown) st.imp = { sys: importPanel.systemId, mode: importPanel.mode, path: importPanel.listing.path || "",
                                          name: importPanel.systemName, dest: importPanel.destText }
        if (logViewer.shown) st.log = logViewer.info.path || ""
        return st
    }
    readonly property string navKey: { const st = snapshotState(); return st ? JSON.stringify(st) : "" }
    onNavKeyChanged: if (navKey !== "") navTimer.restart()
    Timer { id: navTimer; interval: 450; onTriggered: history.visit() }
    Timer { id: restoreDone; interval: 500; onTriggered: { history.restoring = false; history.syncCurrent() } }

    QtObject {
        id: history
        property var entries: []
        property int index: -1
        property bool restoring: false
        readonly property bool canBack: index > 0
        readonly property bool canForward: index >= 0 && index < entries.length - 1
        function visit() {
            if (restoring || win.navKey === "") return
            if (index >= 0 && entries[index].key === win.navKey) return
            let e = entries.slice(0, index + 1)
            e.push({ key: win.navKey, st: win.snapshotState() })
            if (e.length > 60) e = e.slice(e.length - 60)
            entries = e
            index = e.length - 1
        }
        // depois de restaurar, a entrada guarda o estado que de fato ficou na tela
        function syncCurrent() {
            if (index < 0 || win.navKey === "" || entries[index].key === win.navKey) return
            const e = entries.slice()
            e[index] = { key: win.navKey, st: win.snapshotState() }
            entries = e
        }
        function go(i) {
            if (i < 0 || i >= entries.length) return false
            navTimer.stop()
            restoring = true
            index = i
            win.restoreState(entries[i].st)
            restoreDone.restart()
            return true
        }
        function back() {
            if (win.closeTransient()) return
            if (!go(index - 1)) toast.show("Não há página anterior.")
        }
        function forward() {
            if (win.closeTransient()) return
            if (!go(index + 1)) toast.show("Não há próxima página.")
        }
    }
    // janelinhas que não são "páginas": voltar só fecha
    function closeTransient() {
        if (keyboard.shown) { keyboard.finish(false); return true }
        if (displayConfirm.shown) return true
        if (importPanel.shown && importPanel.stage === "running") return true
        if (importPanel.shown && importPanel.stage !== "browse") { importPanel.handleKey({ key: Qt.Key_Escape }); return true }
        if (fileManager.shown && fileManager.menuMode !== "") { fileManager.menuMode = ""; return true }
        if (runner.running) return true
        return false
    }
    function restoreState(st) {
        if (st.tab && st.tab !== currentId) {
            const i = systems.findIndex(x => x.id === st.tab)
            if (i >= 0) selectTab(i)
        }
        if (!st.log && logViewer.shown) logViewer.close()
        if (!st.imp && importPanel.shown) importPanel.close()
        if (!st.opt && options.shown) options.close()
        if (st.files === undefined && fileManager.shown) fileManager.close()
        if (!st.side && sideBar.shown) sideBar.close()
        if (st.side && (!sideBar.shown || sideBar.section !== st.side)) sideBar.open(st.side)
        if (st.files !== undefined) {
            if (!fileManager.shown) fileManager.open(st.files)
            else if (fileManager.listing.path !== st.files) fileManager.load(st.files)
        }
        if (st.opt) {
            const o = st.opt
            const same = options.shown && options.mode === o.mode && options.gameSystem === o.sys && options.gamePath === o.path
            if (!same) {
                if (o.mode === "system") openConsoleOptions(o.sys)
                else if (o.mode === "help") openShortcuts()
                else if (o.mode === "game") {
                    options.mode = "game"; options.gameSystem = o.sys; options.gamePath = o.path
                    options.open(o.title, o.sub, () => library.gameOptions(options.gameSystem, options.gamePath))
                }
            }
        }
        if (st.imp) {
            const m = st.imp
            if (!importPanel.shown || importPanel.systemId !== m.sys || importPanel.mode !== m.mode)
                importPanel.open(m.sys, m.name, m.dest, m.mode)
            if (m.path && importPanel.listing.path !== m.path) importPanel.load(m.path)
        }
        if (st.log && (!logViewer.shown || logViewer.info.path !== st.log)) logViewer.open(st.log)
    }
    Connections {
        target: pointer
        function onBack() { history.back() }
        function onForward() { history.forward() }
    }

    function openConsoleOptions(id) {
        if (!id) { toast.show("Escolha a aba de um console."); return }
        options.mode = "system"
        options.gameSystem = id
        options.open("Configurar " + systemName(id), "Valem para todos os jogos deste console; cada jogo pode ter as suas (X)",
                     () => {
                         const rows = library.systemOptions(options.gameSystem)
                         const extra = bios.optionRows(options.gameSystem)
                         if (!extra.length) return rows
                         // a BIOS entra logo depois de "Abrir o emulador"
                         let at = rows.findIndex(r => r.key === "_open")
                         at = at < 0 ? rows.length : at + 1
                         return rows.slice(0, at).concat(extra, rows.slice(at))
                     })
    }
    function openBiosImport(id) {
        if (!id) { toast.show("Escolha a aba de um console."); return }
        if (!bios.handles(id)) { toast.show(systemName(id) + " não precisa de BIOS."); return }
        if (bios.busy) { toast.show("Já existe uma importação de BIOS em andamento."); return }
        importPanel.open(id, systemName(id), bios.state(id).dir, "bios")
    }
    function installFirmware(id) {
        const err = bios.installFirmware(id)
        if (err) toast.show(err)
    }
    // Sem a BIOS obrigatória o emulador só mostra um erro: avisa antes
    // (apertar de novo em até 5 s abre mesmo assim).
    property string biosWarned: ""
    Timer { id: biosWarnTimer; interval: 5000; onTriggered: win.biosWarned = "" }
    function biosOk(sys, key) {
        if (!sys || sys === "apps" || bios.ready(sys) || biosWarned === key) { biosWarned = ""; return true }
        biosWarned = key
        biosWarnTimer.restart()
        const st = bios.state(sys)
        toast.show(systemName(sys) + ": " + st.summary + ". Importe em Configurar (Y) ▸ BIOS — ou aperte de novo para abrir assim mesmo.")
        return false
    }
    function openImport(id) {
        if (!id) { toast.show("Escolha a aba de um console para importar jogos."); return }
        if (importer.busy) { toast.show("Já existe uma importação em andamento."); return }
        importPanel.open(id, systemName(id), systemDirText(id))
    }
    function openShortcuts() {
        options.mode = "help"
        options.open("Atalhos de teclado e controle",
                     launchOptions.hotkeys.length ? "Atalhos globais ativos: " + launchOptions.hotkeys.join(", ")
                                                  : (launchOptions.hotkeyProblem || ""),
                     () => shortcutRows)
    }
    function openMenu(section) {
        if (options.shown || importPanel.shown || keyboard.shown) return
        sideBar.open(section || "")
    }

    readonly property var shortcutRows: [
        { type: "header", key: "h1", label: "No launcher  (controle · teclado)" },
        { type: "info", key: "s1", label: "Navegar", detail: "D-pad ou analógico esquerdo  ·  setas" },
        { type: "info", key: "s2", label: "Jogar / abrir aplicativo", detail: "A (✕)  ·  Enter" },
        { type: "info", key: "s3", label: "Trocar de aba", detail: "LB / RB  ·  Q / E" },
        { type: "info", key: "s4", label: "Opções do jogo selecionado", detail: "X (□)  ·  F2" },
        { type: "info", key: "s5", label: "Configurar o console", detail: "Y (△)  ·  F3" },
        { type: "info", key: "s5b", label: "BIOS e firmware do console", detail: "Configurar o console ▸ BIOS  ·  menu ▸ BIOS e firmware" },
        { type: "info", key: "s6", label: "Importar jogos para o console", detail: "Select / View / Share  ·  F4" },
        { type: "info", key: "s7", label: "Atualizar biblioteca", detail: "Menu → Biblioteca  ·  F5" },
        { type: "info", key: "s8", label: "Menu lateral (abertos, configurações, energia)", detail: "Start ou Guia, ou ← na primeira coluna  ·  F1" },
        { type: "info", key: "s9", label: "Voltar / fechar painel", detail: "B (○)  ·  Esc" },
        { type: "info", key: "s10", label: "Arquivos", detail: "Menu lateral → Arquivos  ·  F6" },
        { type: "header", key: "h4", label: "No gerenciador de arquivos" },
        { type: "info", key: "f1", label: "Abrir · voltar", detail: "A · B (ou → · ←)" },
        { type: "info", key: "f2", label: "Opções do item (copiar, recortar, renomear, lixeira, jogar)", detail: "X  ·  F2" },
        { type: "info", key: "f3", label: "Marcar vários · opções da pasta (colar, nova pasta, ocultos)", detail: "Y · Select  ·  F3 · F4" },
        { type: "info", key: "f4", label: "Trocar de local", detail: "LB / RB  ·  Q / E" },
        { type: "header", key: "h2", label: "Em qualquer lugar (jogo ou aplicativo aberto)" },
        { type: "info", key: "g1", label: "Voltar para o launcher", detail: "toque no Guia  ·  Super+Esc" },
        { type: "info", key: "g2", label: "Fechar o jogo em execução", detail: "segurar Guia ou Start+Select (1 s)  ·  Super+Shift+Esc" },
        { type: "info", key: "g3", label: "Por comando (atalhos do sistema, Wayland)", detail: "console-launcher --action show | close-game | menu | toggle-overlay" },
        { type: "header", key: "h3", label: "Teclado na tela" },
        { type: "info", key: "k1", label: "Digitar · apagar · espaço · maiúsculas", detail: "A · B · X · Y" },
        { type: "info", key: "k2", label: "Confirmar · cancelar", detail: "Start · Select  (ou Enter · Esc no teclado)" }
    ]

    function countText(n) {
        if (isApps) return n === 1 ? "1 aplicativo" : n + " aplicativos"
        return n === 0 ? "Nenhum jogo" : n === 1 ? "1 jogo" : n + " jogos"
    }

    property bool ready: false
    // adiado: syncTabs altera tabIndex, que depende de systems (evita binding loop)
    onSystemsChanged: if (ready) Qt.callLater(syncTabs)

    Connections {
        target: machine
        function onMessage(text) { toast.show(text) }
        function onPasswordNeeded(ssid) {
            keyboard.open("Senha da rede " + ssid, "", (pw) => machine.connectWifi(ssid, pw), true)
        }
        function onDisplayConfirm(seconds) { displayConfirm.start(seconds) }
    }

    // O emulador fechou logo ao abrir: mostra a causa provável (antes isso ia só para o log)
    Connections {
        target: runner
        function onFailed(text, logFile) {
            // abre o registro na hora (num console não há terminal para ver o erro)
            if (logFile && !importPanel.shown && !keyboard.shown) logViewer.open(logFile)
            else toast.show(text)
        }
    }
    Connections {
        target: gameMode
        function onMessage(text) { toast.show(text) }
    }
    property string pickReturnSection: ""
    Connections {
        target: wallpapers
        function onMessage(text) { toast.show(text) }
        function onPickRequested(scope, kind, start) {
            win.pickReturnSection = sideBar.shown ? sideBar.section : ""
            sideBar.close()
            fileManager.pick(kind, scope, start)
        }
    }
    Connections {
        target: bios
        function onMessage(text) { toast.show(text) }
        function onChanged() { if (options.shown && options.mode === "system") options.reload() }
    }
    Connections {
        target: library
        function onRevisionChanged() {
            indexMemory[currentId] = grid.currentIndex
            reloadGames()
        }
        function onRecentRevisionChanged() {
            if (currentId === "recent") {
                indexMemory.recent = 0
                reloadGames()
            }
        }
        function onMessage(text) { toast.show(text) }
    }

    Component.onCompleted: {
        ready = true
        syncTabs()
        reloadGames()
        if (library.startupMessage)
            toast.show(library.startupMessage)
        const d = launchOptions.demo
        if (d === "menu") sideBar.open()
        else if (d.startsWith("sidebar:")) sideBar.open(d.slice(8))
        else if (d.startsWith("files")) fileManager.open(d.length > 6 ? d.slice(6) : "")
        else if (d === "console") openConsoleOptions(actionSystem)
        else if (d === "game") openGameOptions()
        else if (d === "import") openImport(actionSystem)
        else if (d.startsWith("bios")) openBiosImport(d.length > 5 ? d.slice(5) : actionSystem)
        else if (d === "log") { const l = logs.list(1); if (l.length) { sideBar.open("logs"); logViewer.open(l[0].path) } }
        else if (d === "keyboard") keyboard.open("Nome exibido", selectedGame ? selectedGame.title : "", null)
        else if (d === "help") openShortcuts()
    }

    // ------------------------------------------------------------ teclado / controle
    Item {
        id: input
        anchors.fill: parent
        focus: true
        Keys.onPressed: (e) => {
            e.accepted = true
            // o painel mais acima recebe as teclas
            if (keyboard.shown) { keyboard.handleKey(e); return }
            if (displayConfirm.shown) { displayConfirm.handleKey(e); return }
            if (importPanel.shown) { importPanel.handleKey(e); return }
            if (options.shown) { options.handleKey(e); return }
            if (logViewer.shown) { logViewer.handleKey(e); return }
            if (sideBar.shown) { sideBar.handleKey(e); return }
            if (fileManager.shown) { fileManager.handleKey(e); return }
            if (runningLayer.shown) {
                if (e.key === Qt.Key_Menu || e.key === Qt.Key_F1) { sideBar.open("open"); return }
                if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                    if (runner.running) win.showMinimized()      // volta ao jogo
                } else if (e.key === Qt.Key_F2) {
                    if (runningLayer.closeAsked) { runner.stop(); runningLayer.closeAsked = false }
                    else runningLayer.closeAsked = true
                } else if (e.key === Qt.Key_Escape) {
                    runningLayer.closeAsked = false
                }
                return
            }
            switch (e.key) {
            case Qt.Key_Left: {
                // na primeira coluna, ← abre o menu lateral
                const cols = Math.max(1, Math.floor(grid.width / grid.cellWidth))
                if (games.length === 0 || grid.currentIndex % cols === 0) openMenu()
                else grid.moveCurrentIndexLeft()
                break
            }
            case Qt.Key_Right: grid.moveCurrentIndexRight(); break
            case Qt.Key_Up:    grid.moveCurrentIndexUp(); break
            case Qt.Key_Down:  grid.moveCurrentIndexDown(); break
            case Qt.Key_PageUp: case Qt.Key_Q:
                selectTab(tabIndex - 1); break
            case Qt.Key_PageDown: case Qt.Key_E: case Qt.Key_Tab:
                selectTab(tabIndex + 1); break
            case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space:
                launchSelected(); break
            case Qt.Key_F2:
                openGameOptions(); break
            case Qt.Key_F3:
                openConsoleOptions(actionSystem); break
            case Qt.Key_F4:
                openImport(actionSystem); break
            case Qt.Key_Menu: case Qt.Key_F1: case Qt.Key_M:
                openMenu(); break
            case Qt.Key_F5:
                library.refresh(); break
            case Qt.Key_F6:
                fileManager.open(); break
            case Qt.Key_Home:
                if (games.length) grid.currentIndex = 0
                break
            case Qt.Key_End:
                if (games.length) grid.currentIndex = games.length - 1
                break
            case Qt.Key_Escape: case Qt.Key_Backspace:
                if (grid.currentIndex > 0) grid.currentIndex = 0
                break
            default:
                e.accepted = false
            }
        }
    }

    // ------------------------------------------------------------ fundo
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: win.theme.bg[0] ? win.theme.bg[0] : Qt.darker(win.accent, 2.8)
                           Behavior on color { ColorAnimation { duration: 700 } } }
            GradientStop { position: 0.5; color: win.theme.bg[1]
                           Behavior on color { ColorAnimation { duration: 700 } } }
            GradientStop { position: 1.0; color: win.theme.bg[2]
                           Behavior on color { ColorAnimation { duration: 700 } } }
        }
    }
    ThemeBackdrop {
        anchors.fill: parent
        theme: win.theme
        accent: win.accent
        s: win.s
        animate: wallpapers.themeMotion && !runner.running && win.visibility !== Window.Minimized
                 && win.visibility !== Window.Hidden && !wallpaper.active
        opacity: wallpaper.active ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 900 } }
    }
    // papel de parede (geral ou da aba atual), por cima do fundo padrão
    Wallpaper {
        id: wallpaper
        anchors.fill: parent
        systemId: win.currentId
        playing: !runner.running && win.visibility !== Window.Minimized && win.visibility !== Window.Hidden
        onMessage: (text) => toast.show(text)
    }

    // ------------------------------------------------------------ cabeçalho: abas + relógio
    Item {
        id: header
        x: win.margin
        y: 34 * win.s
        width: parent.width - 2 * win.margin
        height: 76 * win.s

        Row {
            id: navButtons
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10 * win.s
            // só aparecem com o mouse (no controle há LB/RB e B)
            visible: pointer.mouseActive
            width: visible ? implicitWidth : 0
            Repeater {
                model: [{ t: "‹", fwd: false }, { t: "›", fwd: true }]
                delegate: Rectangle {
                    readonly property bool on: modelData.fwd ? history.canForward : history.canBack
                    width: 46 * win.s; height: width; radius: width / 2
                    color: navMouse.containsMouse && on ? "#33ffffff" : "#14ffffff"
                    opacity: on ? 1 : 0.35
                    Text { anchors.centerIn: parent; anchors.verticalCenterOffset: -2 * win.s; text: modelData.t; color: "white"; font.pixelSize: 30 * win.s; font.bold: true }
                    MouseArea {
                        id: navMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: parent.on ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: modelData.fwd ? history.forward() : history.back()
                    }
                }
            }
        }

        ButtonGlyph {
            id: lbGlyph
            button: "lb"; style: win.padStyle; s: win.s
            anchors.left: navButtons.right
            anchors.leftMargin: navButtons.visible ? 22 * win.s : 0
            anchors.verticalCenter: parent.verticalCenter
        }

        ListView {
            id: tabs
            anchors.left: lbGlyph.right
            anchors.leftMargin: 26 * win.s
            anchors.right: rbGlyph.left
            anchors.rightMargin: 26 * win.s
            height: parent.height
            orientation: ListView.Horizontal
            spacing: 42 * win.s
            clip: true
            interactive: false
            model: win.systems
            currentIndex: win.tabIndex
            // com muitos consoles as abas não cabem: rola para manter a atual à vista
            function reveal() {
                const it = currentItem
                if (!it) return
                let t = contentX
                if (it.x - contentX < width * 0.2) t = it.x - width * 0.2
                else if (it.x + it.width - contentX > width * 0.8) t = it.x + it.width - width * 0.8
                t = contentWidth <= width ? originX : Math.max(originX, Math.min(t, originX + contentWidth - width))
                if (Math.abs(t - contentX) < 1) return
                tabScroll.to = t
                tabScroll.restart()
            }
            NumberAnimation { id: tabScroll; target: tabs; property: "contentX"; duration: 300; easing.type: Easing.OutCubic }
            onCurrentIndexChanged: Qt.callLater(reveal)
            onWidthChanged: Qt.callLater(reveal)
            onContentWidthChanged: Qt.callLater(reveal)

            delegate: Item {
                readonly property bool cur: index === win.tabIndex
                width: tabText.implicitWidth + (modelData.id === "apps" ? 0 : 0)
                height: tabs.height

                Rectangle {   // separador antes da aba de aplicativos
                    visible: modelData.id === "apps"
                    x: -22 * win.s
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -4 * win.s
                    width: 2 * win.s; height: 30 * win.s
                    color: "#40ffffff"
                }
                readonly property var tabTheme: win.themeOf(modelData.id)
                Text {
                    id: tabText
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: -4 * win.s
                    text: modelData.short
                    color: win.fg
                    font.family: win.fontFor(tabTheme.font)
                    font.pixelSize: 30 * win.s * (tabTheme.tabScale || 1)
                    font.weight: cur ? Math.max(600, tabTheme.weight || 700) : (tabTheme.font ? (tabTheme.weight || 400) : Font.Normal)
                    opacity: cur ? 1 : (modelData.count > 0 ? 0.62 : 0.32)
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6 * win.s
                    height: 4 * win.s
                    radius: 2 * win.s
                    width: cur ? tabText.width : 0
                    color: win.accent
                    Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: win.selectTab(index) }
            }
            // roda do mouse (ou inclinar a roda para os lados) sobre as abas troca de console
            WheelHandler {
                property real acc: 0
                onWheel: (ev) => {
                    acc += (Math.abs(ev.angleDelta.x) > Math.abs(ev.angleDelta.y) ? ev.angleDelta.x : ev.angleDelta.y)
                    if (Math.abs(acc) >= 120) { win.selectTab(win.tabIndex + (acc < 0 ? 1 : -1)); acc = 0 }
                }
            }
        }

        ButtonGlyph {
            id: rbGlyph
            button: "rb"; style: win.padStyle; s: win.s
            anchors.right: clock.left
            anchors.rightMargin: 56 * win.s
            anchors.verticalCenter: parent.verticalCenter
        }

        Column {
            id: clock
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2 * win.s
            property date now: new Date()
            Timer { interval: 1000; running: true; repeat: true; onTriggered: clock.now = new Date() }
            Text {
                anchors.right: parent.right
                text: Qt.formatDateTime(clock.now, "HH:mm")
                color: win.fg
                font.pixelSize: 34 * win.s
                font.bold: true
            }
            Text {
                anchors.right: parent.right
                text: clock.now.toLocaleDateString(Qt.locale("pt_BR"), "ddd, d 'de' MMMM")
                color: win.fgDim
                font.pixelSize: 17 * win.s
            }
        }
    }

    // ------------------------------------------------------------ título do console
    Column {
        id: heading
        x: win.margin
        y: header.y + header.height + 40 * win.s
        spacing: 8 * win.s

        NumberAnimation on opacity { id: headingFade; from: 0; to: 1; duration: 450; running: false }
        ThemedTitle {
            text: win.currentSystem ? win.currentSystem.name : "Nenhum console configurado"
            theme: win.theme
            family: win.themeFont
            pixelSize: 54 * win.s
            s: win.s
        }
        Row {
            spacing: 14 * win.s
            Text {
                text: win.currentSystem ? win.countText(win.currentSystem.count) : ""
                color: win.fgDim
                font.pixelSize: 21 * win.s
            }
            Text {
                visible: win.currentSystem && win.currentSystem.emulator !== ""
                text: "·   " + (win.currentSystem ? win.currentSystem.emulator : "")
                color: win.fgMute
                font.pixelSize: 21 * win.s
            }
            Text {
                visible: win.currentSystem && !win.currentSystem.available
                text: "·   ⚠ " + (win.currentSystem ? win.currentSystem.problem : "")
                color: "#ff8a80"
                font.pixelSize: 21 * win.s
            }
        }
    }

    // ------------------------------------------------------------ grade de jogos / aplicativos
    GridView {
        id: grid
        x: win.margin - 30 * win.s
        y: heading.y + heading.height + 18 * win.s
        width: parent.width - 2 * win.margin + 60 * win.s
        height: footer.y - y - 8 * win.s
        topMargin: 22 * win.s
        bottomMargin: 22 * win.s
        leftMargin: 12 * win.s
        cellWidth: (win.isApps ? 222 : 258) * win.s
        cellHeight: (win.isApps ? 236 : 372) * win.s
        clip: true
        model: win.games
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds
        highlightRangeMode: GridView.ApplyRange
        preferredHighlightBegin: topMargin
        preferredHighlightEnd: height - bottomMargin
        highlightMoveDuration: 180
        currentIndex: -1
        // botão direito no espaço vazio: menu lateral; inclinar a roda: troca de console
        MouseArea {
            anchors.fill: parent
            z: -1
            acceptedButtons: Qt.RightButton
            onClicked: sideBar.open("")
        }
        WheelHandler {
            orientation: Qt.Horizontal
            property real acc: 0
            onWheel: (ev) => {
                acc += ev.angleDelta.x
                if (Math.abs(acc) >= 120) { win.selectTab(win.tabIndex + (acc < 0 ? 1 : -1)); acc = 0 }
            }
        }

        delegate: GameCard {
            width: grid.cellWidth
            height: grid.cellHeight
            game: modelData
            selected: GridView.isCurrentItem
            accent: win.accent
            s: win.s
            radius: win.theme.cardRadius
            selColor: win.theme.cardSel
            idleColor: win.theme.cardIdle
            onHovered: if (grid.currentIndex !== index) grid.currentIndex = index
            onClicked: { if (grid.currentIndex === index) win.launchSelected(); else grid.currentIndex = index }
            onDoubleClicked: { if (grid.currentIndex !== index) { grid.currentIndex = index; win.launchSelected() } }
            onRightClicked: { grid.currentIndex = index; win.openGameOptions() }
        }

        NumberAnimation on opacity { id: gridFade; from: 0; to: 1; duration: 220; running: false }
    }

    // estado vazio: onde colocar os jogos deste console
    Column {
        visible: win.games.length === 0 && win.currentSystem !== null
        anchors.centerIn: grid
        spacing: 16 * win.s
        width: Math.min(1100 * win.s, parent.width - 2 * win.margin)

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: win.isApps ? "Nenhum aplicativo encontrado"
                  : win.currentSystem ? "Nenhum jogo de " + win.currentSystem.name + " ainda" : ""
            color: "white"
            font.pixelSize: 36 * win.s
            font.bold: true
        }
        Text {
            visible: win.isConsole
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.StyledText
            text: win.currentSystem ? "Importe os jogos ou coloque os arquivos em <b><font color=\"" + win.accent + "\">"
                                      + win.currentSystem.dir + "</font></b>" : ""
            color: "#c8ccd6"
            font.pixelSize: 24 * win.s
        }
        Text {
            visible: win.isConsole
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: win.currentSystem && win.currentSystem.extensions.length
                  ? "Formatos: " + win.currentSystem.extensions.map(e => e.indexOf(" ") >= 0 ? e : "." + e).join("  ")
                  : ""
            color: "#808694"
            font.pixelSize: 19 * win.s
        }
        Item { width: 1; height: 10 * win.s }
        Row {
            visible: win.isConsole
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 40 * win.s
            Hint { button: "select"; label: "Importar jogos"; style: win.padStyle; s: win.s }
            Hint { button: "y"; label: "Configurar " + (win.currentSystem ? win.currentSystem.short : ""); style: win.padStyle; s: win.s }
        }
    }

    // ------------------------------------------------------------ rodapé: item selecionado + dicas
    Rectangle {
        x: win.margin
        width: parent.width - 2 * win.margin
        height: 1
        y: footer.y
        color: "#16ffffff"
    }
    Item {
        id: footer
        x: win.margin
        width: parent.width - 2 * win.margin
        height: 118 * win.s
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18 * win.s

        Column {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - hints.width - 60 * win.s
            spacing: 8 * win.s
            visible: win.selectedGame !== null

            Text {
                width: parent.width
                text: win.selectedGame ? win.selectedGame.title : ""
                color: win.fg
                font.family: win.themeFont
                font.pixelSize: 32 * win.s * Math.min(1, win.theme.titleScale || 1)
                font.bold: true
                elide: Text.ElideRight
            }
            Row {
                spacing: 10 * win.s
                Repeater {
                    model: win.selectedGame ? win.selectedGame.tags.slice(0, 4) : []
                    delegate: Rectangle {
                        height: 30 * win.s
                        width: tagText.implicitWidth + 22 * win.s
                        radius: height / 2
                        color: win.theme.light ? "#14000000" : "#14ffffff"
                        border.color: win.theme.light ? "#26000000" : "#26ffffff"
                        Text {
                            id: tagText
                            anchors.centerIn: parent
                            text: modelData
                            color: win.fgDim
                            font.pixelSize: 15 * win.s
                        }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: win.selectedGame ? (win.isApps ? (win.selectedGame.comment || "") : win.selectedGame.file) : ""
                    color: win.fgMute
                    font.pixelSize: 16 * win.s
                }
            }
        }

        Row {
            id: hints
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 30 * win.s

            Row {
                spacing: 10 * win.s
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    width: 10 * win.s; height: width; radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: gamepad.connected ? "#6cc63a" : "#6b7080"
                }
                Text {
                    text: gamepad.connected ? gamepad.name : "Teclado"
                    color: win.fgMute
                    font.pixelSize: 17 * win.s
                    width: Math.min(implicitWidth, 220 * win.s)
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Hint { visible: win.selectedGame !== null; button: "a"; label: win.isApps ? "Abrir" : "Jogar"; style: win.padStyle; s: win.s; textColor: win.fg }
            Hint { visible: win.selectedGame !== null && !win.isApps; button: "x"; label: "Jogo"; style: win.padStyle; s: win.s; textColor: win.fg }
            Hint { visible: win.actionSystem !== ""; button: "y"; label: "Console"; style: win.padStyle; s: win.s; textColor: win.fg }
            Hint { visible: win.isConsole; button: "select"; label: "Importar"; style: win.padStyle; s: win.s; textColor: win.fg }
            Hint { button: "start"; label: "Menu"; style: win.padStyle; s: win.s; textColor: win.fg }
        }
    }

    // ------------------------------------------------------------ menu e painéis
    SideBar {
        id: sideBar
        anchors.fill: parent
        z: 75
        s: win.s
        accent: win.accent
        padStyle: win.padStyle
        consoleId: win.actionSystem
        tabId: win.currentId
        consoleName: win.actionSystem ? win.systemName(win.actionSystem) : ""
        onRequest: (what) => {
            if (what === "console") win.openConsoleOptions(win.actionSystem)
            else if (what === "import") win.openImport(win.actionSystem)
            else if (what === "shortcuts") win.openShortcuts()
            else if (what === "quit") Qt.quit()
            else if (what === "files") fileManager.open()
            else if (what === "back-to-game") { sideBar.close(); win.showMinimized() }
            else if (what === "back-to-app") {
                // um app pausado pelo modo jogo não responderia: retoma antes de voltar a ele
                if (gameMode.pausedCount) gameMode.resumeAll()
                sideBar.close(); win.showMinimized()
            }
            else if (what === "bios") win.openBiosImport(win.actionSystem)
            else if (what.startsWith("log:")) logViewer.open(what.slice(4))
            else if (what === "logs-folder") fileManager.open(logs.folder())
            else if (what.startsWith("bios:")) win.openBiosImport(what.slice(5))
        }
    }

    OptionsPanel {
        id: options
        anchors.fill: parent
        z: 62
        s: win.s
        accent: win.accent
        padStyle: win.padStyle
        property string mode: ""         // system | game | help
        property string gameSystem: ""
        property string gamePath: ""

        onChanged: (key, value) => {
            if (key.startsWith("_bios")) bios.setOption(gameSystem, key, value)
            else if (mode === "system") library.setSystemOption(gameSystem, key, value)
            else if (mode === "game") library.setGameOption(gameSystem, gamePath, key, value)
        }
        onEditText: (key, label, value) => {
            keyboard.open(label, value, (text) => {
                if (options.mode === "system") library.setSystemOption(options.gameSystem, key, text)
                else library.setGameOption(options.gameSystem, options.gamePath, key, text)
                options.reload()
                if (key === "_title") options.title = text.trim() !== "" ? text.trim() : options.title
            })
        }
        onAction: (key) => {
            if (key === "_open") { if (library.openEmulator(gameSystem)) options.close() }
            else if (key === "_import") { options.close(); win.openImport(gameSystem) }
            else if (key === "_biosImport") { options.close(); win.openBiosImport(gameSystem) }
            else if (key === "_biosFolder") { options.close(); fileManager.open(bios.folder(gameSystem)) }
            else if (key === "_fwInstall") { options.close(); win.installFirmware(gameSystem) }
            else if (key === "_refresh") { library.refresh(); options.reload() }
            else if (key === "_reset") {
                if (mode === "system") library.resetSystemOptions(gameSystem)
                else library.resetGameOptions(gameSystem, gamePath)
                options.reload()
                toast.show("Padrões restaurados.")
            }
        }
    }

    LogViewer {
        id: logViewer
        z: 76
        s: win.s
        accent: "#4c8dff"
        padStyle: win.padStyle
        onMessage: (text) => toast.show(text)
        onOpenFolder: (p) => { sideBar.close(); fileManager.open(p) }
        onPlayAgain: (sys, rom, title) => {
            sideBar.close()
            if (win.biosOk(sys, rom)) library.launch(sys, rom, title, "")
        }
    }

    FileManager {
        id: fileManager
        anchors.fill: parent
        z: 63
        s: win.s
        accent: "#4c8dff"
        padStyle: win.padStyle
        onMessage: (text) => toast.show(text)
        onRequestText: (label, value, cb) => keyboard.open(label, value, cb)
        consoleId: win.currentId
        onPicked: (kind, scope, path) => {
            wallpapers.choose(scope, kind, path)
            if (win.pickReturnSection) sideBar.open(win.pickReturnSection)
            win.pickReturnSection = ""
        }
        onPickCancelled: {
            if (win.pickReturnSection) sideBar.open(win.pickReturnSection)
            win.pickReturnSection = ""
        }
        onLaunchGame: (sys, path) => {
            const name = path.split("/").pop()
            if (win.biosOk(sys, path))
                library.launch(sys, path, name.replace(/\.[^.]*$/, "").replace(/\s*[\(\[].*$/, "") || name, "")
        }
        onImportBios: (sys, path) => {
            if (bios.busy) { toast.show("Já existe uma importação de BIOS em andamento."); return }
            importPanel.startDirect(sys, win.systemName(sys), bios.state(sys).dir, [path], "bios")
        }
        onImportGame: (sys, path) => importPanel.startDirect(sys, win.systemName(sys), win.systemDirText(sys), [path])
    }

    ImportPanel {
        id: importPanel
        anchors.fill: parent
        z: 64
        s: win.s
        accent: win.accent
        padStyle: win.padStyle
        onMessage: (text) => toast.show(text)
        onImported: if (!biosMode) library.refresh()
        onInstallFirmware: (id) => win.installFirmware(id)
    }

    Keyboard {
        id: keyboard
        anchors.fill: parent
        z: 80
        s: win.s
        accent: win.accent
        padStyle: win.padStyle
        padScanCode: launchOptions.padScanCode
    }

    // ------------------------------------------------------------ jogo em execução
    Rectangle {
        id: runningLayer
        z: 70
        anchors.fill: parent
        color: "#05060a"
        property bool closeAsked: false
        readonly property bool shown: runner.running || launchOptions.demo === "running"
        readonly property bool config: g.mode === "config" || g.mode === "firmware"
        readonly property bool firmware: g.mode === "firmware"
        readonly property var g: runner.running ? runner.game
                                 : (win.selectedGame ? { title: win.selectedGame.title, systemName: win.selectedGame.systemName,
                                                         emulator: win.currentSystem.emulator, cover: win.selectedGame.cover,
                                                         color: win.accent, mode: "game" }
                                                     : { title: "", systemName: "", emulator: "", cover: "", color: win.accent, mode: "game" })
        onShownChanged: closeAsked = false
        opacity: shown ? 0.97 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 260 } }

        MouseArea { anchors.fill: parent }   // bloqueia cliques enquanto o jogo roda

        Row {
            anchors.centerIn: parent
            spacing: 60 * win.s

            Rectangle {
                visible: !runningLayer.config
                width: 260 * win.s
                height: 370 * win.s
                radius: 14 * win.s
                clip: true
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.darker(runningLayer.g.color || win.accent, 1.6) }
                    GradientStop { position: 1.0; color: "#11131a" }
                }
                Image {
                    anchors.fill: parent
                    source: runningLayer.g.cover || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14 * win.s
                width: Math.min(900 * win.s, win.width - 520 * win.s)

                Text {
                    text: runningLayer.firmware ? "INSTALANDO FIRMWARE" : runningLayer.config ? "CONFIGURANDO" : "JOGANDO AGORA"
                    color: runningLayer.g.color || win.accent
                    font.pixelSize: 20 * win.s
                    font.bold: true
                    font.letterSpacing: 4 * win.s
                }
                Text {
                    width: parent.width
                    text: runningLayer.g.title || ""
                    color: "white"
                    font.pixelSize: 58 * win.s
                    font.bold: true
                    wrapMode: Text.WordWrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
                Text {
                    text: [runningLayer.g.systemName, runningLayer.g.emulator].filter(x => x).join("   ·   ")
                    color: "#c8ccd6"
                    font.pixelSize: 24 * win.s
                }
                Item { width: 1; height: 26 * win.s }
                Row {
                    spacing: 30 * win.s
                    Hint { button: "a"; label: runningLayer.config ? "Voltar ao emulador" : "Voltar ao jogo"; style: win.padStyle; s: win.s }
                    Hint {
                        button: "x"
                        label: runningLayer.closeAsked ? "Confirmar: fechar" : (runningLayer.config ? "Fechar o emulador" : "Fechar o jogo")
                        style: win.padStyle; s: win.s
                    }
                }
                Text {
                    text: runningLayer.firmware ? "Acompanhe a instalação na janela do emulador. Quando aparecer a mensagem de sucesso, feche o emulador (segure o Guia ou X aqui)."
                          : runningLayer.config ? "As mudanças feitas no emulador valem para os próximos jogos."
                          : "Dentro do jogo: segure Guia (ou Start+Select) para fechar · toque no Guia ou Super+Esc para voltar aqui"
                    color: "#a6abb8"
                    font.pixelSize: 19 * win.s
                    width: parent.width
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: gameMode.active && !runningLayer.config
                    text: "⚡ " + gameMode.summary
                    color: "#9fc2ff"
                    font.pixelSize: 18 * win.s
                    width: parent.width
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // ------------------------------------------------------------ confirmar resolução
    Rectangle {
        id: displayConfirm
        z: 85
        anchors.fill: parent
        color: "#c0000000"
        property bool shown: false
        property int secondsLeft: 0
        visible: shown
        function start(sec) { secondsLeft = sec; shown = true; tick.restart() }
        function finish(keep) { shown = false; tick.stop(); machine.keepDisplay(keep) }
        function handleKey(e) {
            if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) finish(true)
            else if (e.key === Qt.Key_Escape || e.key === Qt.Key_Backspace) finish(false)
        }
        Timer { id: tick; interval: 1000; repeat: true; onTriggered: { displayConfirm.secondsLeft--; if (displayConfirm.secondsLeft <= 0) { displayConfirm.shown = false; stop() } } }
        MouseArea { anchors.fill: parent }
        Rectangle {
            anchors.centerIn: parent
            width: 760 * win.s; height: 300 * win.s; radius: 26 * win.s
            color: "#161a22"; border.color: "#22ffffff"
            Column {
                anchors.centerIn: parent
                spacing: 18 * win.s
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Manter esta resolução?"; color: "white"; font.pixelSize: 36 * win.s; font.bold: true }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Volta para a anterior em " + displayConfirm.secondsLeft + " s"; color: "#c8ccd6"; font.pixelSize: 22 * win.s }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 40 * win.s
                    topPadding: 10 * win.s
                    Hint { button: "a"; label: "Manter"; style: win.padStyle; s: win.s }
                    Hint { button: "b"; label: "Voltar à anterior"; style: win.padStyle; s: win.s }
                }
            }
        }
    }

    // ------------------------------------------------------------ aviso temporário
    Rectangle {
        id: toast
        z: 90
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - footer.height - height - 50 * win.s
        width: toastText.contentWidth + 56 * win.s
        height: toastText.contentHeight + 30 * win.s
        radius: 14 * win.s
        color: "#1d212b"
        border.color: "#26ffffff"
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        function show(msg) {
            toastText.text = msg
            opacity = 1
            toastTimer.restart()
        }
        Text {
            id: toastText
            anchors.centerIn: parent
            width: win.width * 0.7
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "white"
            font.pixelSize: 20 * win.s
        }
        Timer { id: toastTimer; interval: 7000; onTriggered: toast.opacity = 0 }
    }
}
