import QtQuick
import QtMultimedia

// Vídeo de fundo: sem som, em loop. Carregado por um Loader para que a falta do
// módulo QtMultimedia não derrube a interface (aí só imagens funcionam).
Item {
    id: v
    property url source
    property bool playing: true
    property bool cover: true
    signal ready()
    signal failed(string why)

    property bool announced: false

    MediaPlayer {
        id: player
        source: v.source
        loops: MediaPlayer.Infinite
        videoOutput: out
        // sem audioOutput: o fundo nunca faz barulho
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && v.playing) player.play()
            if (!v.announced && (mediaStatus === MediaPlayer.BufferedMedia || mediaStatus === MediaPlayer.BufferingMedia)) {
                v.announced = true
                // espera o primeiro quadro chegar antes de aparecer
                firstFrame.start()
            } else if (mediaStatus === MediaPlayer.InvalidMedia) {
                v.failed("vídeo inválido ou formato sem suporte")
            }
        }
        onErrorOccurred: (error, msg) => v.failed(msg)
    }
    Timer { id: firstFrame; interval: 120; onTriggered: v.ready() }

    VideoOutput {
        id: out
        anchors.fill: parent
        fillMode: v.cover ? VideoOutput.PreserveAspectCrop : VideoOutput.PreserveAspectFit
    }

    onPlayingChanged: playing ? player.play() : player.pause()
    // a fonte chega depois do Loader criar o item: só então dá para tocar
    onSourceChanged: if (source.toString() !== "" && playing) player.play()
    Component.onDestruction: player.stop()
}
