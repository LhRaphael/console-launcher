#pragma once
#include <QObject>
#include <QTimer>

class QWindow;
class Runner;

// Lê controles via SDL2 e converte em teclas para a interface QML.
// Mapeamento (o mesmo em todas as telas):
//   D-pad / analógico esquerdo -> setas (com repetição ao segurar)
//   A -> Enter   B -> Esc   X -> F2   Y -> F3   Select/View -> F4
//   LB/RB -> PageUp/PageDown   Start -> Menu   Guia -> Menu (ou traz o launcher de volta)
//   segurar Guia, ou Start+Select, por ~1,2 s -> fecha o jogo em execução
// As teclas vindas do controle levam nativeScanCode = kPadScanCode, para a
// interface distinguir controle de teclado (ex.: teclado na tela).
class Gamepad : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    // "xbox" | "playstation" | "nintendo" | "keyboard"
    Q_PROPERTY(QString style READ style NOTIFY changed)
    Q_PROPERTY(bool supported READ supported CONSTANT)

public:
    static constexpr quint32 kPadScanCode = 0xC0DE;

    explicit Gamepad(Runner *runner, QObject *parent = nullptr);
    ~Gamepad() override;

    void setWindow(QWindow *w) { m_window = w; }
    bool connected() const { return m_count > 0; }
    QString name() const { return m_name; }
    QString style() const { return m_count > 0 ? m_style : QStringLiteral("keyboard"); }
    bool supported() const;

signals:
    void changed();
    void showRequested();  // Guia com outra janela (jogo/app) em foco

private:
    void poll();
    void sendKey(int key);
    void pressDirection(int key);
    void releaseDirection(int key);
    void updateDevices();
    void armCloseTimer();

    Runner *m_runner;
    QWindow *m_window = nullptr;
    QTimer m_poll, m_repeat, m_hold;
    int m_repeatKey = 0;
    int m_stickKey = 0;
    int m_count = 0;
    bool m_startHeld = false, m_backHeld = false, m_guideHeld = false;
    bool m_holdFired = false, m_comboUsed = false;
    QString m_name, m_style = QStringLiteral("xbox");
    bool m_ok = false;
};
