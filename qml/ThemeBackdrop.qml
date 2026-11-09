import QtQuick

// Fundo desenhado de cada tema (formas genéricas, nada de logotipos).
// Troca com esmaecimento; as animações param durante o jogo (animate = false).
Item {
    id: bd
    property var theme: ({})
    property color accent: "#4c8dff"
    property bool animate: true
    property real s: 1
    property string kind: theme.backdrop || "orb"

    // dois Loaders: o novo aparece por cima do antigo
    property bool useA: true
    onKindChanged: swap()
    function swap() {
        const incoming = useA ? b : a
        const outgoing = useA ? a : b
        incoming.kindName = kind
        incoming.z = 2; outgoing.z = 1
        fadeIn.target = incoming; fadeIn.restart()
        fadeOut.target = outgoing; fadeOut.restart()
        useA = !useA
    }
    Component.onCompleted: { a.kindName = kind; a.opacity = 1 }
    NumberAnimation { id: fadeIn; property: "opacity"; from: 0; to: 1; duration: 700; easing.type: Easing.InOutQuad }
    NumberAnimation { id: fadeOut; property: "opacity"; to: 0; duration: 700; easing.type: Easing.InOutQuad
                      onFinished: if (target) target.kindName = "" }

    component Slot: Loader {
        property string kindName: ""
        anchors.fill: parent
        opacity: 0
        active: kindName !== ""
        sourceComponent: ({ orb: orbC, pixels: pixelsC, poly: polyC, towers: towersC, waves: wavesC,
                            bokeh: bokehC, hex: hexC, cubes: cubesC, stripes: stripesC, neon: neonC,
                            floor: floorC, lcd: lcdC })[kindName] || null
    }
    Slot { id: a }
    Slot { id: b }

    // ------------------------------------------------------------ padrão: círculo na cor do console
    Component {
        id: orbC
        Item {
            Rectangle {
                width: parent.width * 0.9; height: width; radius: width / 2
                x: parent.width * 0.42; y: -height * 0.66
                color: bd.accent; opacity: 0.10
            }
        }
    }

    // ------------------------------------------------------------ 16 bits: nuvens de pixel e faixa de blocos
    Component {
        id: pixelsC
        Item {
            id: px
            readonly property real cell: Math.round(8 * bd.s)
            Repeater {
                model: [{ x: 0.55, y: 0.10, w: 14, h: 5 }, { x: 0.78, y: 0.22, w: 10, h: 4 }, { x: 0.30, y: 0.30, w: 9, h: 3 }]
                delegate: Canvas {
                    id: cloud
                    width: modelData.w * px.cell * 2; height: modelData.h * px.cell * 2
                    y: px.height * modelData.y
                    property real base: px.width * modelData.x
                    x: base + drift
                    property real drift: 0
                    NumberAnimation on drift { from: -40 * bd.s; to: 40 * bd.s; duration: 18000 + index * 4000
                        loops: Animation.Infinite; running: bd.animate; easing.type: Easing.InOutSine }
                    opacity: 0.18
                    onPaint: {
                        const g = getContext("2d"), c = px.cell
                        g.clearRect(0, 0, width, height)
                        g.fillStyle = "#ffffff"
                        const w = modelData.w, h = modelData.h
                        for (let r = 0; r < h; ++r) {
                            const inset = Math.abs(r - (h - 1) / 2) * 1.6
                            for (let col = Math.ceil(inset); col < w * 2 - inset; ++col)
                                g.fillRect(col * c, r * c * 2, c - 1, c * 2 - 1)
                        }
                    }
                }
            }
            Canvas {   // faixa de blocos no rodapé
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: px.cell * 6
                opacity: 0.22
                onWidthChanged: requestPaint()
                onPaint: {
                    const g = getContext("2d"), c = px.cell * 2
                    g.clearRect(0, 0, width, height)
                    for (let x = 0; x < width; x += c)
                        for (let y = 0; y < height; y += c) {
                            const even = ((x / c) + (y / c)) % 2 === 0
                            g.fillStyle = even ? "#8f7cf2" : "#5b4cc4"
                            g.fillRect(x, y, c - 2, c - 2)
                        }
                }
            }
        }
    }

    // ------------------------------------------------------------ 32 bits: triângulos chapados flutuando
    Component {
        id: polyC
        Item {
            id: poly
            Repeater {
                model: 9
                delegate: Canvas {
                    readonly property var cols: ["#e2231a", "#f5b400", "#00a6a6", "#2c5fbf"]
                    width: (90 + (index * 37) % 120) * bd.s; height: width
                    x: (poly.width * ((index * 0.137 + 0.08) % 1))
                    y: (poly.height * ((index * 0.291 + 0.12) % 1))
                    opacity: 0.16
                    property real spin: index * 40
                    rotation: spin
                    NumberAnimation on spin { from: index * 40; to: index * 40 + (index % 2 ? 360 : -360)
                        duration: 60000 + index * 7000; loops: Animation.Infinite; running: bd.animate }
                    onPaint: {
                        const g = getContext("2d")
                        g.clearRect(0, 0, width, height)
                        const c = cols[index % 4]
                        g.fillStyle = c
                        g.beginPath(); g.moveTo(width * 0.5, 0); g.lineTo(width, height * 0.85); g.lineTo(0, height * 0.7); g.closePath(); g.fill()
                        g.fillStyle = Qt.darker(c, 1.5)
                        g.beginPath(); g.moveTo(width * 0.5, 0); g.lineTo(0, height * 0.7); g.lineTo(width * 0.35, height * 0.45); g.closePath(); g.fill()
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ torres azuis subindo da névoa
    Component {
        id: towersC
        Item {
            id: tw
            Rectangle {   // névoa azul no chão
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: parent.height * 0.55
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 1.0; color: "#5a1356c8" }
                }
            }
            Repeater {
                model: 16
                delegate: Rectangle {
                    readonly property real h0: tw.height * (0.12 + ((index * 0.379) % 1) * 0.42)
                    x: tw.width * (0.04 + ((index * 0.613) % 1) * 0.92)
                    width: (14 + (index % 4) * 10) * bd.s
                    height: h0 * grow
                    y: tw.height - height
                    property real grow: 1
                    SequentialAnimation on grow {
                        running: bd.animate; loops: Animation.Infinite
                        NumberAnimation { to: 0.88; duration: 4000 + index * 450; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 4000 + index * 450; easing.type: Easing.InOutSine }
                    }
                    opacity: 0.20 + (index % 3) * 0.06
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#d8ecff" }
                        GradientStop { position: 1.0; color: "#002a6e" }
                    }
                }
            }
            Repeater {   // linhas finas violeta→ciano
                model: 3
                delegate: Rectangle {
                    x: 0; width: tw.width; height: Math.max(1, 2 * bd.s)
                    y: tw.height * (0.18 + index * 0.05)
                    opacity: 0.25
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#006d5bd0" }
                        GradientStop { position: 0.5; color: "#6d5bd0" }
                        GradientStop { position: 1.0; color: "#002fb4ff" }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ ondas (fitas brancas ondulando)
    Component {
        id: wavesC
        Canvas {
            id: wv
            property real t: 0
            // ~25 quadros por segundo bastam para uma onda lenta (e poupam processador)
            Timer { interval: 40; repeat: true; running: bd.animate; onTriggered: { wv.t = (wv.t + 0.0097) % (Math.PI * 2); wv.requestPaint() } }
            renderStrategy: Canvas.Threaded
            onPaint: {
                const g = getContext("2d")
                g.clearRect(0, 0, width, height)
                const mid = height * 0.58
                for (let k = 0; k < 9; ++k) {
                    g.beginPath()
                    g.strokeStyle = Qt.rgba(1, 1, 1, 0.05 + 0.025 * (k % 3))
                    g.lineWidth = (k % 3 === 0 ? 2.2 : 1.2) * bd.s
                    for (let x = 0; x <= width; x += 16) {
                        const u = x / width
                        const y = mid + Math.sin(u * 3.1 + t + k * 0.22) * height * 0.08
                                      + Math.sin(u * 7.3 - t * 2 + k * 0.5) * height * 0.025 * (1 + k * 0.15)
                        if (x === 0) g.moveTo(x, y); else g.lineTo(x, y)
                    }
                    g.stroke()
                }
            }
        }
    }

    // ------------------------------------------------------------ luzes suaves subindo
    Component {
        id: bokehC
        Item {
            id: bk
            Rectangle {   // faixa de luz diagonal
                width: parent.width * 1.4; height: parent.height * 0.35
                x: -parent.width * 0.2; y: parent.height * 0.25
                rotation: -14
                opacity: 0.10
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00ffffff" }
                    GradientStop { position: 0.5; color: "#8fc3ff" }
                    GradientStop { position: 1.0; color: "#00ffffff" }
                }
            }
            Repeater {
                model: 14
                delegate: Rectangle {
                    readonly property real size: (18 + (index * 13) % 46) * bd.s
                    width: size; height: size; radius: size / 2
                    x: bk.width * ((index * 0.271 + 0.05) % 1)
                    property real rise: (index * 0.37) % 1
                    y: bk.height * (1.05 - (rise % 1) * 1.2)
                    color: "#9fd0ff"
                    opacity: 0.06 + (index % 4) * 0.025
                    NumberAnimation on rise { from: (index * 0.37) % 1; to: (index * 0.37) % 1 + 1
                        duration: 30000 + index * 2600; loops: Animation.Infinite; running: bd.animate }
                }
            }
        }
    }

    // ------------------------------------------------------------ verde: grade de hexágonos e brilho pulsando
    Component {
        id: hexC
        Item {
            Rectangle {   // brilho central pulsando
                id: glow
                width: parent.width * 0.75; height: width; radius: width / 2
                anchors.centerIn: parent; anchors.verticalCenterOffset: parent.height * 0.18
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 0.5; color: "#2a1fa61f" }
                    GradientStop { position: 1.0; color: "#00000000" }
                }
                property real pulse: 0.7
                opacity: pulse
                SequentialAnimation on pulse {
                    running: bd.animate; loops: Animation.Infinite
                    NumberAnimation { to: 1.0; duration: 3200; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.55; duration: 3200; easing.type: Easing.InOutSine }
                }
            }
            Canvas {
                anchors.fill: parent
                opacity: 0.16
                onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                onPaint: {
                    const g = getContext("2d"), r = 34 * bd.s, h = r * Math.sqrt(3)
                    g.clearRect(0, 0, width, height)
                    g.strokeStyle = "#5ed41f"; g.lineWidth = 1
                    for (let row = -1; row * h * 0.5 < height + h; ++row)
                        for (let col = -1; col * r * 3 < width + r * 3; ++col) {
                            const cx = col * r * 3 + (row % 2 ? r * 1.5 : 0), cy = row * h * 0.5
                            g.beginPath()
                            for (let i = 0; i <= 6; ++i) {
                                const a = Math.PI / 3 * i
                                const x = cx + r * Math.cos(a), y = cy + r * Math.sin(a)
                                if (i === 0) g.moveTo(x, y); else g.lineTo(x, y)
                            }
                            g.stroke()
                        }
                }
            }
        }
    }

    // ------------------------------------------------------------ cubos isométricos
    Component {
        id: cubesC
        Item {
            id: cb
            Canvas {   // padrão de cubos bem suave
                anchors.fill: parent
                opacity: 0.10
                onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                onPaint: {
                    const g = getContext("2d"), r = 46 * bd.s, w = r * Math.sqrt(3)
                    g.clearRect(0, 0, width, height)
                    for (let row = -1; row * r * 1.5 < height + r; ++row)
                        for (let col = -1; col * w < width + w; ++col) {
                            const cx = col * w + (row % 2 ? w / 2 : 0), cy = row * r * 1.5
                            const top = [cx, cy - r], rt = [cx + w / 2, cy - r / 2], rb = [cx + w / 2, cy + r / 2]
                            const bot = [cx, cy + r], lb = [cx - w / 2, cy + r / 2], lt = [cx - w / 2, cy - r / 2]
                            const face = (pts, c) => { g.fillStyle = c; g.beginPath(); g.moveTo(pts[0][0], pts[0][1]); for (const p of pts.slice(1)) g.lineTo(p[0], p[1]); g.closePath(); g.fill() }
                            face([top, rt, [cx, cy], lt], "#b9a6ff")
                            face([lt, [cx, cy], bot, lb], "#7d5cff")
                            face([[cx, cy], rt, rb, bot], "#4a2fb8")
                        }
                }
            }
            Repeater {   // alguns cubos maiores girando devagar
                model: 4
                delegate: Canvas {
                    width: (120 + index * 40) * bd.s; height: width
                    x: cb.width * [0.62, 0.82, 0.48, 0.9][index]; y: cb.height * [0.12, 0.45, 0.62, 0.05][index]
                    opacity: 0.22
                    property real spin: 0
                    rotation: spin
                    NumberAnimation on spin { from: 0; to: 360; duration: 90000 + index * 15000; loops: Animation.Infinite; running: bd.animate }
                    onPaint: {
                        const g = getContext("2d"), r = width * 0.42, w = r * Math.sqrt(3), cx = width / 2, cy = height / 2
                        g.clearRect(0, 0, width, height)
                        g.strokeStyle = "#c9b8ff"; g.lineWidth = 2 * bd.s
                        g.beginPath()
                        g.moveTo(cx, cy - r); g.lineTo(cx + w / 2, cy - r / 2); g.lineTo(cx + w / 2, cy + r / 2); g.lineTo(cx, cy + r)
                        g.lineTo(cx - w / 2, cy + r / 2); g.lineTo(cx - w / 2, cy - r / 2); g.closePath()
                        g.moveTo(cx, cy); g.lineTo(cx, cy + r); g.moveTo(cx, cy); g.lineTo(cx + w / 2, cy - r / 2)
                        g.moveTo(cx, cy); g.lineTo(cx - w / 2, cy - r / 2)
                        g.stroke()
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ claro: listras finas e brilho
    Component {
        id: stripesC
        Item {
            Canvas {
                anchors.fill: parent
                opacity: 0.55
                onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                onPaint: {
                    const g = getContext("2d"), step = 6 * bd.s
                    g.clearRect(0, 0, width, height)
                    g.fillStyle = "#e3e8ee"
                    for (let y = 0; y < height; y += step) g.fillRect(0, y, width, Math.max(1, step / 3))
                }
            }
            Rectangle {   // faixa azul-clara no rodapé
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: parent.height * 0.2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00ffffff" }
                    GradientStop { position: 1.0; color: "#5534bdeb" }
                }
            }
        }
    }

    // ------------------------------------------------------------ neon: vermelho à esquerda, azul à direita
    Component {
        id: neonC
        Item {
            Rectangle {
                width: parent.height * 0.5; height: parent.height
                anchors.left: parent.left
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#55ff3c28" }
                    GradientStop { position: 1.0; color: "#00ff3c28" }
                }
            }
            Rectangle {
                width: parent.height * 0.5; height: parent.height
                anchors.right: parent.right
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#000ab9e6" }
                    GradientStop { position: 1.0; color: "#550ab9e6" }
                }
            }
            Rectangle {   // trilhos finos nas bordas
                width: 6 * bd.s; height: parent.height; color: "#ff3c28"; opacity: 0.85
            }
            Rectangle {
                width: 6 * bd.s; height: parent.height; anchors.right: parent.right; color: "#0ab9e6"; opacity: 0.85
            }
        }
    }

    // ------------------------------------------------------------ 3D colorido: piso quadriculado em perspectiva
    Component {
        id: floorC
        Item {
            id: fl
            property real phase: 0
            Timer {   // 20 quadros/s bastam para o piso andar devagar
                interval: 50; repeat: true; running: bd.animate && fl.visible
                onTriggered: { fl.phase = (fl.phase + 0.02) % 2; floorCanvas.requestPaint() }
            }
            Canvas {
                id: floorCanvas
                anchors.fill: parent
                opacity: 0.30
                onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                onPaint: {
                    const g = getContext("2d")
                    g.clearRect(0, 0, width, height)
                    const hz = height * 0.58, depth = height - hz, cx = width * 0.5
                    const cols = ["#e8392c", "#2a7de1", "#3cb44a", "#f4c20d"]
                    const rows = 14, half = 9, cellW = width * 0.16
                    const ph = fl.phase % 1, flip = fl.phase >= 1 ? 1 : 0
                    // z: 1 (pé da tela) até rows (horizonte); a fase traz as linhas para perto
                    for (let k = 0; k < rows; ++k) {
                        const z0 = 1 + k - ph, z1 = z0 + 1
                        if (z1 <= 0.35) continue
                        const a = Math.max(z0, 0.35)
                        const y0 = hz + depth / a, y1 = hz + depth / z1
                        for (let j = -half; j < half; ++j) {
                            const xa0 = cx + j * cellW / a, xb0 = cx + (j + 1) * cellW / a
                            const xa1 = cx + j * cellW / z1, xb1 = cx + (j + 1) * cellW / z1
                            const even = ((k + j + flip) % 2 + 2) % 2 === 0
                            g.globalAlpha = Math.min(1, 1.6 / z1)
                            g.fillStyle = even ? cols[(k + flip) % 4] : "#0b0e22"
                            g.beginPath()
                            g.moveTo(xa0, y0); g.lineTo(xb0, y0); g.lineTo(xb1, y1); g.lineTo(xa1, y1)
                            g.closePath(); g.fill()
                        }
                    }
                    g.globalAlpha = 1
                }
            }
            Rectangle {   // neblina no horizonte
                anchors.left: parent.left; anchors.right: parent.right
                y: parent.height * 0.50; height: parent.height * 0.18
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#0012173a" }
                    GradientStop { position: 0.5; color: "#cc12173a" }
                    GradientStop { position: 1.0; color: "#0012173a" }
                }
            }
            Repeater {   // poliedros chapados flutuando
                model: [{ x: 0.70, y: 0.14, w: 70, c: 1 }, { x: 0.86, y: 0.34, w: 46, c: 3 }, { x: 0.56, y: 0.30, w: 34, c: 0 }]
                delegate: Canvas {
                    readonly property var cols: ["#e8392c", "#2a7de1", "#3cb44a", "#f4c20d"]
                    width: modelData.w * bd.s; height: width * 1.3
                    x: fl.width * modelData.x
                    property real bob: 0
                    y: fl.height * modelData.y + bob
                    opacity: 0.5
                    NumberAnimation on bob { from: -10 * bd.s; to: 10 * bd.s; duration: 3200 + index * 700
                        loops: Animation.Infinite; running: bd.animate; easing.type: Easing.InOutSine }
                    property real spin: index * 30
                    rotation: spin
                    NumberAnimation on spin { from: index * 30; to: index * 30 + 360; duration: 24000 + index * 5000
                        loops: Animation.Infinite; running: bd.animate }
                    onPaint: {
                        const g = getContext("2d"), w = width, h = height, c = cols[modelData.c]
                        g.clearRect(0, 0, w, h)
                        const faces = [[[w / 2, 0], [w, h / 2], [w / 2, h / 2]], [[w / 2, 0], [0, h / 2], [w / 2, h / 2]],
                                       [[w / 2, h], [w, h / 2], [w / 2, h / 2]], [[w / 2, h], [0, h / 2], [w / 2, h / 2]]]
                        const shade = [1.0, 1.45, 1.9, 2.6]
                        for (let f = 0; f < 4; ++f) {
                            g.fillStyle = Qt.darker(c, shade[f])
                            g.beginPath(); g.moveTo(faces[f][0][0], faces[f][0][1])
                            g.lineTo(faces[f][1][0], faces[f][1][1]); g.lineTo(faces[f][2][0], faces[f][2][1])
                            g.closePath(); g.fill()
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ portátil: grade de LCD e pixels piscando
    Component {
        id: lcdC
        Item {
            id: lcd
            Rectangle {   // brilho da "tela" no centro
                width: parent.width * 0.8; height: parent.height * 0.75; radius: 40 * bd.s
                anchors.centerIn: parent
                color: "#ffffff"; opacity: 0.035
            }
            Canvas {
                anchors.fill: parent
                opacity: 0.09
                onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                onPaint: {
                    const g = getContext("2d"), step = Math.max(4, Math.round(5 * bd.s))
                    g.clearRect(0, 0, width, height)
                    g.fillStyle = "#ffffff"
                    for (let y = 0; y < height; y += step)
                        for (let x = 0; x < width; x += step)
                            g.fillRect(x, y, step - 2, step - 2)
                }
            }
            Repeater {   // estrelinhas de pixel
                model: 12
                delegate: Item {
                    readonly property real c: Math.round(4 * bd.s)
                    width: c * 3; height: c * 3
                    x: lcd.width * ((index * 0.381 + 0.05) % 1)
                    y: lcd.height * ((index * 0.617 + 0.07) % 0.8)
                    opacity: 0
                    Rectangle { x: parent.c; width: parent.c; height: parent.c * 3; color: index % 3 ? "#ffffff" : "#ffd84a" }
                    Rectangle { y: parent.c; width: parent.c * 3; height: parent.c; color: index % 3 ? "#ffffff" : "#ffd84a" }
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite; running: bd.animate
                        PauseAnimation { duration: 600 + index * 530 }
                        NumberAnimation { to: 0.55; duration: 500 }
                        NumberAnimation { to: 0; duration: 900 }
                        PauseAnimation { duration: 2400 + (index * 917) % 3000 }
                    }
                }
            }
            Rectangle {   // faixa no rodapé, como a moldura do portátil
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: parent.height * 0.16
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00a493ff" }
                    GradientStop { position: 1.0; color: "#40a493ff" }
                }
            }
        }
    }
}
