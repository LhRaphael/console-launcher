import QtQuick

// Gerenciador de arquivos navegável por controle.
//   ↑↓ escolhe · A abre (pasta/arquivo; jogos mostram opções) · B / ← sobe
//   X opções do item · Y marca vários · Select opções da pasta · LB/RB troca de local · Start fecha
Rectangle {
    id: fm
    property bool shown: false
    property real s: 1
    property color accent: "#4c8dff"
    property string padStyle: "keyboard"
    property var places: []
    property int placeIndex: 0
    property var listing: ({ entries: [] })
    property int current: 0
    property bool focusPlaces: false
    property bool showHidden: false
    property string sort: "name"
    property var marked: ({})
    property int markedCount: 0
    property var clipboard: ({ paths: [], move: false })
    property var detail: ({})
    property string menuMode: ""         // "" | item | folder
    signal message(string text)
    signal requestText(string label, string value, var callback)
    signal launchGame(string systemId, string path)
    signal importGame(string systemId, string path)
    signal importBios(string systemId, string path)
    // Seletor: escolher uma imagem/vídeo ("file") ou uma pasta ("folder") para o papel de parede
    property string pickKind: ""
    property string pickScope: ""
    property string consoleId: ""        // aba atual (para "usar como fundo desta aba")
    signal picked(string kind, string scope, string path)
    signal pickCancelled()

    color: "#0b0d12"
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 160 } }

    function open(path) {
        places = files.places()
        const start = path || (places.length ? places[0].path : "/")
        placeIndex = Math.max(0, places.findIndex(p => p.path === start))
        focusPlaces = false
        menuMode = ""
        load(start)
        shown = true
    }
    function close() {
        const wasPicking = pickKind !== ""
        shown = false; menuMode = ""; pickKind = ""
        if (wasPicking) pickCancelled()
    }
    function pick(kind, scope, start) {
        pickKind = kind
        pickScope = scope
        open(start || "")
    }
    function finishPick(path) {
        const k = pickKind, sc = pickScope
        pickKind = ""
        shown = false; menuMode = ""
        picked(k, sc, path)
    }

    function load(path, focusPath) {
        const r = files.list(path, showHidden, sort)
        if (r.error) { message(r.error); return false }
        listing = r
        marked = ({}); markedCount = 0
        current = 0
        if (focusPath)
            for (let i = 0; i < r.entries.length; ++i) if (r.entries[i].path === focusPath) current = i
        list.positionViewAtIndex(current, ListView.Contain)
        detailTimer.restart()
        return true
    }
    function reload() { const e = entry(); load(listing.path, e ? e.path : "") ; places = files.places() }
    function entry() { return listing.entries ? listing.entries[current] : undefined }
    function targets() {
        const k = Object.keys(marked)
        if (k.length) return k
        const e = entry()
        return e ? [e.path] : []
    }
    function goUp() {
        const atPlace = places[placeIndex] && listing.path === places[placeIndex].path
        if (!listing.parent || atPlace) { focusPlaces = true; return }
        load(listing.parent, listing.path)
    }
    function toggleMark() {
        const e = entry()
        if (!e) return
        const m = Object.assign({}, marked)
        if (m[e.path]) delete m[e.path]; else m[e.path] = true
        marked = m
        markedCount = Object.keys(m).length
        if (current < listing.entries.length - 1) current++
    }
    function activate() {
        const e = entry()
        if (!e) return
        if (pickKind !== "") {
            if (e.isDir) { load(e.path); return }
            if (pickKind === "file" && wallpapers.isMedia(e.path)) { finishPick(e.path); return }
            message(pickKind === "folder" ? "Entre na pasta e use Select para escolhê-la."
                                          : "Escolha uma imagem" + (wallpapers.videoSupported ? " ou um vídeo." : "."))
            return
        }
        if (e.isDir && files.gameSystems(e.path).length === 0) { load(e.path); return }
        if (e.isDir || e.kind === "game") { openItemMenu(); return }
        if (bios.detect(e.path).length) { openItemMenu(); return }
        if (!files.open(e.path)) message("Nenhum aplicativo abre este tipo de arquivo.")
        else message("Abrindo " + e.name + "… (toque no Guia para voltar)")
    }

    // ------------------------------------------------------------ menus
    function itemRows() {
        const r = (type, key, label, detail, extra) => Object.assign({ type: type, key: key, label: label, detail: detail || "" }, extra || {})
        const t = targets(), e = entry(), n = t.length
        let rows = []
        if (n === 1 && e) {
            const games = files.gameSystems(e.path)
            if (e.isDir) rows.push(r("action", "open", "Abrir pasta"))
            for (const g of games) {
                rows.push(r("action", "play:" + g.id, "Jogar no " + g.name, "Abre direto no emulador"))
                rows.push(r("action", "import:" + g.id, "Importar para " + g.name, "Move para a biblioteca e limpa os restos"))
            }
            if (!e.isDir)
                for (const b of bios.detect(e.path))
                    rows.push(r("action", "bios:" + b.id, "Instalar no " + b.name, b.label + " · copia para a pasta do emulador"))
            if (!e.isDir) rows.push(r("action", "open", "Abrir com o aplicativo padrão"))
            if (!e.isDir && wallpapers.isMedia(e.path)) {
                rows.push(r("header", "h_wp", "Papel de parede"))
                rows.push(r("action", "wp:global", "Usar como papel de parede", "Em todas as abas que não têm um próprio"))
                if (consoleId !== "")
                    rows.push(r("action", "wp:tab", "Usar só na aba " + wallpapers.scopeName(consoleId)))
            }
        }
        rows.push(r("header", "h_e", n > 1 ? n + " itens marcados" : "Editar"))
        rows.push(r("action", "copy", "Copiar", "Depois use Colar (Select) na pasta de destino"))
        rows.push(r("action", "cut", "Recortar", "Move ao colar"))
        if (n === 1) rows.push(r("action", "rename", "Renomear"))
        if (!listing.isTrash) rows.push(r("action", "trash", "Mover para a lixeira", "Dá para recuperar pela Lixeira", { confirm: true }))
        rows.push(r("action", "delete", "Apagar permanentemente", "Não dá para desfazer", { confirm: true }))
        return rows
    }
    function folderRows() {
        const r = (type, key, label, detail, extra) => Object.assign({ type: type, key: key, label: label, detail: detail || "" }, extra || {})
        let rows = []
        if (clipboard.paths.length)
            rows.push(r("action", "paste", (clipboard.move ? "Mover " : "Colar ") + clipboard.paths.length + " item(ns) aqui", listing.display))
        rows.push(r("action", "mkdir", "Nova pasta"))
        rows.push(r("toggle", "hidden", "Mostrar arquivos ocultos", "", { value: showHidden }))
        rows.push(r("choice", "sort", "Ordenar por", "", { value: sort, options: [
            { label: "Nome", value: "name" }, { label: "Data", value: "date" }, { label: "Tamanho", value: "size" }] }))
        if (listing.isTrash) rows.push(r("action", "empty", "Esvaziar a lixeira", "Apaga tudo para sempre", { confirm: true }))
        rows.push(r("action", "refresh", "Atualizar"))
        if (wallpapers.mediaIn(listing.path).length > 1) {
            rows.push(r("header", "h_wpf", "Papel de parede"))
            rows.push(r("action", "wpf:global", "Usar esta pasta como papel de parede", "Troca sozinho de tempos em tempos"))
            if (consoleId !== "")
                rows.push(r("action", "wpf:tab", "Usar esta pasta só na aba " + wallpapers.scopeName(consoleId)))
        }
        return rows
    }
    function openItemMenu() { if (!entry()) return; menuMode = "item"; menuList.rows = itemRows(); menuList.first() }
    function openFolderMenu() { menuMode = "folder"; menuList.rows = folderRows(); menuList.first() }

    function menuAction(key) {
        const t = targets(), e = entry()
        menuMode = ""
        if (key === "open") { e && e.isDir ? load(e.path) : (files.open(e.path) ? message("Abrindo " + e.name + "…") : message("Nenhum aplicativo abre este tipo de arquivo.")) }
        else if (key.startsWith("play:")) launchGame(key.slice(5), e.path)
        else if (key.startsWith("import:")) importGame(key.slice(7), e.path)
        else if (key.startsWith("bios:")) importBios(key.slice(5), e.path)
        else if (key === "wp:global" || key === "wp:tab") wallpapers.choose(key === "wp:tab" ? consoleId : "global", "file", e.path)
        else if (key === "wpf:global" || key === "wpf:tab") wallpapers.choose(key === "wpf:tab" ? consoleId : "global", "folder", listing.path)
        else if (key === "copy" || key === "cut") {
            clipboard = { paths: t, move: key === "cut" }
            message((key === "cut" ? "Recortado: " : "Copiado: ") + t.length + " item(ns). Vá até o destino e use Select → Colar.")
            marked = ({}); markedCount = 0
        }
        else if (key === "rename") requestText("Novo nome", e.name, (txt) => { const err = files.rename(e.path, txt); if (err) message(err); else load(listing.path, listing.path + "/" + txt.trim()) })
        else if (key === "trash") { const err = files.trash(t); message(err || (t.length + " item(ns) na lixeira.")); reload() }
        else if (key === "delete") { const err = files.remove(t); message(err || (t.length + " item(ns) apagado(s).")); reload() }
        else if (key === "paste") {
            const err = files.paste(clipboard.paths, listing.path, clipboard.move)
            if (err) message(err)
            else if (clipboard.move) clipboard = { paths: [], move: false }
        }
        else if (key === "mkdir") requestText("Nome da nova pasta", "Nova pasta", (txt) => { const err = files.makeDir(listing.path, txt); if (err) message(err); else load(listing.path, listing.path + "/" + txt.trim()) })
        else if (key === "empty") { const err = files.emptyTrash(); message(err || "Lixeira esvaziada."); reload() }
        else if (key === "refresh") reload()
    }
    function menuChanged(key, value) {
        if (key === "hidden") { showHidden = value === "on"; reload() }
        else if (key === "sort") { sort = value; reload() }
        menuList.rows = folderRows()
    }

    Connections {
        target: files
        function onFinished(msg, dest) { fm.message(msg); if (fm.shown) fm.reload() }
    }
    Timer { id: detailTimer; interval: 90; onTriggered: { const e = fm.entry(); fm.detail = e ? files.details(e.path) : ({}) } }
    onCurrentChanged: detailTimer.restart()

    // ------------------------------------------------------------ teclas
    function handleKey(e) {
        if (files.busy) {
            if (e.key === Qt.Key_Escape || e.key === Qt.Key_Backspace) files.cancel()
            return
        }
        if (menuMode !== "") {
            switch (e.key) {
            case Qt.Key_Up: menuList.move(-1); break
            case Qt.Key_Down: menuList.move(1); break
            case Qt.Key_Left: menuList.adjust(-1); break
            case Qt.Key_Right: menuList.adjust(1); break
            case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: menuList.activate(); break
            case Qt.Key_Escape: case Qt.Key_Backspace: case Qt.Key_F2: case Qt.Key_F4: menuMode = ""; break
            }
            return
        }
        if (focusPlaces) {
            switch (e.key) {
            case Qt.Key_Up: placeIndex = Math.max(0, placeIndex - 1); break
            case Qt.Key_Down: placeIndex = Math.min(places.length - 1, placeIndex + 1); break
            case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: case Qt.Key_Right:
                if (load(places[placeIndex].path)) focusPlaces = false
                break
            case Qt.Key_Escape: case Qt.Key_Backspace: case Qt.Key_Menu: case Qt.Key_F1: case Qt.Key_Left: close(); break
            }
            return
        }
        const n = listing.entries.length
        switch (e.key) {
        case Qt.Key_Up: if (current > 0) current--; break
        case Qt.Key_Down: if (current < n - 1) current++; break
        case Qt.Key_Home: current = 0; break
        case Qt.Key_End: current = Math.max(0, n - 1); break
        case Qt.Key_PageUp: case Qt.Key_Q:
            placeIndex = (placeIndex + places.length - 1) % places.length; load(places[placeIndex].path); break
        case Qt.Key_PageDown: case Qt.Key_E:
            placeIndex = (placeIndex + 1) % places.length; load(places[placeIndex].path); break
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: activate(); break
        case Qt.Key_Right: { const en = entry(); if (en && en.isDir) load(en.path); break }
        case Qt.Key_Left: case Qt.Key_Escape: case Qt.Key_Backspace: goUp(); break
        case Qt.Key_F2: if (pickKind === "") openItemMenu(); break
        case Qt.Key_F3: if (pickKind === "") toggleMark(); break
        case Qt.Key_F4:
            if (pickKind === "folder") {
                if (wallpapers.mediaIn(listing.path).length) finishPick(listing.path)
                else message("Esta pasta não tem imagens" + (wallpapers.videoSupported ? " nem vídeos." : "."))
            } else if (pickKind === "") openFolderMenu()
            break
        case Qt.Key_Menu: case Qt.Key_F1: close(); break
        }
        list.positionViewAtIndex(current, ListView.Contain)
    }

    MouseArea { anchors.fill: parent }

    // ------------------------------------------------------------ locais
    Rectangle {
        id: placesCol
        width: 400 * fm.s
        height: parent.height
        color: "#11141b"

        Text {
            id: fmTitle
            x: 34 * fm.s; y: 38 * fm.s
            text: "Arquivos"
            color: "white"; font.pixelSize: 38 * fm.s; font.bold: true
        }
        ListView {
            id: placeList
            x: 20 * fm.s
            anchors.top: fmTitle.bottom; anchors.topMargin: 22 * fm.s
            anchors.bottom: parent.bottom; anchors.bottomMargin: 20 * fm.s
            width: parent.width - 40 * fm.s
            clip: true
            model: fm.places
            currentIndex: fm.placeIndex
            spacing: 4 * fm.s
            delegate: Rectangle {
                readonly property bool cur: index === fm.placeIndex
                readonly property bool here: fm.listing.path === modelData.path
                width: placeList.width
                height: (modelData.disk && modelData.storage && modelData.storage.total ? 78 : 52) * fm.s
                radius: 12 * fm.s
                color: cur && fm.focusPlaces ? fm.accent : here ? "#1fffffff" : "transparent"
                Text { id: pIcon; x: 14 * fm.s; y: 13 * fm.s; width: 30 * fm.s; horizontalAlignment: Text.AlignHCenter; text: modelData.icon; color: "white"; font.pixelSize: 20 * fm.s }
                Text { anchors.left: pIcon.right; anchors.leftMargin: 12 * fm.s; y: 12 * fm.s; text: modelData.label; color: "white"; font.pixelSize: 20 * fm.s; font.bold: here; width: parent.width - 80 * fm.s; elide: Text.ElideRight }
                Column {
                    visible: !!(modelData.disk && modelData.storage && modelData.storage.total > 0)
                    x: 58 * fm.s; y: 42 * fm.s
                    width: parent.width - 76 * fm.s
                    spacing: 5 * fm.s
                    Rectangle {
                        width: parent.width; height: 6 * fm.s; radius: height / 2; color: "#2a2f3a"
                        Rectangle {
                            width: parent.width * ((modelData.storage ? modelData.storage.usedPct : 0) / 100); height: parent.height; radius: height / 2
                            color: (modelData.storage && modelData.storage.usedPct >= 90) ? "#ff6b6b" : fm.accent
                        }
                    }
                    Text { text: modelData.storage ? modelData.storage.freeText + " livres de " + modelData.storage.totalText : ""; color: "#99ffffff"; font.pixelSize: 14 * fm.s }
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: { fm.placeIndex = index; fm.focusPlaces = false; fm.load(modelData.path) } }
            }
        }
    }

    // ------------------------------------------------------------ lista de arquivos
    Item {
        id: center
        anchors.left: placesCol.right
        anchors.right: detailCol.left
        height: parent.height

        Column {
            id: head
            x: 34 * fm.s; y: 38 * fm.s
            width: parent.width - 68 * fm.s
            spacing: 6 * fm.s
            Row {
                spacing: 14 * fm.s
                width: parent.width
                Rectangle {   // pasta acima (mouse)
                    visible: !!fm.listing.parent
                    width: 46 * fm.s; height: width; radius: width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: upMouse.containsMouse ? "#2affffff" : "#14ffffff"
                    Text { anchors.centerIn: parent; text: "↑"; color: "white"; font.pixelSize: 24 * fm.s; font.bold: true }
                    MouseArea { id: upMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: fm.goUp() }
                }
                Text {
                    text: (fm.pickKind === "file" ? "Escolha a imagem" + (wallpapers.videoSupported ? " ou o vídeo" : "") + " · "
                           : fm.pickKind === "folder" ? "Escolha a pasta · " : "") + (fm.listing.name || "")
                    color: "white"; font.pixelSize: 34 * fm.s; font.bold: true; width: parent.width - 70 * fm.s; elide: Text.ElideRight
                }
            }
            Text {
                text: (fm.listing.display || "") + "   ·   " + (fm.listing.entries ? fm.listing.entries.length : 0) + " itens"
                      + (fm.showHidden ? "   ·   ocultos visíveis" : "")
                color: "#99ffffff"; font.pixelSize: 17 * fm.s; width: parent.width; elide: Text.ElideMiddle
            }
        }

        ListView {
            id: list
            x: 20 * fm.s
            anchors.top: head.bottom; anchors.topMargin: 16 * fm.s
            anchors.bottom: bottomBar.top; anchors.bottomMargin: 10 * fm.s
            width: parent.width - 40 * fm.s
            clip: true
            model: fm.listing.entries
            currentIndex: fm.current
            boundsBehavior: Flickable.StopAtBounds
            // botão direito no espaço vazio: menu da pasta
            MouseArea {
                anchors.fill: parent
                z: -1
                acceptedButtons: Qt.RightButton
                onClicked: if (fm.pickKind === "") fm.openFolderMenu()
            }
            delegate: Rectangle {
                readonly property bool cur: index === fm.current && !fm.focusPlaces
                readonly property bool mk: fm.marked[modelData.path] === true
                width: list.width
                height: 58 * fm.s
                radius: 12 * fm.s
                color: cur ? "#1fffffff" : mk ? "#14ffffff" : fileMouse.containsMouse ? "#0cffffff" : "transparent"
                border.width: cur ? 2 * fm.s : 0
                border.color: fm.accent
                opacity: modelData.hidden ? 0.6 : 1
                Rectangle {
                    visible: mk
                    x: 6 * fm.s; anchors.verticalCenter: parent.verticalCenter
                    width: 5 * fm.s; height: parent.height * 0.6; radius: width / 2; color: fm.accent
                }
                FileIcon { id: fic; x: 18 * fm.s; anchors.verticalCenter: parent.verticalCenter; size: 34 * fm.s; kind: modelData.kind; ext: modelData.ext }
                Text {
                    anchors.left: fic.right; anchors.leftMargin: 16 * fm.s
                    anchors.right: meta.left; anchors.rightMargin: 16 * fm.s
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name + (modelData.link ? "  ↗" : "")
                    color: "white"; font.pixelSize: 20 * fm.s; font.bold: cur
                    elide: Text.ElideMiddle
                }
                Row {
                    id: meta
                    anchors.right: parent.right; anchors.rightMargin: 18 * fm.s
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 22 * fm.s
                    Text { text: modelData.sizeText || ""; color: "#b3ffffff"; font.pixelSize: 16 * fm.s; width: 110 * fm.s; horizontalAlignment: Text.AlignRight }
                    Text { text: modelData.modified; color: "#80ffffff"; font.pixelSize: 16 * fm.s; width: 150 * fm.s; horizontalAlignment: Text.AlignRight }
                }
                // mouse: clique seleciona, duplo clique abre, Ctrl/Shift+clique marca,
                // botão direito abre o menu do item
                MouseArea {
                    id: fileMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onClicked: (m) => {
                        fm.current = index; fm.focusPlaces = false
                        if (m.button === Qt.RightButton) { if (fm.pickKind === "") fm.openItemMenu() }
                        else if (m.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) fm.toggleMark()
                    }
                    onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) { fm.current = index; fm.activate() } }
                }
            }
            Text {
                visible: fm.listing.entries && fm.listing.entries.length === 0
                anchors.centerIn: parent
                text: fm.listing.isTrash ? "A lixeira está vazia" : "Pasta vazia"
                color: "#80ffffff"; font.pixelSize: 24 * fm.s
            }
        }

        // espaço do disco da pasta atual + dicas
        Column {
            id: bottomBar
            x: 34 * fm.s
            anchors.bottom: parent.bottom; anchors.bottomMargin: 24 * fm.s
            width: parent.width - 68 * fm.s
            spacing: 12 * fm.s
            Row {
                spacing: 14 * fm.s
                visible: !!(fm.listing.storage && fm.listing.storage.total > 0)
                Rectangle {
                    width: 280 * fm.s; height: 8 * fm.s; radius: height / 2; color: "#2a2f3a"
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        width: parent.width * ((fm.listing.storage ? fm.listing.storage.usedPct : 0) / 100); height: parent.height; radius: height / 2
                        color: (fm.listing.storage && fm.listing.storage.usedPct >= 90) ? "#ff6b6b" : fm.accent
                    }
                }
                Text {
                    text: fm.listing.storage ? fm.listing.storage.freeText + " livres de " + fm.listing.storage.totalText
                                               + "  (" + fm.listing.storage.root + ")" : ""
                    color: "#c8ccd6"; font.pixelSize: 17 * fm.s
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    visible: fm.clipboard.paths.length > 0 || fm.markedCount > 0
                    text: (fm.markedCount > 0 ? "·   " + fm.markedCount + " marcado(s)   " : "")
                          + (fm.clipboard.paths.length > 0 ? "·   " + fm.clipboard.paths.length + (fm.clipboard.move ? " recortado(s)" : " copiado(s)") : "")
                    color: fm.accent; font.pixelSize: 17 * fm.s; font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Row {
                visible: fm.pickKind !== ""
                spacing: 22 * fm.s
                Hint { button: "a"; label: fm.pickKind === "file" ? "Escolher / abrir pasta" : "Abrir pasta"; style: fm.padStyle; s: fm.s * 0.82 }
                Hint { visible: fm.pickKind === "folder"; button: "select"; label: "Usar esta pasta"; style: fm.padStyle; s: fm.s * 0.82 }
                Hint { button: "b"; label: "Voltar"; style: fm.padStyle; s: fm.s * 0.82 }
                Hint { button: "start"; label: "Cancelar"; style: fm.padStyle; s: fm.s * 0.82 }
            }
            Row {
                visible: fm.pickKind === ""
                spacing: 22 * fm.s
                Hint { button: "a"; label: "Abrir"; style: fm.padStyle; s: fm.s * 0.82 }
                Hint { button: "b"; label: "Voltar"; style: fm.padStyle; s: fm.s * 0.82 }
                Hint { button: "x"; label: "Opções"; style: fm.padStyle; s: fm.s * 0.82 }
                Hint { button: "y"; label: "Marcar"; style: fm.padStyle; s: fm.s * 0.82 }
                Hint { button: "select"; label: "Pasta"; style: fm.padStyle; s: fm.s * 0.82 }
                Hint { button: "start"; label: "Fechar"; style: fm.padStyle; s: fm.s * 0.82 }
            }
        }
    }

    // ------------------------------------------------------------ detalhes
    Rectangle {
        id: detailCol
        anchors.right: parent.right
        width: 440 * fm.s
        height: parent.height
        color: "#11141b"

        Column {
            x: 32 * fm.s; y: 40 * fm.s
            width: parent.width - 64 * fm.s
            spacing: 14 * fm.s
            visible: !!fm.detail.name

            Rectangle {
                width: parent.width; height: width * 0.7; radius: 16 * fm.s
                color: "#0b0d12"
                clip: true
                Image {
                    id: preview
                    anchors.fill: parent; anchors.margins: 8 * fm.s
                    visible: fm.detail.image === true && status === Image.Ready
                    source: fm.detail.image === true ? fm.detail.url : ""
                    sourceSize.width: 600; sourceSize.height: 420
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                }
                FileIcon {
                    visible: !preview.visible
                    anchors.centerIn: parent
                    size: parent.height * 0.45
                    kind: fm.entry() ? fm.entry().kind : "other"
                    ext: fm.entry() ? fm.entry().ext : ""
                }
            }
            Text { width: parent.width; text: fm.detail.name || ""; color: "white"; font.pixelSize: 24 * fm.s; font.bold: true; wrapMode: Text.WrapAnywhere; maximumLineCount: 3; elide: Text.ElideRight }
            Repeater {
                model: [
                    { k: "Tipo", v: fm.detail.type },
                    { k: "Tamanho", v: fm.detail.size },
                    { k: "Dimensões", v: fm.detail.dimensions },
                    { k: "Modificado", v: fm.detail.modified },
                    { k: "Local", v: fm.detail.location },
                    { k: "Acesso", v: fm.detail.access }
                ]
                delegate: Column {
                    visible: !!modelData.v
                    width: parent.width
                    spacing: 2 * fm.s
                    Text { text: modelData.k; color: "#80ffffff"; font.pixelSize: 15 * fm.s }
                    Text { text: modelData.v || ""; color: "#e6e8ee"; font.pixelSize: 18 * fm.s; width: parent.width; wrapMode: Text.WrapAnywhere }
                }
            }
        }
    }

    // ------------------------------------------------------------ menu de opções
    Rectangle {
        anchors.fill: parent
        color: "#99000000"
        visible: fm.menuMode !== ""
        MouseArea { anchors.fill: parent; onClicked: fm.menuMode = "" }
        Rectangle {
            anchors.centerIn: parent
            width: 720 * fm.s
            height: Math.min(parent.height - 120 * fm.s, menuList.contentHeight + 130 * fm.s)
            radius: 24 * fm.s
            color: "#161a22"; border.color: "#22ffffff"
            MouseArea { anchors.fill: parent }
            Text {
                id: menuTitle
                x: 32 * fm.s; y: 26 * fm.s
                width: parent.width - 64 * fm.s
                text: fm.menuMode === "folder" ? "Pasta: " + (fm.listing.name || "")
                      : fm.markedCount > 0 ? fm.markedCount + " itens marcados" : (fm.entry() ? fm.entry().name : "")
                color: "white"; font.pixelSize: 26 * fm.s; font.bold: true; elide: Text.ElideMiddle
            }
            RowList {
                id: menuList
                x: 18 * fm.s
                anchors.top: menuTitle.bottom; anchors.topMargin: 14 * fm.s
                anchors.bottom: parent.bottom; anchors.bottomMargin: 20 * fm.s
                width: parent.width - 36 * fm.s
                s: fm.s
                accent: fm.accent
                focused: true
                onAction: (key) => fm.menuAction(key)
                onChanged: (key, value) => fm.menuChanged(key, value)
            }
        }
    }

    // ------------------------------------------------------------ cópia em andamento
    Rectangle {
        anchors.fill: parent
        color: "#b3000000"
        visible: files.busy
        MouseArea { anchors.fill: parent }
        Column {
            anchors.centerIn: parent
            width: 700 * fm.s
            spacing: 16 * fm.s
            Text { text: files.status; color: "white"; font.pixelSize: 26 * fm.s; font.bold: true }
            Rectangle {
                width: parent.width; height: 12 * fm.s; radius: height / 2; color: "#2a2f3a"
                Rectangle { width: parent.width * Math.max(0.02, files.progress); height: parent.height; radius: height / 2; color: fm.accent }
            }
            Hint { button: "b"; label: "Cancelar"; style: fm.padStyle; s: fm.s * 0.85 }
        }
    }
}
