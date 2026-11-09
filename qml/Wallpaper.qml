import QtQuick

// Papel de parede do launcher (imagem ou vídeo), com transição suave em qualquer troca:
// a imagem nova é preparada por baixo dos panos e só aparece, por cima da antiga,
// quando já está pronta (dissolve de ~1,2 s). Também troca sozinho de tempos em
// tempos quando o fundo é uma pasta.
Item {
    id: wp
    property string systemId: ""          // aba atual (cada aba pode ter o seu)
    property bool playing: true           // falso durante o jogo / launcher escondido
    property int fadeMs: 1200
    property var cfg: ({ kind: "default", files: [], dim: 0, fit: "cover", interval: 0 })
    property string shownPath: ""
    property var folderIndex: ({})        // escala → posição na pasta
    readonly property bool active: shownPath !== "" && (l0.opacity > 0 || l1.opacity > 0)
    property Item front: null             // camada visível

    function refresh(force) {
        cfg = wallpapers.effective(systemId)
        apply(false, force)
    }
    function apply(advance, force) {
        const files = cfg.files || []
        let target = ""
        if (cfg.kind === "file" && files.length) target = files[0]
        else if (cfg.kind === "folder" && files.length) {
            let i = folderIndex[cfg.scope]
            if (i === undefined || i >= files.length) i = cfg.order === "random" ? Math.floor(Math.random() * files.length) : 0
            else if (advance) {
                if (cfg.order === "random" && files.length > 1) {
                    let j = i
                    while (j === i) j = Math.floor(Math.random() * files.length)
                    i = j
                } else i = (i + 1) % files.length
            }
            const m = folderIndex; m[cfg.scope] = i; folderIndex = m
            target = files[i]
        }
        show(target, force)
    }
    function show(path, force) {
        if (path === shownPath && !force) return
        shownPath = path
        if (path === "") {               // volta ao fundo padrão: some devagar
            fadeOut.target = front
            if (front) fadeOut.restart()
            front = null
            return
        }
        const next = (front === l0) ? l1 : l0
        next.z = 2
        if (front) front.z = 1
        next.load(path)
    }
    function revealed(layer) {
        if (layer.source !== shownPath) return          // chegou atrasada: outra troca já pediu
        const old = front
        front = layer
        fadeIn.target = layer
        fadeIn.restart()
        if (old && old !== layer) cleanup.layer = old
    }

    Timer {   // troca automática
        interval: Math.max(1, wp.cfg.interval || 1) * 60000
        repeat: true
        running: wp.playing && wp.cfg.kind === "folder" && (wp.cfg.files || []).length > 1 && (wp.cfg.interval || 0) > 0
        onTriggered: wp.apply(true)
    }
    Connections {
        target: wallpapers
        function onChanged() { wp.refresh(false) }
        function onNextRequested() { wp.apply(true) }
    }
    onSystemIdChanged: refresh(false)
    Component.onCompleted: refresh(false)

    NumberAnimation {
        id: fadeIn
        property: "opacity"; from: 0; to: 1; duration: wp.fadeMs; easing.type: Easing.InOutQuad
        onFinished: if (cleanup.layer) { cleanup.layer.unload(); cleanup.layer = null }
    }
    NumberAnimation { id: fadeOut; property: "opacity"; to: 0; duration: wp.fadeMs; easing.type: Easing.InOutQuad
                      onFinished: if (target && target !== wp.front) target.unload() }
    QtObject { id: cleanup; property Item layer: null }

    component Layer: Item {
        id: layer
        anchors.fill: parent
        opacity: 0
        property string source: ""
        property bool video: false
        function load(p) {
            opacity = 0
            source = ""
            video = wallpapers.isVideo(p)
            source = p
        }
        function unload() { opacity = 0; source = "" }

        Image {
            anchors.fill: parent
            visible: !layer.video
            source: !layer.video && layer.source !== "" ? wallpapers.url(layer.source) : ""
            asynchronous: true
            cache: false
            smooth: true
            sourceSize: Qt.size(wp.width, wp.height)
            fillMode: wp.cfg.fit === "contain" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
            onStatusChanged: {
                if (status === Image.Ready && layer.source !== "") wp.revealed(layer)
                else if (status === Image.Error && layer.source !== "") wp.failedOn(layer)
            }
        }
        Loader {
            anchors.fill: parent
            active: layer.video && layer.source !== "" && wallpapers.videoSupported
            source: "WallpaperVideo.qml"
            onLoaded: {
                item.cover = Qt.binding(() => wp.cfg.fit !== "contain")
                item.playing = Qt.binding(() => wp.playing)
                item.ready.connect(() => wp.revealed(layer))
                item.failed.connect((why) => wp.failedOn(layer, why))
                item.source = wallpapers.url(layer.source)
            }
            onStatusChanged: if (status === Loader.Error) wp.failedOn(layer, "suporte a vídeo indisponível")
        }
    }
    signal message(string text)
    function failedOn(layer, why) {
        if (layer.source === "") return
        message("Não foi possível mostrar " + layer.source.split("/").pop() + (why ? " (" + why + ")" : ""))
        layer.unload()
        // numa pasta, pula para o próximo; senão fica o fundo anterior
        if (cfg.kind === "folder" && (cfg.files || []).length > 1 && shownPath !== "") { shownPath = ""; apply(true) }
        else shownPath = front ? front.source : ""
    }

    Layer { id: l0 }
    Layer { id: l1 }

    // escurece para os textos continuarem legíveis
    Rectangle {
        anchors.fill: parent
        z: 3
        color: "black"
        opacity: wp.front ? (wp.cfg.dim || 0) / 100 * wp.front.opacity : 0
    }
    Rectangle {   // degradê no rodapé (as dicas de botão ficam sobre ele)
        z: 3
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: parent.height * 0.32
        opacity: wp.front ? wp.front.opacity : 0
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 1.0; color: "#cc000000" }
        }
    }
}
