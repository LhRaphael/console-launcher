import QtQuick

// Menu lateral: abertos, biblioteca, configurações do sistema e energia.
//   Coluna da esquerda (seções): ↑↓ escolhe · A / → entra · B fecha
//   Coluna da direita (opções):  ↑↓ escolhe · ←→ ajusta · A ativa · X ação extra · B volta
Item {
    id: bar
    property bool shown: false
    property real s: 1
    property color accent: "#4c8dff"
    property string padStyle: "keyboard"
    property string consoleId: ""      // console da aba atual (para Biblioteca)
    property string tabId: ""          // aba atual, inclusive Recentes/Apps (papel de parede por aba)
    property string consoleName: ""
    property int navIndex: 1
    property bool inContent: false
    property string section: nav[navIndex] ? (nav[navIndex].id || "") : ""
    signal request(string what)        // ações tratadas pela janela principal

    readonly property var nav: [
        { id: "home", label: "Início", icon: "⌂" },
        { id: "open", label: "Abertos", icon: "❐", badge: (runner.running ? 1 : 0) + machine.openApps.length },
        { id: "files", label: "Arquivos", icon: "◫" },
        { id: "library", label: "Biblioteca", icon: "▤" },
        { header: "Configurações" },
        { id: "audio", label: "Som", icon: "♪" },
        { id: "bluetooth", label: "Bluetooth", icon: "✱" },
        { id: "network", label: "Wi‑Fi e rede", icon: "◎" },
        { id: "display", label: "Tela", icon: "▭" },
        { id: "launcher", label: "Launcher", icon: "⚙" },
        { header: "Sistema" },
        { id: "bios", label: "BIOS e firmware", icon: "◆" },
        { id: "logs", label: "Registros dos jogos", icon: "≡" },
        { id: "about", label: "Este computador", icon: "ℹ" },
        { id: "power", label: "Energia", icon: "⏻" }
    ]
    readonly property bool machineSection: ["audio", "bluetooth", "network", "display", "about"].indexOf(section) >= 0

    visible: slide.x > -slide.width + 1 || shown

    function open(sectionId) {
        shown = true
        inContent = false
        if (sectionId) {
            for (let i = 0; i < nav.length; ++i) if (nav[i].id === sectionId) navIndex = i
            enterContent()
        } else {
            reload()
        }
    }
    function close() { shown = false; inContent = false }
    function moveNav(dir) {
        let i = navIndex + dir
        while (i >= 0 && i < nav.length && nav[i].header) i += dir
        if (i >= 0 && i < nav.length) {
            navIndex = i; content.current = -1; content.pending = ""
            if (nav[i].id === "about") machine.refresh("about")
            reload(); content.positionViewAtBeginning()
        }
    }
    function enterContent() {
        if (section === "home") { close(); return }
        if (section === "files") { close(); request("files"); return }
        reload()
        if (machineSection) machine.refresh(section)
        inContent = content.first() || content.rows.length > 0
    }

    function reload() { content.setRows(buildRows()) }

    function buildRows() {
        const r = (type, key, label, detail, extra) => Object.assign({ type: type, key: key, label: label, detail: detail || "" }, extra || {})
        if (section === "display")
            return machine.rows(section).concat(wallpapers.rows(tabId))
        if (machineSection)
            return machine.rows(section)
        if (section === "home")
            return [r("info", "i", "Voltar para a biblioteca de jogos", "Pressione A")]
        if (section === "files")
            return [r("info", "i", "Gerenciador de arquivos", "Pressione A para abrir · ver pastas, copiar, mover, apagar e o espaço livre dos discos")]
        if (section === "open") {
            let rows = []
            if (runner.running) {
                const g = runner.game
                rows.push(r("header", "h_game", g.mode === "config" ? "Emulador" : "Jogo"))
                rows.push(r("item", "game", g.title, [g.systemName, g.emulator].filter(x => x).join(" · ") + " · A volta",
                            { secondary: g.mode === "config" ? "Fechar" : "Fechar o jogo", active: true }))
            }
            if (machine.openApps.length) {
                rows.push(r("header", "h_apps", "Aplicativos"))
                for (const a of machine.openApps)
                    rows.push(r("item", "app:" + a.id, a.name, "A volta para ele", { secondary: "Fechar", active: true }))
            }
            rows = rows.concat(gameMode.openRows())
            if (!rows.length)
                rows.push(r("info", "none", "Nada aberto além do launcher", "Jogos e aplicativos abertos por aqui aparecem nesta lista"))
            rows.push(r("info", "note", "Dica", "Toque no Guia (ou Super+Esc) dentro de um jogo/app para voltar"))
            return rows
        }
        if (section === "bios")
            return bios.overviewRows()
        if (section === "logs")
            return logs.rows()
        if (section === "library") {
            let rows = []
            if (consoleId !== "") {
                rows.push(r("header", "h_c", consoleName))
                rows.push(r("action", "console", "Configurar " + consoleName, "Emulador, imagem e desempenho"))
                rows.push(r("action", "import", "Importar jogos", "Traz jogos de outra pasta e limpa os restos"))
                if (bios.handles(consoleId))
                    rows.push(r("action", "bios", "Importar BIOS e firmware", bios.state(consoleId).summary))
            }
            rows.push(r("header", "h_l", "Geral"))
            rows.push(r("action", "refresh", "Atualizar biblioteca", "Procura jogos e aplicativos novos"))
            rows.push(r("info", "roms", "Pasta dos jogos", library.romRoot))
            return rows
        }
        if (section === "launcher") {
            let rows = [r("toggle", "overlay", "Overlay de desempenho (LinOSD)",
                          library.overlayAvailable ? "Métricas por cima dos jogos" : "⚠ LinOSD não encontrado (linosd-run)",
                          { value: library.overlayEnabled })]
            rows = rows.concat(wallpapers.themeRows())
            rows = rows.concat(gameMode.optionRows())
            rows.push(r("header", "h_more", "Mais"))
            rows.push(r("action", "shortcuts", "Atalhos de teclado e controle", "Lista completa"))
            if (machine.hasSystemSettings)
                rows.push(r("action", "settings", "Configurações completas do sistema", "Abre o painel do ambiente (use o mouse)"))
            rows.push(r("action", "quit", "Sair para a área de trabalho", "Fecha o launcher"))
            rows.push(r("info", "about", "Console Launcher " + library.version, "Consoles: " + library.configPath))
            return rows
        }
        if (section === "power") {
            return [
                r("action", "suspend", "Suspender", "Economiza energia; volta rápido"),
                r("action", "logout", "Encerrar sessão", "Volta para a tela de login", { confirm: true }),
                r("action", "reboot", "Reiniciar", "", { confirm: true }),
                r("action", "poweroff", "Desligar", "", { confirm: true })
            ]
        }
        return []
    }

    function onAction(key) {
        if (key.startsWith("wp:")) { wallpapers.set(tabId, key, ""); return }
        if (machineSection) { machine.act(section, key, ""); return }
        if (section === "open") {
            if (key === "gm:resume") gameMode.setOption(key, "")
            else if (key === "game") request("back-to-game")
            else if (key.startsWith("app:")) request("back-to-app")
            return
        }
        if (section === "bios") {
            if (key.startsWith("bios:")) { close(); request(key) }
            return
        }
        if (section === "logs") {
            if (key.startsWith("log:")) request(key)
            else if (key === "logs-folder") { close(); request(key) }
            else if (key === "logs-clear") { logs.clear(); content.current = -1 }
            return
        }
        if (section === "library") {
            if (key === "refresh") library.refresh()
            else { close(); request(key) }      // console | import
            return
        }
        if (section === "launcher") {
            if (key === "shortcuts" || key === "quit") { close(); request(key) }
            else if (key === "settings") { machine.openSystemSettings(); close() }
            return
        }
        if (section === "power") machine.power(key)
    }

    function handleKey(e) {
        if (!inContent) {
            switch (e.key) {
            case Qt.Key_Up: moveNav(-1); break
            case Qt.Key_Down: moveNav(1); break
            case Qt.Key_Right: case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: enterContent(); break
            case Qt.Key_Escape: case Qt.Key_Backspace: case Qt.Key_Menu: case Qt.Key_F1: case Qt.Key_Left: close(); break
            }
            return
        }
        switch (e.key) {
        case Qt.Key_Up: content.move(-1); break
        case Qt.Key_Down: content.move(1); break
        case Qt.Key_Left: if (!content.adjust(-1)) { inContent = false; content.pending = "" } break
        case Qt.Key_Right: content.adjust(1); break
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: content.activate(); break
        case Qt.Key_F2: content.activateSecondary(); break
        case Qt.Key_Escape: case Qt.Key_Backspace: inContent = false; content.pending = ""; break
        case Qt.Key_Menu: case Qt.Key_F1: close(); break
        }
    }

    Connections {
        target: machine
        function onChanged(sec) { if (bar.shown && sec === bar.section) bar.reload() }
        function onOpenAppsChanged() { if (bar.shown && bar.section === "open") bar.reload() }
    }
    Connections {
        target: runner
        function onRunningChanged() { if (bar.shown && bar.section === "open") bar.reload() }
    }
    Connections {
        target: library
        function onOverlayEnabledChanged() { if (bar.shown && bar.section === "launcher") bar.reload() }
    }
    Connections {
        target: gameMode
        function onChanged() { if (bar.shown && (bar.section === "launcher" || bar.section === "open")) bar.reload() }
    }
    Connections {
        target: wallpapers
        function onChanged() { if (bar.shown && (bar.section === "display" || bar.section === "launcher")) bar.reload() }
    }
    Connections {
        target: logs
        function onChanged() { if (bar.shown && bar.section === "logs") bar.reload() }
    }
    Connections {
        target: bios
        function onChanged() { if (bar.shown && (bar.section === "bios" || bar.section === "library")) bar.reload() }
    }
    // atualiza som/rede/Bluetooth enquanto a seção está aberta
    Timer {
        interval: 5000
        repeat: true
        running: bar.shown && (bar.inContent || bar.section === "about") && ["audio", "network", "bluetooth", "about"].indexOf(bar.section) >= 0
        onTriggered: machine.refresh(bar.section)
    }

    // ------------------------------------------------------------ visual
    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: bar.shown ? 0.55 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        MouseArea { anchors.fill: parent; enabled: bar.shown; onClicked: bar.close() }
    }

    Rectangle {
        id: slide
        height: parent.height
        width: navCol.width + contentBox.width
        x: bar.shown ? 0 : -width
        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        color: "#11141b"
        MouseArea { anchors.fill: parent }

        // ---------------- seções
        Rectangle {
            id: navCol
            width: 430 * bar.s
            height: parent.height
            color: "#151922"

            Column {
                x: 34 * bar.s
                y: 40 * bar.s
                width: parent.width - 68 * bar.s
                spacing: 4 * bar.s

                Row {
                    spacing: 16 * bar.s
                    bottomPadding: 26 * bar.s
                    Rectangle {
                        width: 58 * bar.s; height: width; radius: width / 2
                        color: bar.accent
                        Text { anchors.centerIn: parent; text: machine.userName.charAt(0).toUpperCase(); color: "white"; font.pixelSize: 28 * bar.s; font.bold: true }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Text { text: machine.userName; color: "white"; font.pixelSize: 24 * bar.s; font.bold: true }
                        Text { text: machine.hostName + "  ·  " + Qt.formatTime(clockTick.now, "HH:mm"); color: "#99ffffff"; font.pixelSize: 16 * bar.s }
                    }
                    Timer { id: clockTick; property date now: new Date(); interval: 10000; running: bar.shown; repeat: true; triggeredOnStart: true; onTriggered: now = new Date() }
                }

                Repeater {
                    model: bar.nav
                    delegate: Item {
                        readonly property bool cur: index === bar.navIndex
                        width: parent.width
                        height: modelData.header ? 46 * bar.s : 58 * bar.s
                        Text {
                            visible: !!modelData.header
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 8 * bar.s
                            x: 14 * bar.s
                            text: (modelData.header || "").toUpperCase()
                            color: "#66ffffff"
                            font.pixelSize: 14 * bar.s
                            font.bold: true
                            font.letterSpacing: 2 * bar.s
                        }
                        Rectangle {
                            visible: !modelData.header
                            anchors.fill: parent
                            radius: 14 * bar.s
                            color: cur ? (bar.inContent ? "#14ffffff" : bar.accent) : navMouse.containsMouse ? "#0dffffff" : "transparent"
                            Text {
                                id: ic
                                x: 18 * bar.s
                                width: 34 * bar.s
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignHCenter
                                text: modelData.icon || ""
                                color: "white"
                                font.pixelSize: 24 * bar.s
                            }
                            Text {
                                anchors.left: ic.right
                                anchors.leftMargin: 16 * bar.s
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label || ""
                                color: "white"
                                font.pixelSize: 23 * bar.s
                                font.bold: cur
                            }
                            Rectangle {
                                visible: (modelData.badge || 0) > 0
                                anchors.right: parent.right
                                anchors.rightMargin: 16 * bar.s
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.max(height, bt.implicitWidth + 14 * bar.s); height: 28 * bar.s; radius: height / 2
                                color: cur && !bar.inContent ? "#33ffffff" : "#6cc63a"
                                Text { id: bt; anchors.centerIn: parent; text: modelData.badge || ""; color: "white"; font.pixelSize: 15 * bar.s; font.bold: true }
                            }
                            MouseArea {
                                id: navMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { bar.navIndex = index; bar.content.current = -1; bar.enterContent() }
                            }
                        }
                    }
                }
            }

            Row {
                x: 34 * bar.s
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 30 * bar.s
                spacing: 22 * bar.s
                Hint { button: "a"; label: bar.inContent ? "Selecionar" : "Abrir"; style: bar.padStyle; s: bar.s * 0.85 }
                Hint { button: "b"; label: bar.inContent ? "Voltar" : "Fechar"; style: bar.padStyle; s: bar.s * 0.85 }
            }
        }

        // ---------------- opções da seção
        Item {
            id: contentBox
            anchors.left: navCol.right
            width: 820 * bar.s
            height: parent.height

            Text {
                id: title
                x: 40 * bar.s
                y: 44 * bar.s
                text: bar.nav[bar.navIndex] ? (bar.nav[bar.navIndex].label || "") : ""
                color: "white"
                font.pixelSize: 38 * bar.s
                font.bold: true
            }

            RowList {
                id: content
                x: 26 * bar.s
                anchors.top: title.bottom
                anchors.topMargin: 20 * bar.s
                anchors.bottom: contentHints.top
                anchors.bottomMargin: 12 * bar.s
                width: parent.width - 52 * bar.s
                s: bar.s
                accent: bar.accent
                focused: bar.inContent
                onHoverFocus: bar.inContent = true
                onChanged: (key, value) => {
                    if (key.startsWith("wp:")) wallpapers.set(bar.tabId, key, value)
                    else if (bar.machineSection) machine.act(bar.section, key, value)
                    else if (bar.section === "launcher" && key === "overlay") library.overlayEnabled = (value === "on")
                    else if (key.startsWith("gm:")) gameMode.setOption(key, value)
                    else if (key.startsWith("th:")) wallpapers.set(bar.tabId, key, value)
                }
                onAction: (key) => bar.onAction(key)
                onSecondary: (key) => {
                    if (bar.machineSection) machine.act(bar.section, key, "secondary")
                    else if (key === "game") runner.stop()
                    else if (key.startsWith("app:")) machine.closeApp(Number(key.slice(4)))
                }
            }

            Row {
                id: contentHints
                x: 40 * bar.s
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 30 * bar.s
                spacing: 22 * bar.s
                opacity: bar.inContent ? 1 : 0.35
                Hint { button: "a"; label: "Ativar"; style: bar.padStyle; s: bar.s * 0.85 }
                Row {
                    spacing: 8 * bar.s
                    anchors.verticalCenter: parent.verticalCenter
                    Text { text: "← →"; color: "#e6e8ee"; font.pixelSize: 19 * bar.s; font.bold: true }
                    Text { text: "Ajustar"; color: "#e6e8ee"; font.pixelSize: 19 * bar.s }
                }
                Hint { button: "x"; label: "Fechar / esquecer"; style: bar.padStyle; s: bar.s * 0.85 }
            }
        }
    }
    property alias content: content
}
