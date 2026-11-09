import QtQuick

// Ícone de botão que muda conforme o controle conectado
// (Xbox, PlayStation, Nintendo) ou mostra a tecla equivalente no teclado.
Rectangle {
    id: g
    property string button: "a"     // a | b | y | lb | rb | start | guide
    property string style: "xbox"   // xbox | playstation | nintendo | keyboard
    property real s: 1
    // clique faz o mesmo que apertar o botão (mouse)
    property bool clickable: true
    readonly property int keyCode: ({ a: Qt.Key_Return, b: Qt.Key_Escape, x: Qt.Key_F2, y: Qt.Key_F3,
                                      lb: Qt.Key_PageUp, rb: Qt.Key_PageDown, start: Qt.Key_Menu,
                                      select: Qt.Key_F4 })[button] || 0

    readonly property var labels: ({
        xbox:        { a: "A", b: "B", x: "X", y: "Y", lb: "LB", rb: "RB", start: "≡", select: "⧉", guide: "Guia" },
        playstation: { a: "✕", b: "○", x: "□", y: "△", lb: "L1", rb: "R1", start: "OPTIONS", select: "SHARE", guide: "PS" },
        nintendo:    { a: "A", b: "B", x: "X", y: "Y", lb: "L", rb: "R", start: "+", select: "−", guide: "⌂" },
        keyboard:    { a: "Enter", b: "Esc", x: "F2", y: "F3", lb: "Q", rb: "E", start: "F1", select: "F4", guide: "Super+Esc" }
    })
    readonly property var tints: ({
        xbox:        { a: "#6cc63a", b: "#e5524a", x: "#4c9dff", y: "#f0c43c" },
        playstation: { a: "#8fb0ff", b: "#ff7b80", x: "#e88fd8", y: "#3fd3a6" }
    })
    readonly property string label: (labels[style] || labels.xbox)[button] || button
    readonly property bool round: label.length <= 1
    readonly property bool face: ["a", "b", "x", "y"].indexOf(button) >= 0 && style !== "keyboard"

    height: 40 * s
    width: round ? height : Math.max(height, t.implicitWidth + 24 * s)
    radius: style === "keyboard" ? 8 * s : height / 2
    color: face ? "#1c2029" : "#2a2f3a"
    border.color: "#40ffffff"
    border.width: Math.max(1, 1.5 * s)

    HoverHandler { id: glyphHover; enabled: g.clickable && g.keyCode !== 0; cursorShape: Qt.PointingHandCursor }
    TapHandler { enabled: g.clickable && g.keyCode !== 0; onTapped: pointer.sendKey(g.keyCode) }
    scale: glyphHover.hovered ? 1.08 : 1
    Behavior on scale { NumberAnimation { duration: 90 } }

    Text {
        id: t
        anchors.centerIn: parent
        text: g.label
        color: (g.tints[g.style] && g.tints[g.style][g.button]) || "#f2f4f8"
        font.pixelSize: (g.label.length > 3 ? 14 : g.round ? 21 : 17) * g.s
        font.bold: true
    }
}
