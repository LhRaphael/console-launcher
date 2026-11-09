#include "apps.h"
#include "bios.h"
#include "gamelogs.h"
#include "gamemode.h"
#include "pointer.h"
#include "wallpaper.h"
#include "fileman.h"
#include "gamepad.h"
#include "hotkeys.h"
#include "importer.h"
#include "library.h"
#include "runner.h"
#include "systemcontrol.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct Cli {
    QCommandLineParser parser;
    QCommandLineOption windowed{"windowed", "Abre em janela em vez de tela cheia."};
    QCommandLineOption size{"size", "Tamanho da janela com --windowed (ex.: 1280x720).", "LxA", "1280x720"};
    QCommandLineOption config{"config", "Usa outro arquivo de consoles (systems.json).", "arquivo"};
    QCommandLineOption tab{"tab", "Abre direto na aba de um console (ex.: ps2, apps).", "id"};
    QCommandLineOption action{"action",
                              "Envia um comando ao launcher já aberto: show (trazer para frente), "
                              "close-game (fechar o jogo), menu, toggle-overlay.",
                              "comando"};
    QCommandLineOption screenshot{"screenshot", "Salva uma captura da interface e sai (testes).", "arquivo.png"};
    QCommandLineOption demo{"demo", "Estado de demonstração para capturas: menu | running | console | game | import | keyboard | help | bios[:id].", "estado"};
    QCommandLineOption biosImport{"import-bios",
                                  "Identifica e instala BIOS/firmware do console (ex.: ps2) a partir dos "
                                  "arquivos, pastas ou compactados indicados, sem abrir a interface.",
                                  "console"};
    QCommandLineOption biosStatus{"bios-status", "Mostra a situação da BIOS/firmware de cada console e sai."};
    QCommandLineOption resumeApps{"resume-apps", "Retoma programas que o modo jogo deixou pausados (se o launcher caiu) e sai."};
    Cli()
    {
        parser.setApplicationDescription("Interface de console para escolher e iniciar jogos e aplicativos.");
        parser.addHelpOption();
        parser.addVersionOption();
        parser.addOptions({windowed, size, config, tab, action, screenshot, demo, biosImport, biosStatus, resumeApps});
        parser.addPositionalArgument("arquivos", "Com --import-bios: arquivos, pastas ou compactados.", "[arquivos…]");
    }
};

QString serverName() { return QStringLiteral("console-launcher-%1").arg(getuid()); }

// Entrega um comando à instância aberta. false = nenhuma instância rodando.
bool sendToRunning(const QString &cmd)
{
    QLocalSocket probe;
    probe.connectToServer(serverName());
    if (!probe.waitForConnected(400))
        return false;
    probe.write(cmd.toUtf8() + '\n');
    probe.waitForBytesWritten(1000);
    probe.disconnectFromServer();
    return true;
}

void setNames()
{
    QCoreApplication::setApplicationName("console-launcher");
    QCoreApplication::setOrganizationName("console-launcher");
    QCoreApplication::setApplicationVersion(APP_VERSION);
}

// SIGTERM/SIGINT/SIGHUP (desligar, sair da sessão, Ctrl+C) viram um quit() normal,
// para o modo jogo retomar o que pausou antes de o launcher sair.
int g_sigFd[2] = {-1, -1};
void onUnixSignal(int)
{
    const char c = 1;
    [[maybe_unused]] ssize_t n = ::write(g_sigFd[0], &c, 1);
}
void installQuitSignals(QCoreApplication *app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sigFd) != 0)
        return;
    auto *sn = new QSocketNotifier(g_sigFd[1], QSocketNotifier::Read, app);
    QObject::connect(sn, &QSocketNotifier::activated, app, [app, sn] {
        sn->setEnabled(false);
        char c;
        [[maybe_unused]] ssize_t n = ::read(g_sigFd[1], &c, 1);
        app->quit();
    });
    struct sigaction sa {};
    sa.sa_handler = onUnixSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (int sig : {SIGTERM, SIGINT, SIGHUP})
        ::sigaction(sig, &sa, nullptr);
}

// --import-bios / --bios-status: no terminal, sem tela
int runBiosCli(int argc, char *argv[])
{
    QCoreApplication core(argc, argv);
    setNames();
    Cli c;
    c.parser.process(core);
    Runner runner;
    Library library(c.parser.value(c.config), &runner);
    BiosManager bios(&library, &runner);
    auto out = [](const QString &s) { fprintf(stdout, "%s\n", s.toLocal8Bit().constData()); };

    if (c.parser.isSet(c.biosStatus)) {
        for (const SystemDef &s : library.systemDefs()) {
            const QVariantMap st = bios.state(s.id);
            const QString need = st.value("need").toString();
            out(QStringLiteral("%1 [%2] %3%4").arg(s.name, s.id,
                                                    need == "none" ? QStringLiteral("— ") : st.value("ok").toBool() ? QStringLiteral("✓ ") : QStringLiteral("✗ "),
                                                    st.value("summary").toString()));
            for (const QVariant &v : st.value("items").toList()) {
                const QVariantMap it = v.toMap();
                out(QStringLiteral("    %1: %2 (%3)").arg(it.value("label").toString(), it.value("detail").toString(),
                                                         it.value("state").toString()));
            }
        }
        return 0;
    }

    const QString id = c.parser.value(c.biosImport);
    const QStringList paths = c.parser.positionalArguments();
    const QString err = bios.start(id, paths, false);
    if (!err.isEmpty()) {
        fprintf(stderr, "%s\n", err.toLocal8Bit().constData());
        return 1;
    }
    int code = 0;
    QObject::connect(&bios, &BiosManager::finished, &core, [&](const QVariantMap &r) {
        for (const QVariant &v : r.value("imported").toList())
            out(QStringLiteral("✓ %1 → %2").arg(v.toMap().value("title").toString(), v.toMap().value("file").toString()));
        for (const QVariant &v : r.value("present").toList())
            out(QStringLiteral("= %1 (já instalado em %2)").arg(v.toMap().value("title").toString(), v.toMap().value("file").toString()));
        for (const QVariant &v : r.value("skipped").toList())
            out(QStringLiteral("⚠ %1: %2").arg(v.toMap().value("name").toString(), v.toMap().value("reason").toString()));
        for (const QVariant &v : r.value("notes").toList())
            out(QStringLiteral("ℹ %1").arg(v.toString()));
        if (!r.value("firmware").toMap().isEmpty())
            out(QStringLiteral("ℹ Para instalar o firmware: abra o launcher ▸ Configurar o console ▸ Instalar firmware agora"));
        code = r.value("imported").toList().isEmpty() && r.value("present").toList().isEmpty() ? 2 : 0;
        core.quit();
    });
    core.exec();
    return code;
}

}  // namespace

int main(int argc, char *argv[])
{
    // --help, --version e --action funcionam sem tela (terminal, SSH, atalhos do sistema).
    bool noGui = false, hasAction = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if (a == "-h" || a == "--help" || a == "--help-all" || a == "-v" || a == "--version")
            noGui = true;
        if (a.startsWith("--action"))
            hasAction = true;
        if ((a.startsWith("--import-bios") || a == "--bios-status") && !noGui)
            return runBiosCli(argc, argv);
        if (a == "--resume-apps" && !noGui) {
            QCoreApplication core(argc, argv);
            setNames();
            const int n = GameMode::recover();
            fprintf(stdout, "%d programa(s) retomado(s).\n", n);
            return 0;
        }
    }
    if (noGui || hasAction) {
        QCoreApplication core(argc, argv);
        setNames();
        Cli c;
        c.parser.process(core);  // --help/--version saem aqui
        const QString cmd = c.parser.isSet(c.action) ? c.parser.value(c.action) : QStringLiteral("show");
        if (sendToRunning(cmd))
            return 0;
        if (cmd != "show") {
            fprintf(stderr, "console-launcher não está aberto.\n");
            return 1;
        }
        // "show" sem instância aberta: segue e abre o launcher
    }

    QGuiApplication app(argc, argv);
    installQuitSignals(&app);
    setNames();
    QGuiApplication::setApplicationDisplayName("Console Launcher");
    QGuiApplication::setDesktopFileName("console-launcher");

    Cli c;
    QCommandLineParser &cli = c.parser;
    const QCommandLineOption &windowed = c.windowed, &size = c.size, &config = c.config, &tab = c.tab,
                             &screenshot = c.screenshot, &demo = c.demo;
    cli.process(app);

    // ---------------------------------------------------------------- instância única
    // Abrir de novo só traz para frente a instância que já está rodando.
    const bool testMode = cli.isSet(screenshot);
    if (!testMode && sendToRunning(QStringLiteral("show")))
        return 0;

    int w = 1280, h = 720;
    const QStringList wh = cli.value(size).split('x');
    if (wh.size() == 2) {
        w = qMax(640, wh[0].toInt());
        h = qMax(360, wh[1].toInt());
    }

    setupIconTheme();
    Runner runner;
    Library library(cli.value(config), &runner);
    Importer importer(&library);
    Gamepad gamepad(&runner);
    SystemControl systemControl;
    FileManager fileManager(&library);
    BiosManager bios(&library, &runner);
    GameLogs gameLogs(&runner);
    GameMode gameMode(&runner, &systemControl);
    PointerInput pointer;
    Wallpapers wallpapers(&library);
    QObject::connect(&library, &Library::appStarted, &systemControl, &SystemControl::trackApp);
    Hotkeys hotkeys;
    const QStringList activeHotkeys = testMode ? QStringList() : hotkeys.install();

    QQmlApplicationEngine engine;
    engine.addImageProvider("appicon", new AppIconProvider);
    engine.rootContext()->setContextProperty("library", &library);
    engine.rootContext()->setContextProperty("importer", &importer);
    engine.rootContext()->setContextProperty("runner", &runner);
    engine.rootContext()->setContextProperty("gamepad", &gamepad);
    engine.rootContext()->setContextProperty("machine", &systemControl);
    engine.rootContext()->setContextProperty("files", &fileManager);
    engine.rootContext()->setContextProperty("bios", &bios);
    engine.rootContext()->setContextProperty("logs", &gameLogs);
    engine.rootContext()->setContextProperty("gameMode", &gameMode);
    engine.rootContext()->setContextProperty("pointer", &pointer);
    engine.rootContext()->setContextProperty("wallpapers", &wallpapers);
    engine.rootContext()->setContextProperty("launchOptions",
                                             QVariantMap{{"windowed", cli.isSet(windowed)},
                                                         {"width", w},
                                                         {"height", h},
                                                         {"tab", cli.value(tab)},
                                                         {"demo", cli.value(demo)},
                                                         {"hotkeys", activeHotkeys},
                                                         {"hotkeyProblem", hotkeys.problem()},
                                                         {"padScanCode", int(Gamepad::kPadScanCode)}});
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    pointer.setWindow(window);
    gamepad.setWindow(window);

    auto showLauncher = [&] {
        if (cli.isSet(windowed))
            window->showNormal();
        else
            window->showFullScreen();
        window->raise();
        window->requestActivate();
    };
    QLocalServer server;
    auto handleAction = [&](const QString &a) {
        if (a == "show") {
            showLauncher();
        } else if (a == "close-game") {
            runner.stop();
        } else if (a == "menu") {
            showLauncher();
            QMetaObject::invokeMethod(window, "openMenu");
        } else if (a == "toggle-overlay") {
            library.setOverlayEnabled(!library.overlayEnabled());
        } else if (a == "restart") {
            // depois de uma atualização: abre a versão nova com os mesmos argumentos
            QStringList args = QCoreApplication::arguments();
            args.removeFirst();
            server.close();
            QString exe = QCoreApplication::applicationFilePath();
            exe.remove(QStringLiteral(" (deleted)"));  // o binário foi trocado pelo apt
            QProcess::startDetached(exe, args);
            QCoreApplication::quit();
        }
    };

    if (!testMode) {
        QLocalServer::removeServer(serverName());
        server.listen(serverName());
        QObject::connect(&server, &QLocalServer::newConnection, &server, [&] {
            while (QLocalSocket *s = server.nextPendingConnection()) {
                QObject::connect(s, &QLocalSocket::readyRead, s, [s, &handleAction] {
                    while (s->canReadLine())
                        handleAction(QString::fromUtf8(s->readLine()).trimmed());
                });
                QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    QObject::connect(&hotkeys, &Hotkeys::triggered, window, handleAction);
    QObject::connect(&gamepad, &Gamepad::showRequested, window, showLauncher);
    // Quando o jogo fecha, traz o launcher de volta para frente.
    QObject::connect(&runner, &Runner::runningChanged, window, [&] {
        if (!runner.running())
            showLauncher();
    });

    if (testMode) {
        const QString out = cli.value(screenshot);
        QTimer::singleShot(2200, window, [window, out] {
            const QImage img = window->grabWindow();
            const bool ok = img.save(out);
            fprintf(stderr, "screenshot %s: %s (%dx%d)\n", qPrintable(out), ok ? "ok" : "falhou",
                    img.width(), img.height());
            QGuiApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
