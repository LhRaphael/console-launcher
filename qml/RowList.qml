import QtQuick

// Lista de opções navegável por controle, usada no menu lateral.
// Tipos de linha: header, info, toggle (value bool), slider (value 0-100),
// choice (options/value), action (confirm), text, item (secondary, active).
ListView {
    id: list
    property real s: 1
    property color accent: "#4c8dff"
    property bool focused: false
    property int current: -1
    property string pending: ""
    property var rows: []
    signal changed(string key, string value)
    signal action(string key)
    signal secondary(string key)
    signal editText(string key, string label, string value)
    signal hoverFocus()     // o mouse entrou na lista (o menu lateral passa o foco para ela)

    model: rows
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    spacing: 2 * s

    function selectable(i) {
        const t = rows[i] ? rows[i].type : ""
        return t === "choice" || t === "action" || t === "text" || t === "toggle" || t === "slider" || t === "item"
    }
    function hasSelectable() { return rows.some((r, i) => selectable(i)) }
    function first() {
        for (let i = 0; i < rows.length; ++i) if (selectable(i)) { current = i; positionViewAtIndex(i, ListView.Contain); return true }
        current = -1
        return false
    }
    // mantém a linha atual pela chave quando as linhas são recarregadas
    function setRows(newRows) {
        const keep = (current >= 0 && current < rows.length) ? rows[current].key : ""
        rows = newRows
        if (keep !== "")
            for (let i = 0; i < rows.length; ++i)
                if (rows[i].key === keep) { current = i; return }
        if (current >= rows.length || (current >= 0 && !selectable(current))) first()
    }
    function move(dir) {
        if (!hasSelectable()) {
            contentY = Math.max(0, Math.min(contentHeight - height, contentY + dir * 120 * s))
            return
        }
        let i = current + dir
        while (i >= 0 && i < rows.length && !selectable(i)) i += dir
        if (i >= 0 && i < rows.length) { current = i; pending = ""; positionViewAtIndex(i, ListView.Contain) }
        else if (dir < 0) positionViewAtBeginning()
    }
    // ←/→: true se a linha usou a tecla (ajustou valor)
    function adjust(dir) {
        const r = rows[current]
        if (!r) return false
        if (r.type === "slider") {
            changed(r.key, String(Math.max(0, Math.min(100, (r.value || 0) + dir * 5))))
            return true
        }
        if (r.type === "choice" && r.options && r.options.length) {
            let idx = r.options.findIndex(o => o.value === r.value)
            idx = (idx + dir + r.options.length) % r.options.length
            changed(r.key, r.options[idx].value)
            return true
        }
        // chaves liga/desliga só mudam com A (evita desligar Wi‑Fi/Bluetooth sem querer com ←)
        return false
    }
    function activate() {
        const r = rows[current]
        if (!r) return
        if (r.type === "toggle") changed(r.key, r.value ? "off" : "on")
        else if (r.type === "choice") adjust(1)
        else if (r.type === "text") editText(r.key, r.label, r.value || "")
        else if (r.type === "action" || r.type === "item") {
            if (r.confirm && pending !== r.key) { pending = r.key; return }
            pending = ""
            action(r.key)
        }
    }
    function activateSecondary() {
        const r = rows[current]
        if (!r || r.type !== "item" || !r.secondary) return
        if (pending !== r.key + "#2") { pending = r.key + "#2"; return }
        pending = ""
        secondary(r.key)
    }

    delegate: Item {
        id: rowItem
        readonly property var r: modelData
        readonly property bool cur: list.focused && index === list.current
        readonly property bool asking: list.pending === r.key
        readonly property bool askingSecondary: list.pending === r.key + "#2"
        width: list.width
        height: r.type === "header" ? 50 * list.s
              : r.type === "slider" || r.type === "meter" ? 92 * list.s
              : r.detail ? 80 * list.s : 60 * list.s

        Text {
            visible: r.type === "header"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8 * list.s
            x: 14 * list.s
            text: (r.label || "").toUpperCase()
            color: list.accent
            font.pixelSize: 15 * list.s
            font.bold: true
            font.letterSpacing: 2 * list.s
        }

        Rectangle {
            visible: r.type !== "header"
            anchors.fill: parent
            anchors.topMargin: 2 * list.s
            anchors.bottomMargin: 2 * list.s
            radius: 12 * list.s
            color: rowItem.cur ? "#1fffffff" : "transparent"
            border.width: rowItem.cur ? 2 * list.s : 0
            border.color: (rowItem.asking || rowItem.askingSecondary) ? "#ff6b6b" : list.accent

            Rectangle {   // marca de "ativo" (conectado, em uso)
                visible: r.active === true
                x: 6 * list.s
                anchors.verticalCenter: parent.verticalCenter
                width: 5 * list.s; height: parent.height * 0.5; radius: width / 2
                color: "#6cc63a"
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 18 * list.s
                anchors.right: valueBox.left
                anchors.rightMargin: 16 * list.s
                y: r.type === "slider" || r.type === "meter" ? 12 * list.s : (parent.height - height) / 2
                spacing: 3 * list.s
                Text {
                    width: parent.width
                    text: rowItem.asking ? "Confirmar: " + r.label + "?"
                          : rowItem.askingSecondary ? "Confirmar: " + (r.secondary || "").toLowerCase() + " " + r.label + "?"
                          : r.label
                    color: r.type === "info" ? "#c8ffffff" : (rowItem.asking || rowItem.askingSecondary) ? "#ff8a8a" : "white"
                    font.pixelSize: (r.type === "info" ? 19 : 22) * list.s
                    font.bold: rowItem.cur
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: text !== ""
                    text: (rowItem.asking || rowItem.askingSecondary) ? "Pressione de novo para confirmar" : (r.detail || "")
                    color: (r.detail || "").startsWith("⚠") ? "#ffb070" : "#8cffffff"
                    font.pixelSize: 15 * list.s
                    elide: Text.ElideRight
                }
            }

            // barra do slider / medidor de uso (disco, memória)
            Rectangle {
                visible: r.type === "slider" || r.type === "meter"
                x: 18 * list.s
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 16 * list.s
                width: parent.width - 36 * list.s
                height: 10 * list.s
                radius: height / 2
                color: "#2a2f3a"
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, (r.value || 0) / 100))
                    height: parent.height
                    radius: height / 2
                    color: r.type === "meter" && r.value >= 90 ? "#ff6b6b" : r.type === "meter" && r.value >= 75 ? "#f0c43c" : list.accent
                }
            }

            Row {
                id: valueBox
                anchors.right: parent.right
                anchors.rightMargin: 16 * list.s
                y: r.type === "slider" ? 12 * list.s : (parent.height - height) / 2
                spacing: 8 * list.s
                readonly property string valueLabel: {
                    if (r.type === "choice") {
                        const o = (r.options || []).find(x => x.value === r.value)
                        return o ? o.label : "—"
                    }
                    if (r.type === "text") return r.value ? r.value : "—"
                    if (r.type === "slider") return (r.value || 0) + "%"
                    if (r.type === "meter") return (r.value || 0) + "% usado"
                    return ""
                }
                Text {
                    visible: (r.type === "choice" || r.type === "slider") && rowItem.cur
                    text: "‹"; color: list.accent; font.pixelSize: 26 * list.s; font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; anchors.margins: -10 * list.s; cursorShape: Qt.PointingHandCursor
                                onClicked: { list.current = index; list.adjust(-1) } }
                }
                Text {
                    visible: valueBox.valueLabel !== ""
                    text: valueBox.valueLabel
                    color: "white"
                    font.pixelSize: 19 * list.s
                    font.bold: true
                    width: Math.min(implicitWidth, 300 * list.s)
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    visible: (r.type === "choice" || r.type === "slider") && rowItem.cur
                    text: "›"; color: list.accent; font.pixelSize: 26 * list.s; font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea { anchors.fill: parent; anchors.margins: -10 * list.s; cursorShape: Qt.PointingHandCursor
                                onClicked: { list.current = index; list.adjust(1) } }
                }
                Rectangle {   // chave liga/desliga
                    visible: r.type === "toggle"
                    width: 60 * list.s; height: 32 * list.s; radius: height / 2
                    color: r.value ? list.accent : "#3a3f4b"
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        width: parent.height - 8 * list.s; height: width; radius: width / 2; y: 4 * list.s
                        x: r.value ? parent.width - width - 4 * list.s : 4 * list.s
                        color: "white"
                        Behavior on x { NumberAnimation { duration: 140 } }
                    }
                }
                Row {
                    visible: r.type === "item" && rowItem.cur && (r.secondary || "") !== ""
                    spacing: 6 * list.s
                    anchors.verticalCenter: parent.verticalCenter
                    ButtonGlyph { button: "x"; style: gamepad.style; s: list.s * 0.75; anchors.verticalCenter: parent.verticalCenter; clickable: false }
                    Text { text: r.secondary || ""; color: "#c8ffffff"; font.pixelSize: 16 * list.s; anchors.verticalCenter: parent.verticalCenter }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: { list.current = index; list.activateSecondary() } }
                }
                Text {
                    visible: r.type === "action" || r.type === "text"
                    text: r.type === "text" ? "✎" : "›"
                    color: "#80ffffff"
                    font.pixelSize: 22 * list.s
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // mouse: passar por cima seleciona, clique ativa, botão direito faz a ação secundária
            // (ou volta uma opção numa escolha); no slider, clicar/arrastar na barra ajusta o valor.
            MouseArea {
                anchors.fill: parent
                z: -1
                enabled: list.selectable(index)
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                preventStealing: r.type === "slider"
                function sliderAt(mx) {
                    const x0 = 18 * list.s, w = width - 36 * list.s
                    const v = Math.round(Math.max(0, Math.min(1, (mx - x0) / w)) * 20) * 5
                    if (v !== r.value) list.changed(r.key, String(v))
                }
                onPositionChanged: (m) => {
                    if (pointer.mouseActive && list.current !== index) {
                        list.current = index
                        list.pending = ""
                        list.hoverFocus()
                    }
                    if (r.type === "slider" && pressed && m.y > height * 0.55) sliderAt(m.x)
                }
                onPressed: (m) => {
                    if (r.type === "slider" && m.button === Qt.LeftButton && m.y > height * 0.55) {
                        list.current = index
                        sliderAt(m.x)
                    }
                }
                onClicked: (m) => {
                    list.current = index
                    list.hoverFocus()
                    if (m.button === Qt.RightButton) {
                        if (r.type === "choice" || r.type === "slider") list.adjust(-1)
                        else list.activateSecondary()
                        return
                    }
                    if (r.type === "slider") { if (m.y <= height * 0.55) list.adjust(1); return }
                    list.activate()
                }
            }
        }
    }
}
