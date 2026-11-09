import QtQuick

// Item da grade: capa de jogo (retrato) ou ícone de aplicativo (quadrado).
// Sem capa, mostra o título sobre um gradiente na cor do console.
Item {
    id: root
    property var game: ({})
    property bool selected: false
    property color accent: "#4c8dff"
    property real s: 1
    property real radius: 12
    property color selColor: "white"
    property color idleColor: "#2b303c"
    readonly property bool app: game.system === "apps"
    signal clicked()
    signal doubleClicked()
    signal rightClicked()
    signal hovered()

    z: selected ? 10 : 1

    Item {
        id: card
        width: root.width - 36 * root.s
        height: root.app ? width : width * 1.42
        anchors.centerIn: parent
        scale: root.selected ? 1.08 : 1.0
        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        // brilho na cor do console atrás do card selecionado
        Rectangle {
            anchors.fill: parent
            anchors.margins: -12 * root.s
            radius: (root.radius + 12) * root.s
            color: root.accent
            opacity: root.selected ? 0.32 : 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
        }
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 16 * root.s
            anchors.bottomMargin: -16 * root.s
            radius: (root.radius + 2) * root.s
            color: "black"
            opacity: 0.45
        }

        Rectangle {
            id: face
            anchors.fill: parent
            radius: root.radius * root.s
            clip: true
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.app ? "#2a3040" : Qt.darker(root.accent, 1.7) }
                GradientStop { position: 1.0; color: root.app ? "#161a24" : "#11131a" }
            }
            // ícones largos (ex.: ICON0.PNG do PS3) ficam inteiros no topo, com o título embaixo
            readonly property bool wide: root.game.wide === true

            // ----- jogo
            Text {
                visible: !root.app && (cover.status !== Image.Ready || face.wide)
                anchors.fill: parent
                anchors.margins: 20 * root.s
                anchors.topMargin: face.wide ? parent.height * 0.5 : 20 * root.s
                text: root.game.title || ""
                color: "white"
                font.pixelSize: 25 * root.s
                font.bold: true
                wrapMode: Text.WordWrap
                maximumLineCount: 6
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            Image {
                id: cover
                visible: !root.app && status === Image.Ready
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: face.wide ? 14 * root.s : 0
                height: face.wide ? parent.height * 0.42 : parent.height
                source: root.app ? "" : (root.game.cover || "")
                asynchronous: true
                fillMode: face.wide ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                sourceSize.width: 480
                sourceSize.height: 680
                smooth: true
            }

            // ----- aplicativo
            Image {
                id: icon
                visible: root.app && status === Image.Ready
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.13
                width: parent.width * 0.48
                height: width
                source: root.app ? (root.game.icon || "") : ""
                sourceSize.width: 192
                sourceSize.height: 192
                fillMode: Image.PreserveAspectFit
                asynchronous: false
                smooth: true
            }
            Rectangle {   // sem ícone: inicial do nome
                visible: root.app && icon.status !== Image.Ready
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.13
                width: parent.width * 0.48
                height: width
                radius: width * 0.22
                color: root.accent
                Text {
                    anchors.centerIn: parent
                    text: (root.game.title || "?").charAt(0).toUpperCase()
                    color: "white"
                    font.pixelSize: parent.height * 0.55
                    font.bold: true
                }
            }
            Text {
                visible: root.app
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12 * root.s
                anchors.bottomMargin: 14 * root.s
                height: parent.height * 0.26
                text: root.game.title || ""
                color: "white"
                font.pixelSize: 19 * root.s
                font.bold: root.selected
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: root.radius * root.s
            color: "transparent"
            border.width: root.selected ? 4 * root.s : 1
            border.color: root.selected ? root.selColor : root.idleColor
        }
    }

    MouseArea {
        anchors.fill: card
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        // só movimento de verdade do mouse seleciona (rolar a grade embaixo do cursor não)
        onPositionChanged: if (pointer.mouseActive) root.hovered()
        onClicked: (m) => m.button === Qt.RightButton ? root.rightClicked() : root.clicked()
        onDoubleClicked: (m) => { if (m.button === Qt.LeftButton) root.doubleClicked() }
    }
}
