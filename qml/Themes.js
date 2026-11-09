.pragma library
// Temas por aba de console. Cada um lembra a geração/estética do console com elementos
// genéricos (paleta, tipografia, padrões de fundo); nenhum logotipo ou marca é reproduzido.
// Fontes: livres (OFL/Apache), em data/fonts.
//
// titleStyle: plain | multicolor (letras coloridas com contorno e sombra) | outline (só o
// contorno, em degradê) | glow (brilho) | block3d (letras com profundidade) | solid3d (cores em
// ciclo, cada uma com profundidade própria) | pixel (fonte de pixel com sombra dura)
// backdrop: o fundo desenhado em ThemeBackdrop.qml

var defs = {
    "default": {
        label: "Padrão", font: "", weight: 700, titleStyle: "plain", letterSpacing: 0, upper: false,
        titleScale: 1.0, tabScale: 1.0,
        bg: [null, "#0d0f15", "#06070a"], accent: null,
        fg: "#ffffff", fgDim: "#c8ccd6", fgMute: "#8d93a1", light: false,
        cardRadius: 12, cardSel: "#ffffff", cardIdle: "#2b303c", backdrop: "orb"
    },
    // Super Nintendo: 16 bits, desenho animado, as quatro cores dos botões, nuvens e blocos de pixel
    snes: {
        label: "16 bits", font: "lucky", weight: 400, titleStyle: "multicolor",
        titleColors: ["#e8392c", "#f4c20d", "#3cb44a", "#2a7de1"], outline: "#16162b", shadow: "#16162b",
        letterSpacing: 1.5, upper: false, titleScale: 1.12, tabScale: 1.0,
        bg: ["#3a3a8c", "#1a1b46", "#0b0b1e"], accent: "#8f7cf2",
        fg: "#ffffff", fgDim: "#d4d2f2", fgMute: "#a09cd0", light: false,
        cardRadius: 8, cardSel: "#ffd23f", cardIdle: "#3b3a6e", backdrop: "pixels"
    },
    // Nintendo 64: o 3D colorido dos anos 90 — letras gordas e extrudadas em quatro cores
    // primárias, piso quadriculado em perspectiva como nas primeiras fases em 3D
    n64: {
        label: "3D colorido", font: "titan", weight: 400, titleStyle: "solid3d",
        titleColors: ["#e8392c", "#2a7de1", "#3cb44a", "#f4c20d"], outline: "#10121f",
        letterSpacing: 1, upper: false, titleScale: 1.0, tabScale: 0.95,
        bg: ["#26306e", "#121738", "#06081a"], accent: "#3cb44a",
        fg: "#ffffff", fgDim: "#d2d8f2", fgMute: "#98a0c8", light: false,
        cardRadius: 10, cardSel: "#f4c20d", cardIdle: "#2a3160", backdrop: "floor"
    },
    // Game Boy Advance: portátil de 32 bits, tela de LCD com grade de pixels, índigo e
    // lilás, fonte de pixel com sombra dura
    gba: {
        label: "Portátil 32 bits", font: "press", weight: 400, titleStyle: "pixel",
        titleColors: ["#ffffff", "#c8bdff"], shadow: "#1b1450",
        letterSpacing: 0, upper: false, titleScale: 0.6, tabScale: 0.55,
        bg: ["#4a3aa8", "#231a63", "#0c0828"], accent: "#a493ff",
        fg: "#ffffff", fgDim: "#d6d0ff", fgMute: "#a49cd8", light: false,
        cardRadius: 6, cardSel: "#ffffff", cardIdle: "#3a2f80", backdrop: "lcd"
    },
    // PlayStation: começo do 3D poligonal, preto e cinza com triângulos de cores primárias
    ps1: {
        label: "Polígonos 32 bits", font: "exo2", weight: 800, titleStyle: "plain",
        letterSpacing: 2, upper: false, titleScale: 1.0, tabScale: 0.95,
        bg: ["#26272c", "#121316", "#050506"], accent: "#c9ced8",
        fg: "#f2f2f2", fgDim: "#c4c7cf", fgMute: "#8b8f99", light: false,
        cardRadius: 4, cardSel: "#f2f2f2", cardIdle: "#33353c", backdrop: "poly"
    },
    // PlayStation 2: preto e azul profundo, linhas finas em degradê violeta→ciano, "torres"
    ps2: {
        label: "Torres azuis", font: "orbitron", weight: 500, titleStyle: "outline",
        titleColors: ["#6d5bd0", "#2fb4ff"], letterSpacing: 4, upper: true, titleScale: 0.92, tabScale: 0.82,
        bg: ["#040a1c", "#020612", "#000000"], accent: "#2f8cff",
        fg: "#e8f0ff", fgDim: "#9fb6e6", fgMute: "#6a7fa8", light: false,
        cardRadius: 2, cardSel: "#3fb6ff", cardIdle: "#13213f", backdrop: "towers"
    },
    // PlayStation 3: a "onda" sobre um fundo que muda de cor com o mês, letras largas e prateadas
    ps3: {
        label: "Onda", font: "michroma", weight: 400, titleStyle: "plain",
        letterSpacing: 3, upper: false, titleScale: 0.8, tabScale: 0.72,
        bg: "month", accent: "#e6e9ef",
        fg: "#ffffff", fgDim: "#d7dbe3", fgMute: "#a6adba", light: false,
        cardRadius: 6, cardSel: "#ffffff", cardIdle: "#3a3f4a", backdrop: "waves"
    },
    // PlayStation 4: azul profundo, luz suave subindo, letras finas e espaçadas
    ps4: {
        label: "Azul profundo", font: "titillium", weight: 300, titleStyle: "plain",
        letterSpacing: 6, upper: true, titleScale: 1.0, tabScale: 1.0,
        bg: ["#0d3270", "#061b42", "#020a1d"], accent: "#3a8dff",
        fg: "#ffffff", fgDim: "#c9dbf7", fgMute: "#8ea8d4", light: false,
        cardRadius: 4, cardSel: "#ffffff", cardIdle: "#1a335f", backdrop: "bokeh"
    },
    // Xbox (2001): preto e verde "radioativo", brilho pulsando, grade de hexágonos
    xbox: {
        label: "Verde radioativo", font: "russo", weight: 400, titleStyle: "glow",
        titleColors: ["#b4ff3c"], glow: "#2bd12b", letterSpacing: 1, upper: true, titleScale: 0.95, tabScale: 0.92,
        bg: ["#0c240a", "#050c04", "#000000"], accent: "#5ed41f",
        fg: "#eaffd9", fgDim: "#a6d98a", fgMute: "#6f9a5c", light: false,
        cardRadius: 10, cardSel: "#9bf00b", cardIdle: "#1d3a15", backdrop: "hex"
    },
    // GameCube: roxo, cubos, letras grossas com profundidade
    gc: {
        label: "Cubo roxo", font: "bungee", weight: 400, titleStyle: "block3d",
        titleColors: ["#b9a6ff", "#7d5cff"], depth: "#2b1773", letterSpacing: 1, upper: true,
        titleScale: 0.9, tabScale: 0.8,
        bg: ["#2e1f6b", "#140c33", "#07041a"], accent: "#8a6cff",
        fg: "#ffffff", fgDim: "#d3c9ff", fgMute: "#9d8fd6", light: false,
        cardRadius: 6, cardSel: "#c9b8ff", cardIdle: "#33245e", backdrop: "cubes"
    },
    // Wii: claro, branco e cinza, listras finas, cantos bem arredondados, azul-claro
    wii: {
        label: "Canais brancos", font: "varela", weight: 400, titleStyle: "plain",
        letterSpacing: 1, upper: false, titleScale: 1.0, tabScale: 1.0,
        bg: ["#fbfcfd", "#eef1f4", "#d9dee5"], accent: "#34bdeb",
        fg: "#3f454d", fgDim: "#5f6772", fgMute: "#8a929d", light: true,
        cardRadius: 26, cardSel: "#34bdeb", cardIdle: "#c9d0d8", backdrop: "stripes"
    },
    // Switch: cinza-escuro, vermelho e azul neon dos lados, formas limpas e arredondadas
    "switch": {
        label: "Neon", font: "rubik", weight: 700, titleStyle: "split",
        titleColors: ["#ff3c28", "#0ab9e6"], letterSpacing: 0.5, upper: false, titleScale: 1.0, tabScale: 0.95,
        bg: ["#323232", "#232323", "#161616"], accent: "#0ab9e6",
        fg: "#ffffff", fgDim: "#d0d0d0", fgMute: "#9a9a9a", light: false,
        cardRadius: 14, cardSel: "#0ab9e6", cardIdle: "#3d3d3d", backdrop: "neon"
    }
}

// Cor de fundo por mês (como o menu do PS3, que muda ao longo do ano)
var months = ["#8a7f3e", "#7a4f2a", "#4f7a34", "#5d8a3e", "#a0607a", "#7c5aa0",
              "#2f7aa0", "#2f6a9a", "#7a3e8a", "#9a7b2a", "#8a4a2a", "#9a2f2f"]

function get(id) {
    var t = defs[id] || defs["default"]
    if (t.bg === "month") {
        var c = months[new Date().getMonth()]
        var copy = {}
        for (var k in t) copy[k] = t[k]
        copy.bg = [c, "#151519", "#050507"]
        copy.monthColor = c
        return copy
    }
    return t
}
function has(id) { return defs[id] !== undefined }
