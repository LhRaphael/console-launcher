#include "pointer.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWindow>

PointerInput::PointerInput(QObject *parent) : QObject(parent)
{
    qApp->installEventFilter(this);
}

void PointerInput::setMouseActive(bool on)
{
    if (on == m_mouseActive)
        return;
    m_mouseActive = on;
    // cursor escondido enquanto se joga/navega pelo controle ou teclado
    if (!on && !m_cursorHidden) {
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
        m_cursorHidden = true;
    } else if (on && m_cursorHidden) {
        QGuiApplication::restoreOverrideCursor();
        m_cursorHidden = false;
    }
    emit mouseActiveChanged();
}

bool PointerInput::eventFilter(QObject *obj, QEvent *ev)
{
    // só os eventos que chegam às janelas (cada evento passa também pelos itens)
    if (!obj->isWindowType())
        return QObject::eventFilter(obj, ev);

    switch (ev->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        const auto *me = static_cast<QMouseEvent *>(ev);
        setMouseActive(true);
        // Linux/Qt: BTN_SIDE = BackButton (ExtraButton1), BTN_EXTRA = ForwardButton (ExtraButton2);
        // mouses que mandam BTN_FORWARD/BTN_BACK chegam como ExtraButton3/ExtraButton4.
        const Qt::MouseButton b = me->button();
        if (b == Qt::BackButton || b == Qt::ExtraButton4) {
            if (ev->type() == QEvent::MouseButtonPress)
                emit back();
            return true;
        }
        if (b == Qt::ForwardButton || b == Qt::ExtraButton3) {
            if (ev->type() == QEvent::MouseButtonPress)
                emit forward();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        const Qt::MouseButton b = static_cast<QMouseEvent *>(ev)->button();
        if (b == Qt::BackButton || b == Qt::ExtraButton4 || b == Qt::ForwardButton || b == Qt::ExtraButton3)
            return true;
        break;
    }
    case QEvent::MouseMove:
    case QEvent::Wheel:
        setMouseActive(true);
        break;
    case QEvent::KeyPress: {
        if (m_sending)
            break;  // tecla gerada por um clique: continua no modo mouse
        const auto *ke = static_cast<QKeyEvent *>(ev);
        if (ke->key() == Qt::Key_Back || (ke->key() == Qt::Key_Left && (ke->modifiers() & Qt::AltModifier))) {
            emit back();
            return true;
        }
        if (ke->key() == Qt::Key_Forward || (ke->key() == Qt::Key_Right && (ke->modifiers() & Qt::AltModifier))) {
            emit forward();
            return true;
        }
        // modificadores sozinhos (Alt, Shift…) não tiram do modo mouse
        if (ke->key() != Qt::Key_Alt && ke->key() != Qt::Key_Shift && ke->key() != Qt::Key_Control
            && ke->key() != Qt::Key_Meta)
            setMouseActive(false);
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(obj, ev);
}

void PointerInput::sendKey(int key, int modifiers)
{
    QWindow *w = m_window ? m_window.data() : QGuiApplication::focusWindow();
    if (!w)
        return;
    m_sending = true;
    QKeyEvent press(QEvent::KeyPress, key, Qt::KeyboardModifiers(modifiers));
    QGuiApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::KeyboardModifiers(modifiers));
    QGuiApplication::sendEvent(w, &release);
    m_sending = false;
}
