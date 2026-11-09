#pragma once
#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>
#include <QStringList>

// Atalhos de teclado globais (funcionam com qualquer janela em foco), via X11.
//   Super+Esc         volta para o launcher
//   Super+Shift+Esc   fecha o jogo em execução
// Em Wayland não há como capturar teclas globalmente: use
// "console-launcher --action show|close-game|menu" num atalho do próprio ambiente.
class Hotkeys : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit Hotkeys(QObject *parent = nullptr);
    ~Hotkeys() override;

    QStringList install();  // retorna os atalhos ativos
    QString problem() const { return m_problem; }
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

signals:
    void triggered(const QString &action);

private:
    struct Grab {
        quint8 code;
        quint16 mods;
        QString action;
    };
    QList<Grab> m_grabs;
    void *m_conn = nullptr;
    quint32 m_root = 0;
    QString m_problem;
};
