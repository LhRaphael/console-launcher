import QtQuick

// Ícone desenhado (sem depender de tema de ícones): pasta ou "etiqueta" com a extensão.
Item {
    id: icon
    property string kind: "other"   // dir | image | video | audio | archive | game | doc | exec | other
    property string ext: ""
    property real size: 40

    readonly property var colors: ({
        dir: "#e8b53c", image: "#3fbf7f", video: "#9b6cf0", audio: "#e86c9b", archive: "#d9a23a",
        game: "#3d8bff", doc: "#8a93a6", exec: "#ef7d45", other: "#6b7385"
    })
    width: size
    height: size

    // pasta
    Item {
        visible: icon.kind === "dir"
        anchors.fill: parent
        Rectangle { x: 0; y: icon.size * 0.14; width: icon.size * 0.46; height: icon.size * 0.2; radius: icon.size * 0.06; color: Qt.darker(icon.colors.dir, 1.25) }
        Rectangle { x: 0; y: icon.size * 0.24; width: icon.size; height: icon.size * 0.64; radius: icon.size * 0.1; color: icon.colors.dir }
    }
    // arquivo
    Rectangle {
        visible: icon.kind !== "dir"
        anchors.horizontalCenter: parent.horizontalCenter
        width: icon.size * 0.8
        height: icon.size
        radius: icon.size * 0.12
        color: icon.colors[icon.kind] || icon.colors.other
        Rectangle {   // dobra do papel
            anchors.right: parent.right; anchors.top: parent.top
            width: parent.width * 0.32; height: width
            color: "#30ffffff"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: icon.size * 0.1
            text: icon.kind === "game" && icon.ext === "" ? "◉" : icon.ext
            color: "white"
            font.pixelSize: icon.size * (icon.ext.length > 3 ? 0.2 : 0.25)
            font.bold: true
        }
    }
}
