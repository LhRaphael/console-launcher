import QtQuick

// Título grande da aba no estilo do tema do console (ver Themes.js).
// solid3d: cores em ciclo, cada letra com profundidade na própria cor escurecida
// pixel: sombra dura deslocada, sem contorno (fonte de pixel)
Item {
    id: tt
    property string text: ""
    property var theme: ({})
    property string family: ""
    property real pixelSize: 54
    property real s: 1

    readonly property string shown: theme.upper ? text.toUpperCase() : text
    readonly property string style: theme.titleStyle || "plain"
    readonly property var colors: theme.titleColors || [theme.fg || "white"]
    readonly property real px: pixelSize * (theme.titleScale || 1)
    readonly property bool perLetter: style === "multicolor" || style === "outline" || style === "block3d" || style === "split"
                                     || style === "solid3d" || style === "pixel"

    implicitWidth: perLetter ? letters.implicitWidth + 8 * s : plain.implicitWidth
    implicitHeight: perLetter ? letters.implicitHeight + 8 * s : plain.implicitHeight
    width: implicitWidth
    height: implicitHeight

    // cor da letra i: ciclo (multicolor), degradê (outline/block3d) ou metade/metade (split)
    function mix(a, b, t) {
        const ca = Qt.color(a), cb = Qt.color(b)
        return Qt.rgba(ca.r + (cb.r - ca.r) * t, ca.g + (cb.g - ca.g) * t, ca.b + (cb.b - ca.b) * t, 1)
    }
    function letterColor(i, n) {
        if (style === "multicolor" || style === "solid3d") {
            // espaços não contam, para as cores não "pularem"
            let k = 0
            for (let j = 0; j < i; ++j) if (shown[j] !== " ") k++
            return colors[k % colors.length]
        }
        if (style === "split") return i < n / 2 ? colors[0] : colors[colors.length - 1]
        const t = n > 1 ? i / (n - 1) : 0
        return mix(colors[0], colors[colors.length - 1], t)
    }

    // ---- texto simples (plain / glow)
    Item {
        id: plainBox
        visible: !tt.perLetter
        anchors.fill: parent
        Repeater {   // brilho: cópias deslocadas, translúcidas
            model: tt.style === "glow" ? [[-3, 0], [3, 0], [0, -3], [0, 3], [-2, -2], [2, 2], [-2, 2], [2, -2]] : []
            delegate: Text {
                x: modelData[0] * tt.s * 1.4; y: modelData[1] * tt.s * 1.4
                text: tt.shown; font: plain.font; color: tt.theme.glow || tt.colors[0]
                opacity: 0.22
            }
        }
        Text {
            id: plain
            text: tt.shown
            color: tt.style === "glow" ? tt.colors[0] : (tt.theme.fg || "white")
            font.family: tt.family
            font.pixelSize: tt.px
            font.weight: tt.theme.weight || Font.Bold
            font.letterSpacing: (tt.theme.letterSpacing || 0) * tt.s
        }
    }

    // ---- letra por letra
    Row {
        id: letters
        visible: tt.perLetter
        Repeater {
            model: tt.perLetter ? tt.shown.split("") : []
            delegate: Item {
                id: letter
                readonly property color c: tt.letterColor(index, tt.shown.length)
                width: main.implicitWidth + (tt.theme.letterSpacing || 0) * tt.s
                height: main.implicitHeight
                // profundidade (block3d) ou sombra (multicolor)
                Repeater {
                    readonly property bool deep: tt.style === "block3d" || tt.style === "solid3d"
                    model: deep ? 6 : (tt.style === "multicolor" || tt.style === "pixel") ? 1 : 0
                    delegate: Text {
                        readonly property bool deep: tt.style === "block3d" || tt.style === "solid3d"
                        readonly property color shade: tt.style === "solid3d" ? Qt.darker(letter.c, 2.4)
                                                     : tt.style === "block3d" ? (tt.theme.depth || "#000")
                                                                             : (tt.theme.shadow || "#000")
                        x: (deep ? (index + 1) * 1.2 : tt.style === "pixel" ? 4 : 4) * tt.s
                        y: (deep ? (index + 1) * 1.2 : tt.style === "pixel" ? 4 : 5) * tt.s
                        text: modelData === undefined ? "" : main.text
                        font: main.font
                        color: shade
                        style: tt.style === "pixel" ? Text.Normal : Text.Outline
                        styleColor: tt.style === "solid3d" ? (tt.theme.outline || shade) : shade
                    }
                }
                Text {
                    id: main
                    text: modelData
                    font.family: tt.family
                    font.pixelSize: tt.px
                    font.weight: tt.theme.weight || Font.Bold
                    color: tt.style === "outline" ? "transparent" : letter.c
                    style: tt.style === "split" || tt.style === "pixel" ? Text.Normal : Text.Outline
                    styleColor: tt.style === "outline" ? letter.c : (tt.theme.outline || tt.theme.depth || "#000")
                }
            }
        }
    }
}
