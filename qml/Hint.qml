import QtQuick

// Dica de botão no rodapé ("A Jogar"). Com o mouse, a dica inteira é clicável
// e faz o mesmo que apertar o botão.
Row {
    id: root
    property string button: "a"
    property string style: "xbox"
    property string label: ""
    property real s: 1
    property color textColor: "#e6e8ee"
    spacing: 12 * s

    ButtonGlyph {
        id: glyph
        button: root.button
        style: root.style
        s: root.s
        clickable: false
        anchors.verticalCenter: parent.verticalCenter
    }
    Text {
        text: root.label
        color: root.textColor
        font.pixelSize: 21 * root.s
        font.underline: hover.hovered && glyph.keyCode !== 0
        anchors.verticalCenter: parent.verticalCenter
    }
    HoverHandler { id: hover; enabled: glyph.keyCode !== 0; cursorShape: Qt.PointingHandCursor }
    TapHandler { enabled: glyph.keyCode !== 0; onTapped: pointer.sendKey(glyph.keyCode) }
}
