import QtQuick

// Teclado na tela para digitar com o controle (também aceita o teclado físico).
//   Controle: A digita · B apaga · X espaço · Y maiúsculas · Start confirma · Select cancela
//   Teclado:  digite normalmente · Enter confirma · Esc cancela
Rectangle {
    id: kb
    property bool shown: false
    property real s: 1
    property color accent: "#4c8dff"
    property string padStyle: "keyboard"
    property int padScanCode: 0
    property string label: ""
    property string text: ""
    property bool shift: false
    property int row: 1
    property int col: 0
    property var callback: null
    property bool secret: false     // senha: mostra só pontos

    readonly property var layout: [
        "1234567890".split(""),
        "qwertyuiop".split(""),
        "asdfghjklç".split(""),
        "zxcvbnm-_.".split(""),
        ":'&!?(),+/".split(""),
        ["⇧", "espaço", "⌫", "OK"]
    ]
    readonly property var widths: [[1,1,1,1,1,1,1,1,1,1],[1,1,1,1,1,1,1,1,1,1],[1,1,1,1,1,1,1,1,1,1],
                                   [1,1,1,1,1,1,1,1,1,1],[1,1,1,1,1,1,1,1,1,1],[2,4,2,2]]

    color: "#c0000000"
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 140 } }

    function open(lbl, value, fn, isSecret) {
        label = lbl; text = value || ""; callback = fn; secret = !!isSecret; shift = false; row = 1; col = 0; shown = true
    }
    function finish(ok) {
        shown = false
        const fn = callback
        callback = null
        if (ok && fn) fn(text)
    }
    function center(r, c) {
        let x = 0
        for (let i = 0; i < c; ++i) x += widths[r][i]
        return x + widths[r][c] / 2
    }
    function moveRow(dir) {
        const nr = row + dir
        if (nr < 0 || nr >= layout.length) return
        const x = center(row, col)
        let best = 0, bestD = 1e9
        for (let c = 0; c < layout[nr].length; ++c) {
            const d = Math.abs(center(nr, c) - x)
            if (d < bestD) { bestD = d; best = c }
        }
        row = nr; col = best
    }
    function keyLabel(k) { return (shift && k.length === 1) ? k.toUpperCase() : k }
    function press() {
        const k = layout[row][col]
        if (k === "⇧") shift = !shift
        else if (k === "espaço") text += " "
        else if (k === "⌫") text = text.slice(0, -1)
        else if (k === "OK") finish(true)
        else { text += keyLabel(k); if (shift) shift = false }
    }

    function handleKey(e) {
        const pad = e.nativeScanCode === padScanCode
        switch (e.key) {
        case Qt.Key_Up: moveRow(-1); return
        case Qt.Key_Down: moveRow(1); return
        case Qt.Key_Left: col = (col + layout[row].length - 1) % layout[row].length; return
        case Qt.Key_Right: col = (col + 1) % layout[row].length; return
        case Qt.Key_Return: case Qt.Key_Enter: if (pad) press(); else finish(true); return
        case Qt.Key_Escape: if (pad) text = text.slice(0, -1); else finish(false); return
        case Qt.Key_Backspace: text = text.slice(0, -1); return
        case Qt.Key_F2: text += " "; return
        case Qt.Key_F3: shift = !shift; return
        case Qt.Key_F4: finish(false); return
        case Qt.Key_Menu: case Qt.Key_F1: finish(true); return
        }
        if (!pad && e.text && e.text.length === 1 && e.text >= " ")
            text += e.text
    }

    MouseArea { anchors.fill: parent }

    Rectangle {
        id: box
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 60 * kb.s
        width: 1240 * kb.s
        height: kbCol.implicitHeight + 64 * kb.s
        radius: 26 * kb.s
        color: "#161a22"
        border.color: "#22ffffff"

        Column {
            id: kbCol
            x: 32 * kb.s; y: 30 * kb.s
            width: parent.width - 64 * kb.s
            spacing: 14 * kb.s

            Text { text: kb.label; color: "#b3ffffff"; font.pixelSize: 20 * kb.s }
            Rectangle {
                width: parent.width; height: 64 * kb.s; radius: 12 * kb.s
                color: "#0d0f15"; border.color: kb.accent; border.width: 2 * kb.s
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 20 * kb.s
                    width: parent.width - 40 * kb.s
                    text: (kb.secret ? "•".repeat(kb.text.length) : kb.text) + "▏"
                    color: "white"
                    font.pixelSize: 28 * kb.s
                    elide: Text.ElideLeft
                }
            }
            Column {
                spacing: 8 * kb.s
                Repeater {
                    model: kb.layout.length
                    delegate: Row {
                        readonly property int r: index
                        spacing: 8 * kb.s
                        Repeater {
                            model: kb.layout[r].length
                            delegate: Rectangle {
                                readonly property bool cur: kb.row === r && kb.col === index
                                readonly property string k: kb.layout[r][index]
                                width: (kb.widths[r][index] * 110 - 8) * kb.s + (kb.widths[r][index] - 1) * 0
                                height: 62 * kb.s
                                radius: 10 * kb.s
                                color: cur ? kb.accent : (k === "⇧" && kb.shift) ? "#3a4560" : "#232834"
                                Text {
                                    anchors.centerIn: parent
                                    text: kb.keyLabel(k)
                                    color: "white"
                                    font.pixelSize: (k.length > 1 ? 20 : 26) * kb.s
                                    font.bold: cur
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: { kb.row = r; kb.col = index; kb.press() }
                                }
                            }
                        }
                    }
                }
            }
            Row {
                spacing: 26 * kb.s
                topPadding: 4 * kb.s
                Hint { button: "a"; label: "Digitar"; style: kb.padStyle === "keyboard" ? "xbox" : kb.padStyle; s: kb.s * 0.9 }
                Hint { button: "b"; label: "Apagar"; style: kb.padStyle === "keyboard" ? "xbox" : kb.padStyle; s: kb.s * 0.9 }
                Hint { button: "x"; label: "Espaço"; style: kb.padStyle === "keyboard" ? "xbox" : kb.padStyle; s: kb.s * 0.9 }
                Hint { button: "y"; label: "Maiúsculas"; style: kb.padStyle === "keyboard" ? "xbox" : kb.padStyle; s: kb.s * 0.9 }
                Hint { button: "start"; label: "OK"; style: kb.padStyle === "keyboard" ? "xbox" : kb.padStyle; s: kb.s * 0.9 }
                Hint { button: "select"; label: "Cancelar"; style: kb.padStyle === "keyboard" ? "xbox" : kb.padStyle; s: kb.s * 0.9 }
                Text { visible: kb.padStyle === "keyboard"; text: "ou digite · Enter OK · Esc cancela"; color: "#80ffffff"; font.pixelSize: 18 * kb.s; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
}
