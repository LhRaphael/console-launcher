#pragma once
#include <QObject>
#include <QPointer>
#include <QWindow>



// Mouse em toda a interface:
//  - botões laterais (voltar/avançar) de qualquer mouse → back()/forward(),
//    incluindo os que o Linux chama de BTN_SIDE/BTN_EXTRA e BTN_BACK/BTN_FORWARD,
//    e as teclas Voltar/Avançar de teclados multimídia;
//  - mouseActive: o último uso foi o mouse (a interface mostra destaques de
//    "passar por cima") e o cursor some quando o controle/teclado volta a ser usado;
//  - sendKey(): um clique numa dica de botão ("A Jogar", "B Voltar"…) faz o mesmo
//    que apertar o botão.
class PointerInput : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool mouseActive READ mouseActive NOTIFY mouseActiveChanged)

public:
    explicit PointerInput(QObject *parent = nullptr);

    void setWindow(QWindow *w) { m_window = w; }
    bool mouseActive() const { return m_mouseActive; }

    Q_INVOKABLE void sendKey(int key, int modifiers = 0);

signals:
    void back();
    void forward();
    void mouseActiveChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void setMouseActive(bool on);
    QPointer<QWindow> m_window;
    bool m_mouseActive = true;
    bool m_cursorHidden = false;
    bool m_sending = false;
};
