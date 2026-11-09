import QtQuick

// Visualizador de um registro de execução (log do emulador), feito para o controle:
// ↑↓ rolam, LB/RB passam página, A joga de novo, X apaga, Y abre a pasta, B fecha.
Rectangle {
    id: viewer
    property real s: 1
    property color accent: "#4c8dff"
    property string padStyle: "keyboard"
    property bool shown: false
    property var info: ({})
    property bool askDelete: false
    signal message(string text)
    signal playAgain(string systemId, string rom, string title)
    signal openFolder(string path)
    signal closed()

    anchors.fill: parent
    color: "#cc000000"
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 160 } }
    MouseArea { anchors.fill: parent; enabled: viewer.shown; onClicked: viewer.close() }

    readonly property string status: info.status || ""
    readonly property color statusColor: status === "ok" ? "#5fd38d" : status === "running" ? accent
                                       : status === "error" ? "#ffb04c" : "#a9b0bd"
    readonly property string statusText: status === "ok" ? "Fechou normalmente"
                                       : status === "running" ? "Em execução"
                                       : status === "stopped" ? "Fechado pelo launcher"
                                       : status === "error" ? ("Erro" + (info.exitCode ? " (código " + info.exitCode + ")" : ""))
                                       : "Sem registro de término"
    readonly property bool showHint: !!info.hint && (status === "error" || status === "unknown")

    function open(path) {
        const d = logs.read(path)
        if (d.error) { message(d.error); return }
        info = d
        askDelete = false
        shown = true
        follow = true
        Qt.callLater(stickToEnd)
    }
    function reload() {
        if (!shown || !info.path) return
        const y = flick.contentY
        const d = logs.read(info.path)
        if (d.error) return
        info = d
        if (!follow) flick.contentY = Math.min(y, Math.max(0, flick.contentHeight - flick.height))
    }
    function close() { shown = false; closed() }
    // começa no fim (onde costuma estar o erro) e acompanha o jogo aberto
    property bool follow: true
    function stickToEnd() {
        if (follow && flick.height > 0)
            flick.contentY = Math.max(0, flick.contentHeight - flick.height)
    }
    function scrollBy(dy) {
        follow = dy > 0 && flick.contentY + dy >= flick.contentHeight - flick.height
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY + dy))
    }

    function handleKey(e) {
        if (!shown) return false
        const line = 26 * s * 3, page = flick.height * 0.9
        switch (e.key) {
        case Qt.Key_Up: scrollBy(-line); break
        case Qt.Key_Down: scrollBy(line); break
        case Qt.Key_PageUp: case Qt.Key_Left: scrollBy(-page); break
        case Qt.Key_PageDown: case Qt.Key_Right: scrollBy(page); break
        case Qt.Key_Home: follow = false; flick.contentY = 0; break
        case Qt.Key_End: scrollBy(flick.contentHeight); break
        case Qt.Key_Return: case Qt.Key_Enter:
            if (info.romExists && info.mode !== "config") {
                const d = info; close(); playAgain(d.system, d.rom, d.title)
            } else message("Este registro não tem um jogo para abrir de novo.")
            break
        case Qt.Key_F2:
            if (!askDelete) { askDelete = true; break }
            if (logs.remove(info.path)) { message("Registro apagado."); close() }
            else { askDelete = false; message("Não dá para apagar o registro de um jogo aberto.") }
            break
        case Qt.Key_F3: { const p = logs.folder(); close(); openFolder(p) } break
        case Qt.Key_Escape: case Qt.Key_Backspace: case Qt.Key_Menu: case Qt.Key_F1:
            close(); break
        default: return true
        }
        if (e.key !== Qt.Key_F2) askDelete = false
        return true
    }

    // o registro do jogo aberto continua crescendo
    Timer { interval: 2000; repeat: true; running: viewer.shown && viewer.status === "running"; onTriggered: viewer.reload() }
    Connections { target: logs; function onChanged() { viewer.reload() } }

    Rectangle {
        id: box
        anchors.centerIn: parent
        width: Math.min(1500 * viewer.s, parent.width - 80 * viewer.s)
        height: parent.height - 80 * viewer.s
        radius: 26 * viewer.s
        color: "#161a22"
        border.color: "#22ffffff"
        scale: viewer.shown ? 1 : 0.97
        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent }

        Column {
            id: head
            x: 34 * viewer.s; y: 28 * viewer.s
            width: parent.width - 68 * viewer.s
            spacing: 6 * viewer.s
            Text {
                width: parent.width
                text: viewer.info.mode === "config" ? (viewer.info.emulator || "") + " (configurações)" : (viewer.info.title || "")
                color: "white"; font.pixelSize: 34 * viewer.s; font.bold: true; elide: Text.ElideRight
            }
            Row {
                spacing: 14 * viewer.s
                Rectangle {
                    height: 30 * viewer.s; width: st.implicitWidth + 24 * viewer.s; radius: height / 2
                    color: Qt.rgba(viewer.statusColor.r, viewer.statusColor.g, viewer.statusColor.b, 0.18)
                    Text { id: st; anchors.centerIn: parent; text: viewer.statusText; color: viewer.statusColor; font.pixelSize: 17 * viewer.s; font.bold: true }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: [viewer.info.systemName, viewer.info.emulator, viewer.info.whenText,
                           viewer.info.duration !== undefined ? String(viewer.info.duration).replace(".", ",") + " s" : "",
                           viewer.info.sizeText].filter(x => !!x).join("  ·  ")
                    color: "#a9b0bd"; font.pixelSize: 19 * viewer.s
                }
            }
            Rectangle {
                visible: viewer.showHint
                width: parent.width
                height: visible ? hintText.implicitHeight + 26 * viewer.s : 0
                radius: 14 * viewer.s
                color: "#2a2216"
                border.color: "#66ffb04c"
                Text {
                    id: hintText
                    anchors.verticalCenter: parent.verticalCenter
                    x: 18 * viewer.s; width: parent.width - 36 * viewer.s
                    wrapMode: Text.WordWrap
                    text: "⚠  Causa provável: " + (viewer.info.hint || "")
                    color: "#ffd49a"; font.pixelSize: 20 * viewer.s
                }
            }
            Text {
                visible: !!viewer.info.note
                width: parent.width; wrapMode: Text.WordWrap
                text: "ℹ  " + (viewer.info.note || "")
                color: "#9fc2ff"; font.pixelSize: 17 * viewer.s
            }
        }

        Rectangle {
            id: textBox
            anchors.top: head.bottom; anchors.topMargin: 16 * viewer.s
            anchors.bottom: foot.top; anchors.bottomMargin: 14 * viewer.s
            x: 34 * viewer.s; width: parent.width - 68 * viewer.s
            radius: 14 * viewer.s
            color: "#0d1016"
            border.color: "#14ffffff"
            clip: true

            Flickable {
                id: flick
                anchors.fill: parent
                anchors.margins: 16 * viewer.s
                contentWidth: width
                contentHeight: logText.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                onContentHeightChanged: viewer.stickToEnd()
                onHeightChanged: viewer.stickToEnd()
                TextEdit {
                    id: logText
                    width: flick.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    textFormat: TextEdit.PlainText
                    text: viewer.info.text || ""
                    color: "#d5dae3"
                    font.family: "monospace"
                    font.pixelSize: 17 * viewer.s
                }
            }
            // barra de rolagem
            Rectangle {
                visible: flick.contentHeight > flick.height
                anchors.right: parent.right; anchors.rightMargin: 4 * viewer.s
                width: 5 * viewer.s; radius: width / 2; color: "#55ffffff"
                y: flick.contentHeight > 0 ? flick.y + (flick.contentY / flick.contentHeight) * flick.height : 0
                height: flick.contentHeight > 0 ? Math.max(30 * viewer.s, flick.height * flick.height / flick.contentHeight) : 0
            }
        }

        Row {
            id: foot
            anchors.bottom: parent.bottom; anchors.bottomMargin: 22 * viewer.s
            x: 34 * viewer.s
            spacing: 28 * viewer.s
            Hint { button: "a"; label: "Jogar de novo"; style: viewer.padStyle; s: viewer.s * 0.85; visible: !!viewer.info.romExists && viewer.info.mode !== "config" }
            Hint { button: "x"; label: viewer.askDelete ? "Aperte de novo para apagar" : "Apagar registro"; style: viewer.padStyle; s: viewer.s * 0.85 }
            Hint { button: "y"; label: "Abrir a pasta"; style: viewer.padStyle; s: viewer.s * 0.85 }
            Hint { button: "lb"; label: "Página"; style: viewer.padStyle; s: viewer.s * 0.85 }
            Hint { button: "b"; label: "Fechar"; style: viewer.padStyle; s: viewer.s * 0.85 }
        }
    }
}
