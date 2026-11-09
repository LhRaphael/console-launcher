#include "gamepad.h"
#include "runner.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QWindow>

#ifdef HAVE_SDL2
#include <SDL.h>
#endif

namespace {
constexpr int kRepeatDelay = 380;   // ms até começar a repetir
constexpr int kRepeatRate = 90;     // ms entre repetições
constexpr int kHoldToClose = 1200;  // ms segurando Guia para fechar o jogo
constexpr int kStickOn = 20000;     // limiar do analógico (de 32767)
constexpr int kStickOff = 12000;    // histerese para soltar
}

Gamepad::Gamepad(Runner *runner, QObject *parent) : QObject(parent), m_runner(runner)
{
    m_repeat.setSingleShot(false);
    connect(&m_repeat, &QTimer::timeout, this, [this] {
        m_repeat.setInterval(kRepeatRate);
        if (m_repeatKey)
            sendKey(m_repeatKey);
    });
    m_hold.setSingleShot(true);
    m_hold.setInterval(kHoldToClose);
    connect(&m_hold, &QTimer::timeout, this, [this] {
        m_holdFired = true;
        if (m_runner->running())
            m_runner->stop();
    });

#ifdef HAVE_SDL2
    // O launcher precisa ouvir o controle mesmo sem foco (para fechar o jogo).
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0) {
        m_ok = true;
        connect(&m_poll, &QTimer::timeout, this, &Gamepad::poll);
        m_poll.start(16);
    } else {
        qWarning("SDL2: %s", SDL_GetError());
    }
#endif
}

Gamepad::~Gamepad()
{
#ifdef HAVE_SDL2
    if (m_ok)
        SDL_Quit();
#endif
}

bool Gamepad::supported() const { return m_ok; }

void Gamepad::sendKey(int key)
{
    // Com um jogo ou app em foco, o controle é dele: o launcher não reage.
    if (!m_window || !m_window->isActive())
        return;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, kPadScanCode, 0, 0);
    QGuiApplication::sendEvent(m_window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, kPadScanCode, 0, 0);
    QGuiApplication::sendEvent(m_window, &release);
}

void Gamepad::pressDirection(int key)
{
    sendKey(key);
    m_repeatKey = key;
    m_repeat.setInterval(kRepeatDelay);
    m_repeat.start();
}

void Gamepad::releaseDirection(int key)
{
    if (m_repeatKey == key) {
        m_repeatKey = 0;
        m_repeat.stop();
    }
}

void Gamepad::armCloseTimer()
{
    if (m_startHeld && m_backHeld)
        m_comboUsed = true;
    const bool combo = m_guideHeld || (m_startHeld && m_backHeld);
    if (combo && !m_hold.isActive()) {
        m_holdFired = false;
        m_hold.start();
    } else if (!combo) {
        m_hold.stop();
    }
}

void Gamepad::updateDevices()
{
#ifdef HAVE_SDL2
    int count = 0;
    QString name, style = "xbox";
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i))
            continue;
        SDL_GameController *gc = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (!gc)
            gc = SDL_GameControllerOpen(i);
        if (!gc)
            continue;
        if (++count == 1) {
            name = QString::fromUtf8(SDL_GameControllerName(gc));
            switch (SDL_GameControllerGetType(gc)) {
            case SDL_CONTROLLER_TYPE_PS3:
            case SDL_CONTROLLER_TYPE_PS4:
            case SDL_CONTROLLER_TYPE_PS5:
                style = "playstation";
                break;
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
                style = "nintendo";
                break;
            default:
                style = "xbox";
            }
        }
    }
    if (count != m_count || name != m_name || style != m_style) {
        m_count = count;
        m_name = name;
        m_style = style;
        emit changed();
    }
#endif
}

void Gamepad::poll()
{
#ifdef HAVE_SDL2
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (SDL_GameController *gc = SDL_GameControllerFromInstanceID(e.cdevice.which))
                    SDL_GameControllerClose(gc);
            }
            updateDevices();
            break;

        case SDL_CONTROLLERBUTTONDOWN:
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    pressDirection(Qt::Key_Up); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  pressDirection(Qt::Key_Down); break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  pressDirection(Qt::Key_Left); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: pressDirection(Qt::Key_Right); break;
            case SDL_CONTROLLER_BUTTON_A:          sendKey(Qt::Key_Return); break;
            case SDL_CONTROLLER_BUTTON_B:          sendKey(Qt::Key_Escape); break;
            case SDL_CONTROLLER_BUTTON_X:          sendKey(Qt::Key_F2); break;
            case SDL_CONTROLLER_BUTTON_Y:          sendKey(Qt::Key_F3); break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  sendKey(Qt::Key_PageUp); break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: sendKey(Qt::Key_PageDown); break;
            case SDL_CONTROLLER_BUTTON_START:  m_startHeld = true; armCloseTimer(); break;
            case SDL_CONTROLLER_BUTTON_BACK:   m_backHeld = true; armCloseTimer(); break;
            case SDL_CONTROLLER_BUTTON_GUIDE:  m_guideHeld = true; armCloseTimer(); break;
            default: break;
            }
            break;

        case SDL_CONTROLLERBUTTONUP:
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    releaseDirection(Qt::Key_Up); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  releaseDirection(Qt::Key_Down); break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  releaseDirection(Qt::Key_Left); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: releaseDirection(Qt::Key_Right); break;
            case SDL_CONTROLLER_BUTTON_START:
            case SDL_CONTROLLER_BUTTON_BACK:
            case SDL_CONTROLLER_BUTTON_GUIDE: {
                const auto b = e.cbutton.button;
                // Toques curtos: Start = menu, Select = importar, Guia = menu ou voltar ao launcher.
                // Depois de um combo (Start+Select) ou de segurar para fechar, nada acontece.
                if (!m_holdFired && !m_comboUsed) {
                    if (b == SDL_CONTROLLER_BUTTON_START)
                        sendKey(Qt::Key_Menu);
                    else if (b == SDL_CONTROLLER_BUTTON_BACK)
                        sendKey(Qt::Key_F4);
                }
                if (!m_holdFired && b == SDL_CONTROLLER_BUTTON_GUIDE) {
                    if (m_window && m_window->isActive())
                        sendKey(Qt::Key_Menu);
                    else
                        emit showRequested();
                }
                if (b == SDL_CONTROLLER_BUTTON_START) m_startHeld = false;
                if (b == SDL_CONTROLLER_BUTTON_BACK) m_backHeld = false;
                if (b == SDL_CONTROLLER_BUTTON_GUIDE) m_guideHeld = false;
                armCloseTimer();
                if (!m_startHeld && !m_backHeld && !m_guideHeld)
                    m_comboUsed = m_holdFired = false;
                break;
            }
            default: break;
            }
            break;

        case SDL_CONTROLLERAXISMOTION: {
            if (e.caxis.axis != SDL_CONTROLLER_AXIS_LEFTX && e.caxis.axis != SDL_CONTROLLER_AXIS_LEFTY)
                break;
            SDL_GameController *gc = SDL_GameControllerFromInstanceID(e.caxis.which);
            if (!gc)
                break;
            const int x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
            const int y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
            const int limit = m_stickKey ? kStickOff : kStickOn;
            int key = 0;
            if (qAbs(x) >= qAbs(y) && qAbs(x) > limit)
                key = x > 0 ? Qt::Key_Right : Qt::Key_Left;
            else if (qAbs(y) > qAbs(x) && qAbs(y) > limit)
                key = y > 0 ? Qt::Key_Down : Qt::Key_Up;
            if (key != m_stickKey) {
                if (m_stickKey)
                    releaseDirection(m_stickKey);
                m_stickKey = key;
                if (key)
                    pressDirection(key);
            }
            break;
        }
        default:
            break;
        }
    }
#endif
}
