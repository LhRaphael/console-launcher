import QtQuick

// Menu do sistema (Start / Guia / F1).
Rectangle {
    id: menu
    property bool shown: false
    property real s: 1
    property color accent: "#4c8dff"
    property string padStyle: "keyboard"
    property string consoleName: ""    // console da aba atual ("" fora de um console)
    property int current: 0
    property string pending: ""        // item aguardando confirmação (reiniciar/desligar)
    signal action(string id)

    color: "#b3000000"
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 180 } }

    readonly property var items: {
        let list = []
        if (consoleName !== "") {
            list.push({ id: "console", label: "Configurar " + consoleName, detail: "Emulador, imagem, desempenho e biblioteca" })
            list.push({ id: "import", label: "Importar jogos de " + consoleName, detail: "Traz jogos de outra pasta e limpa os restos" })
        }
        list.push({ id: "refresh", label: "Atualizar biblioteca", detail: "Procura jogos e aplicativos novos" })
        list.push({ id: "overlay", label: "Overlay de desempenho", toggle: true,
                    value: library.overlayEnabled, enabled: library.overlayAvailable,
                    detail: !library.overlayAvailable ? "LinOSD não encontrado (linosd-run)"
                            : library.overlayEnabled ? "Ligado: métricas do LinOSD nos jogos" : "Desligado" })
        list.push({ id: "shortcuts", label: "Atalhos de teclado e controle", detail: "Lista de todos os atalhos" })
        list.push({ id: "quit", label: "Fechar o launcher", detail: "Volta para a área de trabalho" })
        list.push({ id: "reboot", label: "Reiniciar", detail: "Reinicia o computador", confirm: true })
        list.push({ id: "poweroff", label: "Desligar", detail: "Desliga o computador", confirm: true })
        return list
    }

    function open() { current = 0; pending = ""; shown = true }
    function close() { shown = false; pending = "" }

    function handleKey(key) {
        switch (key) {
        case Qt.Key_Up:
            current = (current + items.length - 1) % items.length; pending = ""; break
        case Qt.Key_Down:
            current = (current + 1) % items.length; pending = ""; break
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space:
            activate(current); break
        case Qt.Key_Escape: case Qt.Key_Backspace: case Qt.Key_Menu: case Qt.Key_F1: case Qt.Key_M:
            close(); break
        }
    }

    function activate(i) {
        const it = items[i]
        if (it.enabled === false)
            return
        if (it.confirm && pending !== it.id) {
            pending = it.id
            return
        }
        pending = ""
        if (!it.toggle)
            close()
        action(it.id)
    }

    MouseArea { anchors.fill: parent; onClicked: menu.close() }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: 780 * menu.s
        height: col.implicitHeight + 64 * menu.s
        radius: 26 * menu.s
        color: "#161a22"
        border.color: "#22ffffff"
        border.width: 1
        scale: menu.shown ? 1 : 0.96
        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        MouseArea { anchors.fill: parent }

        Column {
            id: col
            x: 36 * menu.s
            y: 32 * menu.s
            width: parent.width - 72 * menu.s
            spacing: 6 * menu.s

            Text {
                text: "Menu"
                color: "white"
                font.pixelSize: 38 * menu.s
                font.bold: true
                bottomPadding: 8 * menu.s
            }

            Repeater {
                model: menu.items
                delegate: Rectangle {
                    readonly property bool cur: index === menu.current
                    readonly property bool asking: menu.pending === modelData.id
                    width: col.width
                    height: 78 * menu.s
                    radius: 14 * menu.s
                    color: cur ? "#1fffffff" : "transparent"
                    border.width: cur ? 2 * menu.s : 0
                    border.color: asking ? "#ff6b6b" : menu.accent

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 22 * menu.s
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3 * menu.s
                        Text {
                            text: asking ? "Confirmar: " + modelData.label.toLowerCase() + "?" : modelData.label
                            color: modelData.enabled === false ? "#66ffffff" : asking ? "#ff8a8a" : "white"
                            font.pixelSize: 24 * menu.s
                            font.bold: cur
                        }
                        Text {
                            text: asking ? "Pressione de novo para confirmar" : modelData.detail
                            color: "#99ffffff"
                            font.pixelSize: 16 * menu.s
                        }
                    }

                    Rectangle {
                        visible: modelData.toggle === true
                        anchors.right: parent.right
                        anchors.rightMargin: 22 * menu.s
                        anchors.verticalCenter: parent.verticalCenter
                        width: 66 * menu.s
                        height: 36 * menu.s
                        radius: height / 2
                        opacity: modelData.enabled === false ? 0.4 : 1
                        color: modelData.value ? menu.accent : "#3a3f4b"
                        Rectangle {
                            width: parent.height - 8 * menu.s
                            height: width
                            radius: width / 2
                            y: 4 * menu.s
                            x: modelData.value ? parent.width - width - 4 * menu.s : 4 * menu.s
                            color: "white"
                            Behavior on x { NumberAnimation { duration: 150 } }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: if (menu.current !== index) { menu.current = index; menu.pending = "" }
                        onClicked: { menu.current = index; menu.activate(index) }
                    }
                }
            }

            Item { width: 1; height: 6 * menu.s }
            Text {
                width: col.width
                text: "ROMs em " + library.romRoot + "   ·   Consoles: " + library.configPath
                color: "#80ffffff"
                font.pixelSize: 15 * menu.s
                elide: Text.ElideMiddle
            }
            Row {
                spacing: 30 * menu.s
                topPadding: 6 * menu.s
                Hint { button: "a"; label: "Selecionar"; style: menu.padStyle; s: menu.s }
                Hint { button: "b"; label: "Fechar"; style: menu.padStyle; s: menu.s }
                Text {
                    text: "Console Launcher " + library.version
                    color: "#55ffffff"
                    font.pixelSize: 16 * menu.s
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
