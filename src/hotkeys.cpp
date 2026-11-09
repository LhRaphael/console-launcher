#include "hotkeys.h"

#include <QGuiApplication>

#ifdef HAVE_XCB
#include <xcb/xcb.h>
#include <cstdlib>
#endif

Hotkeys::Hotkeys(QObject *parent) : QObject(parent) {}

Hotkeys::~Hotkeys()
{
#ifdef HAVE_XCB
    if (!m_conn)
        return;
    auto *c = static_cast<xcb_connection_t *>(m_conn);
    const quint16 extras[] = {0, XCB_MOD_MASK_LOCK, XCB_MOD_MASK_2, XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2};
    for (const Grab &g : m_grabs)
        for (quint16 e : extras)
            xcb_ungrab_key(c, g.code, m_root, g.mods | e);
    xcb_flush(c);
#endif
}

QStringList Hotkeys::install()
{
    QStringList active;
#ifdef HAVE_XCB
    auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->connection()) {
        m_problem = QStringLiteral("Atalhos globais de teclado só funcionam em sessão X11. "
                                   "No Wayland, crie atalhos no sistema para: console-launcher --action show");
        return active;
    }
    auto *c = x11->connection();
    m_conn = c;
    m_root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;

    // tecla (keysym) -> código físico do teclado atual
    const xcb_setup_t *setup = xcb_get_setup(c);
    const int minKc = setup->min_keycode, maxKc = setup->max_keycode;
    xcb_get_keyboard_mapping_reply_t *map =
        xcb_get_keyboard_mapping_reply(c, xcb_get_keyboard_mapping(c, minKc, maxKc - minKc + 1), nullptr);
    auto keycodeFor = [&](quint32 sym) -> quint8 {
        if (!map)
            return 0;
        const int per = map->keysyms_per_keycode;
        const xcb_keysym_t *syms = xcb_get_keyboard_mapping_keysyms(map);
        for (int kc = minKc; kc <= maxKc; ++kc)
            for (int j = 0; j < per; ++j)
                if (syms[(kc - minKc) * per + j] == sym)
                    return quint8(kc);
        return 0;
    };

    struct Def {
        quint32 sym;
        quint16 mods;
        const char *action, *label;
    };
    const Def defs[] = {
        {0xff1b, XCB_MOD_MASK_4, "show", "Super+Esc"},
        {0xff1b, XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT, "close-game", "Super+Shift+Esc"},
    };
    // Caps Lock e Num Lock não podem atrapalhar o atalho
    const quint16 extras[] = {0, XCB_MOD_MASK_LOCK, XCB_MOD_MASK_2, XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2};
    QStringList busy;
    for (const Def &d : defs) {
        const quint8 code = keycodeFor(d.sym);
        if (!code)
            continue;
        bool ok = true;
        for (quint16 e : extras) {
            xcb_void_cookie_t ck = xcb_grab_key_checked(c, 1, m_root, d.mods | e, code, XCB_GRAB_MODE_ASYNC,
                                                         XCB_GRAB_MODE_ASYNC);
            if (xcb_generic_error_t *err = xcb_request_check(c, ck)) {
                ok = false;
                free(err);
            }
        }
        if (ok) {
            m_grabs << Grab{code, d.mods, QString::fromLatin1(d.action)};
            active << QString::fromLatin1(d.label);
        } else {
            busy << QString::fromLatin1(d.label);
        }
    }
    free(map);
    if (!busy.isEmpty())
        m_problem = QStringLiteral("Atalhos já usados por outro programa: %1").arg(busy.join(", "));
    if (!m_grabs.isEmpty())
        qApp->installNativeEventFilter(this);
#else
    m_problem = QStringLiteral("Compilado sem suporte a X11: atalhos globais de teclado indisponíveis.");
#endif
    return active;
}

bool Hotkeys::nativeEventFilter(const QByteArray &type, void *message, qintptr *)
{
#ifdef HAVE_XCB
    if (type != "xcb_generic_event_t")
        return false;
    auto *ev = static_cast<xcb_generic_event_t *>(message);
    if ((ev->response_type & 0x7f) != XCB_KEY_PRESS)
        return false;
    auto *k = reinterpret_cast<xcb_key_press_event_t *>(ev);
    if (k->event != m_root)
        return false;
    const quint16 state = k->state & quint16(~(XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2)) & 0xff;
    for (const Grab &g : std::as_const(m_grabs)) {
        if (g.code == k->detail && g.mods == state) {
            const QString action = g.action;
            QMetaObject::invokeMethod(this, [this, action] { emit triggered(action); }, Qt::QueuedConnection);
            return true;
        }
    }
#else
    Q_UNUSED(type);
    Q_UNUSED(message);
#endif
    return false;
}
