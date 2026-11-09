import QtQuick

// Importação de jogos para a pasta do console, com navegação por controle.
//   Navegar: A entra na pasta / marca o arquivo · X marca · Y marca tudo · LB/RB troca de local
//   Select importa os marcados · B sobe uma pasta (no início, fecha)
Rectangle {
    id: imp
    property bool shown: false
    property real s: 1
    property color accent: "#4c8dff"
    property string padStyle: "keyboard"
    property string systemId: ""
    property string systemName: ""
    property string destText: ""
    property var listing: ({ entries: [] })
    property var places: []
    property int placeIndex: -1
    property int current: 0
    property var selected: ({})
    property int selectedCount: 0
    property string stage: "browse"     // browse | confirm | running | done
    property bool cleanup: true
    property int confirmIndex: 0
    property var report: null
    property bool cancelAsked: false
    property string mode: "games"       // games | bios
    readonly property bool biosMode: mode === "bios"
    readonly property var backend: biosMode ? bios : importer
    readonly property int confirmCount: biosMode ? 2 : 3
    signal message(string text)
    signal imported()
    signal installFirmware(string systemId)

    color: "#c0000000"
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 160 } }

    function open(sysId, name, dest, kind) {
        mode = kind || "games"
        systemId = sysId; systemName = name; destText = dest
        selected = ({}); selectedCount = 0; stage = "browse"; report = null
        places = importer.places()
        const start = backend.startDir(sysId)
        placeIndex = places.findIndex(p => p.path === start)
        load(start)
        shown = true
    }
    // importação direta (ex.: pelo gerenciador de arquivos), sem passar pela navegação
    function startDirect(sysId, name, dest, paths, kind) {
        mode = kind || "games"
        systemId = sysId; systemName = name; destText = dest; report = null
        places = importer.places()
        const m = {}
        for (const p of paths) m[p] = { name: p.split("/").pop(), path: p }
        selected = m; selectedCount = paths.length
        listing = ({ entries: [] })
        shown = true
        stage = "confirm"; confirmIndex = 0
    }
    function load(path, focusPath) {
        const r = backend.listDir(path, systemId)
        if (r.error) { message(r.error); return false }
        listing = r
        current = 0
        if (focusPath)
            for (let i = 0; i < r.entries.length; ++i)
                if (r.entries[i].path === focusPath) current = i
        list.positionViewAtIndex(current, ListView.Contain)
        return true
    }
    function entry() { return listing.entries[current] }
    function setSel(e, on) {
        const m = Object.assign({}, selected)
        if (on) m[e.path] = e; else delete m[e.path]
        selected = m
        selectedCount = Object.keys(m).length
    }
    function toggle(e) {
        if (!e) return
        if (e.kind === "dir" && e.protected) {
            message("Entre em " + e.name + " e escolha os jogos — essa pasta não pode ser importada inteira.")
            return
        }
        setSel(e, !selected[e.path])
    }
    function selectAll() {
        const all = listing.entries.filter(e => e.kind !== "dir" || e.gameRoot)
        const allOn = all.length > 0 && all.every(e => selected[e.path])
        for (const e of all) setSel(e, !allOn)
        if (all.length === 0) message(biosMode ? "Nenhum arquivo de BIOS/firmware provável nesta pasta." : "Nenhum jogo ou arquivo compactado nesta pasta.")
    }
    function toConfirm() {
        if (selectedCount === 0) {
            const e = entry()
            if (!e) { message(biosMode ? "Marque os arquivos com X (ou F2) primeiro." : "Marque os jogos com X (ou F2) primeiro."); return }
            if (e.kind === "dir" && e.protected) { toggle(e); return }
            setSel(e, true)
        }
        stage = "confirm"; confirmIndex = 0
    }
    function startImport() {
        const err = backend.start(systemId, Object.keys(selected), cleanup)
        if (err) { message(err); stage = "browse"; return }
        stage = "running"; cancelAsked = false
    }
    function goUp() {
        const atPlace = placeIndex >= 0 && places[placeIndex] && listing.path === places[placeIndex].path
        if (!listing.parent || atPlace) { close(); return }
        load(listing.parent, listing.path)
    }
    function switchPlace(dir) {
        if (!places.length) return
        placeIndex = (placeIndex + dir + places.length) % places.length
        if (placeIndex < 0) placeIndex = 0
        load(places[placeIndex].path)
    }
    function close() {
        if (stage === "running") return
        shown = false
    }

    function finish(r) {
        imp.report = r
        imp.stage = "done"
        imp.selected = ({}); imp.selectedCount = 0
        imp.imported()
    }
    readonly property bool firmwareReady: biosMode && report !== null && report.firmware !== undefined
                                          && Object.keys(report.firmware).length > 0
    Connections {
        target: importer
        function onFinished(r) { if (!imp.biosMode && imp.stage === "running") imp.finish(r) }
    }
    Connections {
        target: bios
        function onFinished(r) { if (imp.biosMode && imp.stage === "running") imp.finish(r) }
    }

    function handleKey(e) {
        if (stage === "browse") {
            const n = listing.entries.length
            switch (e.key) {
            case Qt.Key_Up: if (current > 0) current--; break
            case Qt.Key_Down: if (current < n - 1) current++; break
            case Qt.Key_PageUp: case Qt.Key_Q: switchPlace(-1); break
            case Qt.Key_PageDown: case Qt.Key_E: switchPlace(1); break
            case Qt.Key_Home: current = 0; break
            case Qt.Key_End: current = Math.max(0, n - 1); break
            case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: {
                const en = entry()
                if (!en) break
                if (en.kind === "dir" && !en.gameRoot) load(en.path)
                else toggle(en)
                break
            }
            case Qt.Key_Right: { const en = entry(); if (en && en.kind === "dir") load(en.path); break }
            case Qt.Key_Left: goUp(); break
            case Qt.Key_F2: toggle(entry()); break
            case Qt.Key_F3: selectAll(); break
            case Qt.Key_F4: toConfirm(); break
            case Qt.Key_Escape: case Qt.Key_Backspace: goUp(); break
            case Qt.Key_Menu: case Qt.Key_F1: close(); break
            }
            list.positionViewAtIndex(current, ListView.Contain)
        } else if (stage === "confirm") {
            switch (e.key) {
            case Qt.Key_Up: confirmIndex = (confirmIndex + confirmCount - 1) % confirmCount; break
            case Qt.Key_Down: confirmIndex = (confirmIndex + 1) % confirmCount; break
            case Qt.Key_Left: case Qt.Key_Right: if (confirmIndex === 1 && !biosMode) cleanup = !cleanup; break
            case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: case Qt.Key_F4:
                if (confirmIndex === 0 || e.key === Qt.Key_F4) startImport()
                else if (confirmIndex === 1 && !biosMode) cleanup = !cleanup
                else stage = "browse"
                break
            case Qt.Key_Escape: case Qt.Key_Backspace: stage = "browse"; break
            }
        } else if (stage === "running") {
            if (e.key === Qt.Key_Escape || e.key === Qt.Key_Backspace) {
                if (!cancelAsked) { cancelAsked = true; backend.cancel() }
            }
        } else if (stage === "done") {
            switch (e.key) {
            case Qt.Key_Return: case Qt.Key_Enter:
                if (firmwareReady) { const id = systemId; close(); installFirmware(id) }
                else close()
                break
            case Qt.Key_Escape: case Qt.Key_Menu: case Qt.Key_F1:
                close(); break
            case Qt.Key_F4: stage = "browse"; if (!listing.path) { places = importer.places(); load(backend.startDir(systemId)) } else load(listing.path); break
            }
        }
    }

    MouseArea { anchors.fill: parent }

    Rectangle {
        id: box
        anchors.centerIn: parent
        width: Math.min(1400 * imp.s, parent.width - 80 * imp.s)
        height: Math.min(940 * imp.s, parent.height - 80 * imp.s)
        radius: 26 * imp.s
        color: "#161a22"
        border.color: "#22ffffff"

        Column {
            id: head
            x: 40 * imp.s; y: 30 * imp.s
            width: parent.width - 80 * imp.s
            spacing: 6 * imp.s
            Text { text: (imp.biosMode ? "Importar BIOS e firmware — " : "Importar jogos — ") + imp.systemName; color: "white"; font.pixelSize: 36 * imp.s; font.bold: true }
            Text {
                textFormat: Text.StyledText
                text: "Destino: <b><font color=\"" + imp.accent + "\">" + imp.destText + "</font></b>"
                color: "#b3ffffff"; font.pixelSize: 19 * imp.s
            }
        }

        // ---------------------------------------------------- navegação
        Item {
            visible: imp.stage === "browse"
            anchors.top: head.bottom
            anchors.topMargin: 20 * imp.s
            anchors.bottom: browseHints.top
            anchors.bottomMargin: 14 * imp.s
            x: 40 * imp.s
            width: parent.width - 80 * imp.s

            Item {
                id: placesRow
                width: parent.width
                height: 44 * imp.s
                ButtonGlyph { id: lbG; button: "lb"; style: imp.padStyle; s: imp.s * 0.9; anchors.verticalCenter: parent.verticalCenter }
                ListView {
                    id: placesList
                    anchors.left: lbG.right; anchors.leftMargin: 12 * imp.s
                    anchors.right: rbG.left; anchors.rightMargin: 12 * imp.s
                    height: parent.height
                    orientation: ListView.Horizontal
                    spacing: 12 * imp.s
                    clip: true
                    interactive: false
                    model: imp.places
                    currentIndex: imp.placeIndex
                    highlightRangeMode: ListView.ApplyRange
                    preferredHighlightBegin: width * 0.2
                    preferredHighlightEnd: width * 0.8
                    highlightMoveDuration: 200
                    delegate: Rectangle {
                        readonly property bool cur: index === imp.placeIndex
                        height: 40 * imp.s
                        width: pl.implicitWidth + 30 * imp.s
                        radius: height / 2
                        color: cur ? imp.accent : "#232834"
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        Text { id: pl; anchors.centerIn: parent; text: modelData.label; color: "white"; font.pixelSize: 18 * imp.s; font.bold: cur }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { imp.placeIndex = index; imp.load(modelData.path) } }
                    }
                }
                ButtonGlyph { id: rbG; button: "rb"; style: imp.padStyle; s: imp.s * 0.9; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter }
            }

            Text {
                id: pathText
                anchors.top: placesRow.bottom
                anchors.topMargin: 16 * imp.s
                width: parent.width
                text: (imp.listing.display || "") + (imp.listing.isDest ? "   (esta já é a pasta da biblioteca)" : "")
                color: "#d8dbe3"
                font.pixelSize: 21 * imp.s
                elide: Text.ElideMiddle
            }

            ListView {
                id: list
                anchors.top: pathText.bottom
                anchors.topMargin: 12 * imp.s
                anchors.bottom: parent.bottom
                width: parent.width
                clip: true
                model: imp.listing.entries
                currentIndex: imp.current
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    readonly property bool cur: index === imp.current
                    readonly property bool sel: imp.selected[modelData.path] !== undefined
                    width: list.width
                    height: 58 * imp.s
                    radius: 12 * imp.s
                    color: cur ? "#1fffffff" : "transparent"
                    border.width: cur ? 2 * imp.s : 0
                    border.color: imp.accent

                    Rectangle {
                        id: check
                        visible: modelData.kind !== "dir" || modelData.gameRoot
                        x: 16 * imp.s
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28 * imp.s; height: width; radius: 6 * imp.s
                        color: sel ? imp.accent : "transparent"
                        border.color: sel ? imp.accent : "#80ffffff"
                        border.width: 2 * imp.s
                        Text { anchors.centerIn: parent; visible: sel; text: "✓"; color: "white"; font.pixelSize: 20 * imp.s; font.bold: true }
                    }
                    Text {
                        x: 60 * imp.s
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 360 * imp.s
                        text: (modelData.kind === "dir" ? (modelData.gameRoot ? "★  " : "▸  ") : modelData.kind === "archive" ? "▣  " : "◉  ") + modelData.name
                        color: "white"
                        font.pixelSize: 21 * imp.s
                        elide: Text.ElideMiddle
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 18 * imp.s
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 16 * imp.s
                        Rectangle {
                            readonly property string badge: modelData.kind === "archive" ? "compactado"
                                                          : modelData.badge ? modelData.badge
                                                          : modelData.kind === "game" ? "jogo"
                                                          : modelData.gameRoot ? "pasta de jogo" : ""
                            visible: badge !== ""
                            height: 28 * imp.s; width: bt.implicitWidth + 20 * imp.s; radius: height / 2
                            color: modelData.kind === "archive" ? "#3a3220" : "#1f3326"
                            anchors.verticalCenter: parent.verticalCenter
                            Text { id: bt; anchors.centerIn: parent; text: parent.badge; color: modelData.kind === "archive" ? "#f0c43c" : "#7fdc8f"; font.pixelSize: 15 * imp.s }
                        }
                        Text { text: modelData.sizeText; color: "#99ffffff"; font.pixelSize: 17 * imp.s; anchors.verticalCenter: parent.verticalCenter }
                        Text { visible: modelData.kind === "dir" && !modelData.gameRoot; text: "›"; color: "#80ffffff"; font.pixelSize: 26 * imp.s; anchors.verticalCenter: parent.verticalCenter }
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onPositionChanged: if (pointer.mouseActive && imp.current !== index) imp.current = index
                        // botão direito (ou Ctrl+clique) marca/desmarca
                        onClicked: (m) => {
                            imp.current = index
                            if (m.button === Qt.RightButton || (m.modifiers & Qt.ControlModifier)) imp.toggle(modelData)
                        }
                        onDoubleClicked: (m) => { if (m.button !== Qt.LeftButton) return; imp.current = index; if (modelData.kind === "dir" && !modelData.gameRoot) imp.load(modelData.path); else imp.toggle(modelData) }
                    }
                }

                Text {
                    visible: imp.listing.entries.length === 0
                    anchors.centerIn: parent
                    width: parent.width * 0.8
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: (imp.biosMode ? "Nenhum arquivo de BIOS/firmware provável, compactado ou pasta aqui."
                                        : "Nenhum jogo de " + imp.systemName + ", arquivo compactado ou pasta aqui.")
                          + (imp.listing.hidden > 0 ? "\n(" + imp.listing.hidden + " outros arquivos ocultos)" : "")
                    color: "#99ffffff"
                    font.pixelSize: 22 * imp.s
                }
            }
        }

        Column {
            id: browseHints
            visible: imp.stage === "browse"
            x: 40 * imp.s
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24 * imp.s
            spacing: 12 * imp.s
            Text {
                text: (imp.selectedCount > 0 ? imp.selectedCount + " selecionado(s)" : "Nada selecionado")
                      + (imp.listing.hidden > 0 && imp.listing.entries.length > 0 ? "   ·   " + imp.listing.hidden + " arquivo(s) de outros tipos ocultos" : "")
                color: imp.selectedCount > 0 ? imp.accent : "#80ffffff"
                font.pixelSize: 18 * imp.s
                font.bold: imp.selectedCount > 0
            }
            Row {
                spacing: 26 * imp.s
                Hint { button: "a"; label: "Abrir / marcar"; style: imp.padStyle; s: imp.s * 0.9 }
                Hint { button: "x"; label: "Marcar"; style: imp.padStyle; s: imp.s * 0.9 }
                Hint { button: "y"; label: "Marcar tudo"; style: imp.padStyle; s: imp.s * 0.9 }
                Hint { button: "select"; label: "Importar"; style: imp.padStyle; s: imp.s * 0.9 }
                Hint { button: "b"; label: "Voltar"; style: imp.padStyle; s: imp.s * 0.9 }
            }
        }

        // ---------------------------------------------------- confirmação
        Column {
            visible: imp.stage === "confirm"
            anchors.top: head.bottom
            anchors.topMargin: 34 * imp.s
            x: 40 * imp.s
            width: parent.width - 80 * imp.s
            spacing: 14 * imp.s

            Text {
                text: (imp.biosMode ? "Procurar BIOS/firmware em " : "Importar ") + imp.selectedCount + " item(ns)"
                      + (imp.biosMode ? "?" : " para " + imp.destText + "?")
                color: "white"; font.pixelSize: 28 * imp.s; font.bold: true
            }
            Repeater {
                model: Object.keys(imp.selected).slice(0, 6)
                delegate: Text { text: "•  " + imp.selected[modelData].name; color: "#c8ccd6"; font.pixelSize: 19 * imp.s; width: parent.width; elide: Text.ElideMiddle }
            }
            Text { visible: imp.selectedCount > 6; text: "…e mais " + (imp.selectedCount - 6); color: "#80ffffff"; font.pixelSize: 18 * imp.s }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: imp.biosMode
                      ? "Cada arquivo é identificado pelo conteúdo (não pelo nome) e copiado para a pasta que o emulador lê. Os originais ficam onde estão. Compactados são extraídos numa pasta temporária."
                      : "Arquivos compactados são extraídos. Os arquivos de que o jogo depende (.bin do .cue, discos, capas) vão junto, e jogos de vários discos ganham uma playlist .m3u."
                color: "#99ffffff"; font.pixelSize: 18 * imp.s
                topPadding: 8 * imp.s
            }
            Repeater {
                model: imp.biosMode ? [
                    { label: "Importar agora", detail: "" },
                    { label: "Voltar", detail: "" }
                ] : [
                    { label: "Importar agora", detail: "" },
                    { label: "Limpeza depois de importar", detail: imp.cleanup
                        ? "Apaga os compactados extraídos com sucesso e restos (.txt, .nfo, .url, pastas vazias)"
                        : "Mantém tudo na pasta de origem (os jogos são movidos mesmo assim)" },
                    { label: "Voltar", detail: "" }
                ]
                delegate: Rectangle {
                    readonly property bool cur: index === imp.confirmIndex
                    width: parent.width
                    height: (modelData.detail ? 84 : 62) * imp.s
                    radius: 14 * imp.s
                    color: cur ? "#1fffffff" : "transparent"
                    border.width: cur ? 2 * imp.s : 0
                    border.color: imp.accent
                    Column {
                        x: 20 * imp.s
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4 * imp.s
                        Text { text: modelData.label; color: "white"; font.pixelSize: 23 * imp.s; font.bold: cur }
                        Text { visible: modelData.detail !== ""; text: modelData.detail; color: "#8cffffff"; font.pixelSize: 16 * imp.s }
                    }
                    Text {
                        visible: index === 1 && !imp.biosMode
                        anchors.right: parent.right; anchors.rightMargin: 20 * imp.s
                        anchors.verticalCenter: parent.verticalCenter
                        text: (cur ? "‹  " : "") + (imp.cleanup ? "Sim" : "Não") + (cur ? "  ›" : "")
                        color: imp.cleanup ? "#7fdc8f" : "#ffb070"; font.pixelSize: 23 * imp.s; font.bold: true
                    }
                    MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                onPositionChanged: imp.confirmIndex = index
                                onClicked: { imp.confirmIndex = index; imp.handleKey({ key: Qt.Key_Return }) } }
                }
            }
        }

        // ---------------------------------------------------- progresso
        Column {
            visible: imp.stage === "running"
            anchors.centerIn: parent
            width: parent.width - 160 * imp.s
            spacing: 18 * imp.s
            Text { text: "Importando…"; color: imp.accent; font.pixelSize: 20 * imp.s; font.bold: true; font.letterSpacing: 3 * imp.s }
            Text { width: parent.width; text: imp.backend.item; color: "white"; font.pixelSize: 30 * imp.s; font.bold: true; elide: Text.ElideMiddle }
            Text { text: imp.cancelAsked ? "Cancelando… (o arquivo atual é desfeito)" : imp.backend.status; color: "#c8ccd6"; font.pixelSize: 21 * imp.s }
            Rectangle {
                width: parent.width; height: 14 * imp.s; radius: height / 2; color: "#232834"
                Rectangle {
                    width: parent.width * Math.max(0.02, imp.backend.progress); height: parent.height; radius: height / 2; color: imp.accent
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }
            Text { text: Math.round(imp.backend.progress * 100) + "%"; color: "#99ffffff"; font.pixelSize: 18 * imp.s }
            Hint { button: "b"; label: "Cancelar"; style: imp.padStyle; s: imp.s * 0.9; visible: !imp.cancelAsked }
        }

        // ---------------------------------------------------- resultado
        Column {
            visible: imp.stage === "done" && imp.report !== null
            anchors.top: head.bottom
            anchors.topMargin: 30 * imp.s
            x: 40 * imp.s
            width: parent.width - 80 * imp.s
            spacing: 10 * imp.s
            readonly property var r: imp.report || ({ imported: [], skipped: [], deleted: 0, freedText: "", cancelled: false })
            readonly property string noun1: imp.biosMode ? "1 arquivo instalado" : "1 jogo importado"
            readonly property string nounN: imp.biosMode ? " arquivos instalados" : " jogos importados"

            Text {
                text: (parent.r.cancelled ? "Importação cancelada · " : "")
                      + (parent.r.imported.length === 1 ? parent.noun1
                         : imp.biosMode && parent.r.imported.length === 0
                           ? ((parent.r.present || []).length ? "Já estava tudo instalado" : "Nada instalado")
                           : parent.r.imported.length + parent.nounN)
                color: parent.r.imported.length > 0 ? "#7fdc8f" : "#ffb070"
                font.pixelSize: 32 * imp.s; font.bold: true
            }
            Text {
                visible: parent.r.deleted > 0
                text: "Limpeza: " + parent.r.deleted + " arquivo(s) apagado(s), " + parent.r.freedText + " liberados"
                color: "#c8ccd6"; font.pixelSize: 20 * imp.s
            }
            Repeater {
                model: parent.r.imported.slice(0, 7)
                delegate: Text { width: parent.width; text: "✓  " + modelData.title + "   —   " + modelData.file; color: "white"; font.pixelSize: 19 * imp.s; elide: Text.ElideMiddle }
            }
            Text { visible: parent.r.imported.length > 7; text: "…e mais " + (parent.r.imported.length - 7); color: "#80ffffff"; font.pixelSize: 18 * imp.s }
            Repeater {
                model: (parent.r.present || []).slice(0, 4)
                delegate: Text { width: parent.width; text: "=  " + modelData.title + "   —   já estava em " + modelData.file; color: "#8c94a3"; font.pixelSize: 18 * imp.s; elide: Text.ElideMiddle }
            }
            Item { width: 1; height: 8 * imp.s; visible: parent.r.skipped.length > 0 }
            Repeater {
                model: parent.r.skipped.slice(0, 6)
                delegate: Text { width: parent.width; text: "⚠  " + modelData.name + ": " + modelData.reason; color: "#ffb070"; font.pixelSize: 18 * imp.s; elide: Text.ElideMiddle }
            }
            Text { visible: parent.r.skipped.length > 6; text: "…e mais " + (parent.r.skipped.length - 6) + " aviso(s)"; color: "#80ffffff"; font.pixelSize: 18 * imp.s }
            Repeater {
                model: parent.r.notes || []
                delegate: Text { width: parent.width; wrapMode: Text.WordWrap; text: "ℹ  " + modelData; color: "#9fc2ff"; font.pixelSize: 18 * imp.s; topPadding: 6 * imp.s }
            }
            Row {
                spacing: 26 * imp.s
                topPadding: 20 * imp.s
                Hint { button: "a"; label: imp.firmwareReady ? "Instalar o firmware agora" : "Fechar"; style: imp.padStyle; s: imp.s * 0.9 }
                Hint { button: "b"; label: "Fechar"; style: imp.padStyle; s: imp.s * 0.9; visible: imp.firmwareReady }
                Hint { button: "select"; label: "Importar mais"; style: imp.padStyle; s: imp.s * 0.9 }
            }
        }
    }
}
