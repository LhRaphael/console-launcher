import QtQuick

// Painel genérico de opções, navegável por controle:
//   ↑↓ escolhe a linha · ←→ (ou A) troca o valor · A executa/edita · B fecha
// rows: [{type: header|info|choice|text|action, key, label, detail, options, value, confirm}]
Rectangle {
    id: panel
    property bool shown: false
    property real s: 1
    property color accent: "#4c8dff"
    property string padStyle: "keyboard"
    property string title: ""
    property string subtitle: ""
    property var loader: null       // função que devolve as linhas
    property var rows: []
    property int current: 0
    property string pending: ""     // ação aguardando confirmação
    signal changed(string key, string value)
    signal action(string key)
    signal editText(string key, string label, string value)
    signal closed()

    color: "#b3000000"
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 160 } }

    function open(t, sub, fn) {
        title = t; subtitle = sub; loader = fn
        rows = loader ? loader() : []
        current = 0
        while (current < rows.length && !selectable(current)) current++
        pending = ""
        shown = true
        list.positionViewAtBeginning()
    }
    function reload() {
        const keep = (current < rows.length) ? rows[current].key : ""
        rows = loader ? loader() : []
        for (let i = 0; i < rows.length; ++i)
            if (rows[i].key === keep) { current = i; return }
        current = Math.min(current, rows.length - 1)
    }
    function selectable(i) {
        const t = rows[i].type
        return t === "choice" || t === "action" || t === "text"
    }
    readonly property bool hasSelectable: rows.some((r, i) => selectable(i))
    function move(dir) {
        if (!hasSelectable) {   // painel só de leitura (ex.: atalhos): rola a lista
            list.contentY = Math.max(0, Math.min(list.contentHeight - list.height, list.contentY + dir * 140 * s))
            return
        }
        let i = current + dir
        while (i >= 0 && i < rows.length && !selectable(i)) i += dir
        if (i >= 0 && i < rows.length) current = i
        pending = ""
    }
    function cycle(dir) {
        const r = rows[current]
        if (!r || r.type !== "choice" || !r.options.length) return
        let idx = r.options.findIndex(o => o.value === r.value)
        idx = (idx + dir + r.options.length) % r.options.length
        changed(r.key, r.options[idx].value)
        reload()
    }
    function activate() {
        const r = rows[current]
        if (!r) return
        if (r.type === "choice") cycle(1)
        else if (r.type === "text") editText(r.key, r.label, r.value || "")
        else if (r.type === "action") {
            if (r.confirm && pending !== r.key) { pending = r.key; return }
            pending = ""
            action(r.key)
        }
    }
    function close() { shown = false; closed() }

    function handleKey(e) {
        switch (e.key) {
        case Qt.Key_Up: move(-1); break
        case Qt.Key_Down: move(1); break
        case Qt.Key_Left: cycle(-1); break
        case Qt.Key_Right: cycle(1); break
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space: activate(); break
        case Qt.Key_Escape: case Qt.Key_Backspace: case Qt.Key_Menu: case Qt.Key_F1: close(); break
        case Qt.Key_Home: current = 0; move(1); move(-1); break
        }
    }

    onCurrentChanged: if (hasSelectable) list.positionViewAtIndex(current, ListView.Contain)

    MouseArea { anchors.fill: parent; onClicked: panel.close() }

    Rectangle {
        id: box
        anchors.centerIn: parent
        width: Math.min(1180 * panel.s, parent.width - 80 * panel.s)
        height: Math.min(parent.height - 100 * panel.s, header.height + list.contentHeight + footer.height + 90 * panel.s)
        radius: 26 * panel.s
        color: "#161a22"
        border.color: "#22ffffff"
        scale: panel.shown ? 1 : 0.97
        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent }

        Column {
            id: header
            x: 40 * panel.s; y: 32 * panel.s
            width: parent.width - 80 * panel.s
            spacing: 6 * panel.s
            Text { text: panel.title; color: "white"; font.pixelSize: 36 * panel.s; font.bold: true; width: parent.width; elide: Text.ElideRight }
            Text { text: panel.subtitle; visible: text !== ""; color: "#99ffffff"; font.pixelSize: 18 * panel.s; width: parent.width; elide: Text.ElideMiddle }
        }

        ListView {
            id: list
            x: 28 * panel.s
            anchors.top: header.bottom
            anchors.topMargin: 16 * panel.s
            anchors.bottom: footer.top
            anchors.bottomMargin: 12 * panel.s
            width: parent.width - 56 * panel.s
            clip: true
            model: panel.rows
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: rowItem
                readonly property var r: modelData
                readonly property bool cur: index === panel.current
                readonly property bool asking: panel.pending === r.key
                width: list.width
                height: r.type === "header" ? 58 * panel.s : r.type === "info" ? (r.detail ? 66 : 44) * panel.s : (r.detail ? 84 : 64) * panel.s

                Text {
                    visible: r.type === "header"
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10 * panel.s
                    x: 14 * panel.s
                    text: (r.label || "").toUpperCase()
                    color: panel.accent
                    font.pixelSize: 16 * panel.s
                    font.bold: true
                    font.letterSpacing: 2 * panel.s
                }

                Rectangle {
                    visible: r.type !== "header"
                    anchors.fill: parent
                    anchors.topMargin: 3 * panel.s
                    anchors.bottomMargin: 3 * panel.s
                    radius: 14 * panel.s
                    color: rowItem.cur ? "#1fffffff" : "transparent"
                    border.width: rowItem.cur ? 2 * panel.s : 0
                    border.color: rowItem.asking ? "#ff6b6b" : panel.accent

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 18 * panel.s
                        anchors.right: valueBox.left
                        anchors.rightMargin: 20 * panel.s
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3 * panel.s
                        Text {
                            width: parent.width
                            text: rowItem.asking ? "Confirmar: " + r.label.toLowerCase() + "?" : r.label
                            color: r.type === "info" ? "#b3ffffff" : rowItem.asking ? "#ff8a8a" : "white"
                            font.pixelSize: (r.type === "info" ? 18 : 23) * panel.s
                            font.bold: rowItem.cur
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            visible: text !== ""
                            text: rowItem.asking ? "Pressione de novo para confirmar" : (r.detail || "")
                            color: (r.detail || "").startsWith("⚠") ? "#ffb070" : "#8cffffff"
                            font.pixelSize: 16 * panel.s
                            elide: Text.ElideMiddle
                        }
                    }

                    Row {
                        id: valueBox
                        anchors.right: parent.right
                        anchors.rightMargin: 18 * panel.s
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10 * panel.s
                        readonly property string valueLabel: {
                            if (r.type === "choice") {
                                const o = (r.options || []).find(x => x.value === r.value)
                                return o ? o.label : (r.value || "—")
                            }
                            if (r.type === "text") return r.value ? r.value : "—"
                            return ""
                        }
                        Text {
                            visible: r.type === "choice" && rowItem.cur
                            text: "‹"; color: panel.accent; font.pixelSize: 28 * panel.s; font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea { anchors.fill: parent; anchors.margins: -10 * panel.s; cursorShape: Qt.PointingHandCursor
                                        onClicked: { panel.current = index; panel.cycle(-1) } }
                        }
                        Text {
                            visible: valueBox.valueLabel !== ""
                            text: valueBox.valueLabel
                            color: r.type === "choice" ? "white" : "#d0ffffff"
                            font.pixelSize: 21 * panel.s
                            font.bold: r.type === "choice"
                            width: Math.min(implicitWidth, 430 * panel.s)
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            visible: r.type === "choice" && rowItem.cur
                            text: "›"; color: panel.accent; font.pixelSize: 28 * panel.s; font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea { anchors.fill: parent; anchors.margins: -10 * panel.s; cursorShape: Qt.PointingHandCursor
                                        onClicked: { panel.current = index; panel.cycle(1) } }
                        }
                        Text {
                            visible: r.type === "text" || r.type === "action"
                            text: r.type === "text" ? "✎" : "›"
                            color: "#80ffffff"
                            font.pixelSize: 24 * panel.s
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        enabled: panel.selectable(index)
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.PointingHandCursor
                        onPositionChanged: if (pointer.mouseActive && panel.current !== index) { panel.current = index; panel.pending = "" }
                        onClicked: (m) => {
                            panel.current = index
                            if (m.button === Qt.RightButton) { if (r.type === "choice") panel.cycle(-1); return }
                            panel.activate()
                        }
                    }
                }
            }
        }

        Row {
            id: footer
            x: 40 * panel.s
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 26 * panel.s
            height: 44 * panel.s
            spacing: 30 * panel.s
            Hint { visible: panel.hasSelectable; button: "a"; label: "Selecionar"; style: panel.padStyle; s: panel.s }
            Row {
                visible: panel.rows.some(r => r.type === "choice")
                spacing: 8 * panel.s
                anchors.verticalCenter: parent.verticalCenter
                Text { text: "← →"; color: "#e6e8ee"; font.pixelSize: 21 * panel.s; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Trocar valor"; color: "#e6e8ee"; font.pixelSize: 21 * panel.s; anchors.verticalCenter: parent.verticalCenter }
            }
            Hint { button: "b"; label: "Voltar"; style: panel.padStyle; s: panel.s }
        }
    }
}
