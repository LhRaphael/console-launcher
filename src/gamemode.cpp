#include "gamemode.h"
#include "runner.h"
#include "systemcontrol.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <csignal>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// ------------------------------------------------------------------ categorias
struct Category {
    const char *id, *label, *detail;
    bool byDefault;
    QStringList names;  // comparados (minúsculas) com comm, executável e argv[0]
};

const QList<Category> &categories()
{
    static const QList<Category> list{
        {"browsers", "Navegadores", "Firefox, Chromium, Chrome, Brave, Vivaldi, Epiphany…", true,
         {"firefox", "firefox-esr", "firefox-bin", "librewolf", "waterfox", "floorp", "zen", "zen-bin", "chromium",
          "chromium-browser", "chromium-browse", "chrome", "google-chrome", "brave", "brave-browser", "vivaldi-bin",
          "opera", "epiphany", "falkon", "midori", "msedge", "microsoft-edge"}},
        {"terminals", "Terminais", "GNOME Terminal, Console, Konsole, Kitty, Alacritty… (e o que estiver rodando neles)", true,
         {"gnome-terminal-server", "gnome-terminal-", "gnome-terminal", "kgx", "ptyxis", "ptyxis-agent", "konsole", "xterm",
          "uxterm", "alacritty", "kitty", "tilix", "xfce4-terminal", "foot", "wezterm-gui", "terminator", "lxterminal",
          "mate-terminal", "qterminal", "terminology", "blackbox", "ghostty"}},
        {"files", "Gerenciadores de arquivos", "Arquivos (Nautilus), Dolphin do KDE, Thunar, Nemo…", true,
         {"nautilus", "nemo", "thunar", "pcmanfm", "pcmanfm-qt", "caja", "dolphin", "krusader", "spacefm"}},
        {"office", "Editores e escritório", "Editor de texto, VS Code, LibreOffice, leitores de PDF, GIMP…", true,
         {"gedit", "gnome-text-editor", "kate", "kwrite", "mousepad", "xed", "pluma", "code", "codium", "code-oss",
          "sublime_text", "soffice.bin", "oosplash", "evince", "okular", "papers", "gimp", "gimp-2.10", "inkscape",
          "krita", "blender"}},
        {"index", "Indexação de arquivos", "Tracker/LocalSearch do GNOME e Baloo do KDE (leem o disco o tempo todo)", true,
         {"tracker-miner-fs-3", "tracker-miner-f", "tracker-extract-3", "tracker-extract", "localsearch-3",
          "localsearch", "localsearch-ext", "baloo_file", "baloo_file_extractor", "baloo_file_extr"}},
        {"updates", "Lojas e atualizações", "GNOME Software, Discover, avisos de atualização", true,
         {"gnome-software", "plasma-discover", "update-notifier", "update-manager", "mintupdate"}},
        {"wallpaper", "Papel de parede animado", "VideoWallpaper e similares (o vídeo fica escondido atrás do jogo)", true,
         {"videowallpaper", "mpvpaper", "xwinwrap", "hidamari", "komorebi"}},
        {"comm", "Comunicação", "Discord, Telegram, Slack, e-mail (desligado: muita gente usa voz durante o jogo)", false,
         {"discord", "telegram-desktop", "telegram", "slack", "signal-desktop", "thunderbird", "evolution",
          "element-desktop", "teams-for-linux", "zoom", "skypeforlinux"}},
        {"apps", "Apps abertos pelo launcher", "O que foi aberto pela aba Apps", true, {}},
    };
    return list;
}

// Nunca pausados (nem eles, nem quem os abriu): gravação, métricas, áudio, sessão
const QStringList kProtected{
    "console-launcher", "obs", "obs-studio", "gpu-screen-recorder", "gpu-screen-reco", "gsr-kms-server",
    "wf-recorder", "kooha", "simplescreenrecorder", "simplescreenrec", "vokoscreenng", "peek", "kazam",
    "linosd", "linosd-run", "mangohud", "gamemoded", "pipewire", "pipewire-pulse", "wireplumber", "pulseaudio",
    "gnome-shell", "kwin_wayland", "kwin_x11", "plasmashell", "xwayland", "systemd", "dbus-daemon", "dbus-broker",
    "xdg-desktop-portal", "steam", "steamwebhelper", "gamescope"};

QString stateFile()
{
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base))
        base = QDir::homePath() + "/.local/state";
    return base + "/console-launcher/modo-jogo.json";
}

bool readProc(qint64 pid, qint64 *ppid, qint64 *pgid, quint64 *start, QString *comm, char *state)
{
    QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray s = f.readAll();
    const int l = s.indexOf('('), r = s.lastIndexOf(')');
    if (l < 0 || r < l)
        return false;
    if (comm)
        *comm = QString::fromUtf8(s.mid(l + 1, r - l - 1));
    const QList<QByteArray> rest = s.mid(r + 2).split(' ');
    // rest[0]=state(3) rest[1]=ppid(4) rest[2]=pgrp(5) … rest[19]=starttime(22)
    if (rest.size() < 20)
        return false;
    if (state)
        *state = rest[0].isEmpty() ? '?' : rest[0][0];
    if (ppid)
        *ppid = rest[1].toLongLong();
    if (pgid)
        *pgid = rest[2].toLongLong();
    if (start)
        *start = rest[19].toULongLong();
    return true;
}

bool sameProcess(qint64 pid, quint64 start)
{
    quint64 now = 0;
    return readProc(pid, nullptr, nullptr, &now, nullptr, nullptr) && now == start;
}

QString niceName(const QString &raw)
{
    static const QHash<QString, QString> pretty{
        {"gnome-terminal-server", "Terminal"}, {"kgx", "Console"}, {"ptyxis", "Ptyxis"}, {"nautilus", "Arquivos"},
        {"firefox-esr", "Firefox"}, {"firefox", "Firefox"}, {"firefox-bin", "Firefox"}, {"chromium", "Chromium"},
        {"google-chrome", "Chrome"}, {"chrome", "Chrome"}, {"brave", "Brave"}, {"code", "VS Code"},
        {"soffice.bin", "LibreOffice"}, {"gnome-text-editor", "Editor de texto"}, {"gedit", "Editor de texto"},
        {"evince", "Visualizador de documentos"}, {"gnome-software", "Software"}, {"tracker-miner-fs-3", "Indexação"},
        {"localsearch-3", "Indexação"}, {"baloo_file", "Indexação"}, {"videowallpaper", "Papel de parede animado"},
        {"konsole", "Konsole"}, {"dolphin", "Dolphin (arquivos)"}, {"discord", "Discord"},
        {"telegram-desktop", "Telegram"}, {"thunderbird", "Thunderbird"}};
    return pretty.value(raw, raw);
}

bool setting(const char *key, bool def) { return QSettings().value(QStringLiteral("gameMode/") + key, def).toBool(); }

}  // namespace

// ==========================================================================

GameMode::GameMode(Runner *runner, SystemControl *machine, QObject *parent)
    : QObject(parent), m_runner(runner), m_machine(machine)
{
    const int resumed = recover();
    if (resumed > 0)
        qInfo("modo jogo: %d processo(s) retomado(s) de uma sessão anterior", resumed);
    connect(m_runner, &Runner::runningChanged, this, [this] {
        const bool game = m_runner->running() && m_runner->game().value("mode").toString() == "game";
        if (game && !m_active)
            activate();
        else if (!m_runner->running() && m_active)
            deactivate();
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_active)
            deactivate();
    });
}

GameMode::~GameMode()
{
    if (m_active)
        deactivate();
}

bool GameMode::forceFullscreen() { return setting("fullscreen", true); }

QList<GameMode::Proc> GameMode::scan() const
{
    QList<Proc> out;
    const uid_t me = getuid();
    const QStringList pids = QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &p : pids) {
        bool ok = false;
        const qint64 pid = p.toLongLong(&ok);
        if (!ok || pid <= 1)
            continue;
        struct stat st;
        if (::stat(QStringLiteral("/proc/%1").arg(pid).toLocal8Bit().constData(), &st) != 0 || st.st_uid != me)
            continue;
        Proc pr;
        pr.pid = pid;
        if (!readProc(pid, &pr.ppid, &pr.pgid, &pr.start, &pr.comm, &pr.state))
            continue;
        if (pr.state == 'Z')
            continue;
        pr.exe = QFileInfo(QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid))).fileName();
        QFile cl(QStringLiteral("/proc/%1/cmdline").arg(pid));
        if (cl.open(QIODevice::ReadOnly))
            pr.argv0 = QFileInfo(QString::fromLocal8Bit(cl.read(4096).split('\0').value(0))).fileName();
        out << pr;
    }
    return out;
}

QList<QPair<QString, QList<GameMode::Proc>>> GameMode::targets(const QList<Proc> &procs) const
{
    QHash<qint64, const Proc *> byPid;
    QHash<qint64, QList<const Proc *>> children;
    for (const Proc &p : procs) {
        byPid.insert(p.pid, &p);
        children[p.ppid] << &p;
    }
    // devolve o nome que bateu (vazio = nenhum)
    auto matchName = [](const Proc &p, const QStringList &names) {
        for (const QString &n : {p.comm, p.argv0, p.exe})
            if (!n.isEmpty() && names.contains(n.toLower()))
                return n.toLower();
        return QString();
    };
    auto nameIn = [&](const Proc &p, const QStringList &names) { return !matchName(p, names).isEmpty(); };

    // protegidos: o launcher, o jogo (e tudo abaixo dele), gravação/métricas/áudio/sessão,
    // e os "pais" de cada um deles (pausar o terminal que abriu o launcher travaria a saída dele)
    QSet<qint64> prot;
    auto protectTree = [&](qint64 root) {
        QList<qint64> stack{root};
        while (!stack.isEmpty()) {
            const qint64 p = stack.takeLast();
            if (prot.contains(p))
                continue;
            prot.insert(p);
            for (const Proc *c : children.value(p))
                stack << c->pid;
        }
    };
    auto protectAncestors = [&](qint64 pid) {
        for (int guard = 0; pid > 1 && guard < 64; ++guard) {
            prot.insert(pid);
            const Proc *p = byPid.value(pid);
            if (!p)
                break;
            pid = p->ppid;
        }
    };
    const qint64 self = QCoreApplication::applicationPid();
    protectTree(self);
    protectAncestors(self);
    if (const qint64 g = m_runner->pid(); g > 0) {
        protectTree(g);
        protectAncestors(g);
        for (const Proc &p : procs)
            if (p.pgid == g)
                protectTree(p.pid);
    }
    for (const Proc &p : procs)
        if (nameIn(p, kProtected)) {
            protectTree(p.pid);
            protectAncestors(p.pid);
        }

    // alvos por categoria ligada
    QList<QPair<QString, QList<Proc>>> out;
    QSet<qint64> taken;
    auto addTree = [&](const QString &label, qint64 root) {
        QList<Proc> group;
        QList<qint64> stack{root};
        while (!stack.isEmpty()) {
            const qint64 pid = stack.takeLast();
            if (taken.contains(pid))
                continue;
            const Proc *p = byPid.value(pid);
            if (!p)
                continue;
            taken.insert(pid);
            // protegido (ex.: o terminal que abriu o OBS) fica rodando, mas os outros filhos dele entram
            if (!prot.contains(pid))
                group << *p;
            for (const Proc *c : children.value(pid))
                stack << c->pid;
        }
        if (!group.isEmpty()) {
            for (auto &o : out)
                if (o.first == label) {
                    o.second << group;
                    return;
                }
            out << qMakePair(label, group);
        }
    };
    for (const Category &c : categories()) {
        if (!setting(c.id, c.byDefault))
            continue;
        if (QLatin1String(c.id) == QLatin1String("apps")) {
            if (!m_machine)
                continue;
            for (const QVariant &v : m_machine->openApps()) {
                const QVariantMap a = v.toMap();
                const qint64 pg = a.value("id").toLongLong();
                for (const Proc &p : procs)
                    if (p.pgid == pg && (p.pid == pg || !byPid.contains(p.ppid) || byPid.value(p.ppid)->pgid != pg))
                        addTree(a.value("name").toString(), p.pid);
            }
            continue;
        }
        for (const Proc &p : procs)
            if (!taken.contains(p.pid) && nameIn(p, c.names)) {
                // só a raiz de cada programa (os filhos entram junto)
                const Proc *parent = byPid.value(p.ppid);
                if (parent && nameIn(*parent, c.names))
                    continue;
                addTree(niceName(matchName(p, c.names)), p.pid);
            }
    }
    return out;
}

QStringList GameMode::preview() const
{
    QStringList names;
    for (const auto &t : targets(scan()))
        names << t.first;
    names.removeDuplicates();
    return names;
}

void GameMode::activate()
{
    m_active = true;
    m_paused.clear();
    m_pausedNames.clear();
    const QString title = m_runner->game().value("title").toString();
    QStringList parts;

    if (setting("enabled", true)) {
        const bool close = QSettings().value("gameMode/action", "pause").toString() == "close";
        const auto groups = targets(scan());
        QStringList closedNames;
        for (const auto &g : groups) {
            if (close) {
                // fecha só a raiz de cada programa; os filhos fecham junto
                if (::kill(pid_t(g.second.first().pid), SIGTERM) == 0)
                    closedNames << g.first;
                continue;
            }
            bool any = false;
            for (const Proc &p : g.second) {
                if (p.state == 'T')
                    continue;  // já estava parado por outro motivo: não é nosso para retomar
                if (::kill(pid_t(p.pid), SIGSTOP) == 0) {
                    m_paused << Paused{p.pid, p.start, g.first};
                    any = true;
                }
            }
            if (any)
                m_pausedNames << g.first;
        }
        m_pausedNames.removeDuplicates();
        closedNames.removeDuplicates();
        if (close && !closedNames.isEmpty())
            parts << QStringLiteral("%1 fechado(s)").arg(closedNames.join(", "));
        else if (!m_paused.isEmpty())
            parts << QStringLiteral("%1 pausado(s)").arg(m_pausedNames.join(", "));
    }
    holdResources(title, m_runner->pid());
    if (!m_profileService.isEmpty())
        parts << QStringLiteral("energia em desempenho");
    if (m_gamemodePid)
        parts << QStringLiteral("GameMode ativo");
    m_summary = parts.isEmpty() ? QStringLiteral("Modo jogo ativo") : QStringLiteral("Modo jogo: ") + parts.join(" · ");
    saveState();
    emit changed();
}

void GameMode::resumeAll()
{
    // na ordem inversa: filhos antes dos pais não importa para SIGCONT, mas evita
    // que um pai retomado veja filhos ainda parados por muito tempo
    for (int i = m_paused.size() - 1; i >= 0; --i)
        if (sameProcess(m_paused[i].pid, m_paused[i].start))
            ::kill(pid_t(m_paused[i].pid), SIGCONT);
    m_paused.clear();
    m_pausedNames.clear();
    saveState();
    emit changed();
}

void GameMode::deactivate()
{
    resumeAll();
    releaseResources();
    m_active = false;
    m_summary.clear();
    QFile::remove(stateFile());
    emit changed();
}

void GameMode::saveState() const
{
    if (m_paused.isEmpty() && m_bannersBefore.isEmpty()) {
        QFile::remove(stateFile());
        return;
    }
    QJsonArray arr;
    for (const Paused &p : m_paused)
        arr << QJsonObject{{"pid", p.pid}, {"start", QString::number(p.start)}, {"name", p.name}};
    QJsonObject o{{"paused", arr}};
    if (!m_bannersBefore.isEmpty())
        o.insert("showBanners", m_bannersBefore);
    QDir().mkpath(QFileInfo(stateFile()).absolutePath());
    QFile f(stateFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

int GameMode::recover()
{
    QFile f(stateFile());
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    int n = 0;
    for (const QJsonValue &v : o.value("paused").toArray()) {
        const QJsonObject p = v.toObject();
        const qint64 pid = p.value("pid").toInteger();
        if (pid > 1 && sameProcess(pid, p.value("start").toString().toULongLong()) && ::kill(pid_t(pid), SIGCONT) == 0)
            ++n;
    }
    const QString banners = o.value("showBanners").toString();
    if (!banners.isEmpty() && !QStandardPaths::findExecutable("gsettings").isEmpty())
        QProcess::execute("gsettings", {"set", "org.gnome.desktop.notifications", "show-banners", banners});
    QFile::remove(stateFile());
    return n;
}

void GameMode::holdResources(const QString &title, qint64 gamePid)
{
    const QString reason = QStringLiteral("Jogando %1").arg(title);
    QDBusConnection sys = QDBusConnection::systemBus();
    QDBusConnection ses = QDBusConnection::sessionBus();

    // Perfil de energia "desempenho" (power-profiles-daemon / tuned-ppd). O pedido
    // some sozinho se o launcher fechar.
    if (setting("performance", true) && sys.isConnected()) {
        const QList<QPair<QString, QString>> svcs{{"org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles"},
                                                  {"net.hadess.PowerProfiles", "/net/hadess/PowerProfiles"}};
        for (const auto &s : svcs) {
            QDBusInterface ppd(s.first, s.second, s.first, sys);
            if (!ppd.isValid())
                continue;
            // sem o perfil "desempenho" (comum em VM) o pedido só falha
            QDBusReply<quint32> r = ppd.call("HoldProfile", "performance", reason, "console-launcher");
            if (r.isValid()) {
                m_profileCookie = r.value();
                m_profileService = s.first;
                m_profilePath = s.second;
            }
            break;
        }
    }
    if (ses.isConnected()) {
        // tela não apaga nem bloqueia
        QDBusInterface ss("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", ses);
        if (ss.isValid()) {
            QDBusReply<quint32> r = ss.call("Inhibit", "console-launcher", reason);
            if (r.isValid())
                m_idleCookie = r.value();
        }
        // não perturbe
        if (setting("dnd", true)) {
            if (qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains("GNOME", Qt::CaseInsensitive)
                && !QStandardPaths::findExecutable("gsettings").isEmpty()) {
                QProcess p;
                p.start("gsettings", {"get", "org.gnome.desktop.notifications", "show-banners"});
                if (p.waitForFinished(2000) && p.exitCode() == 0) {
                    const QString before = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
                    if (before == "true") {
                        m_bannersBefore = before;
                        QProcess::execute("gsettings", {"set", "org.gnome.desktop.notifications", "show-banners", "false"});
                    }
                }
            } else {
                QDBusInterface n("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                                 "org.freedesktop.Notifications", ses);
                if (n.isValid()) {
                    QDBusReply<quint32> r = n.call("Inhibit", "console-launcher", reason, QVariantMap());
                    if (r.isValid())
                        m_notifyCookie = r.value();
                }
            }
        }
        // GameMode da Feral: governador da CPU, prioridade e modo de desempenho da GPU
        if (setting("gamemode", true) && gamePid > 0 && ses.interface()
            && (ses.interface()->isServiceRegistered("com.feralinteractive.GameMode")
                || ses.interface()->activatableServiceNames().value().contains("com.feralinteractive.GameMode"))) {
            QDBusInterface gm("com.feralinteractive.GameMode", "/com/feralinteractive/GameMode",
                              "com.feralinteractive.GameMode", ses);
            QDBusReply<int> r = gm.call("RegisterGame", int(gamePid));
            if (r.isValid() && r.value() == 0)
                m_gamemodePid = gamePid;
        }
    }
}

void GameMode::releaseResources()
{
    QDBusConnection ses = QDBusConnection::sessionBus();
    if (!m_profileService.isEmpty()) {
        QDBusInterface ppd(m_profileService, m_profilePath, m_profileService, QDBusConnection::systemBus());
        ppd.call("ReleaseProfile", m_profileCookie);
        m_profileService.clear();
    }
    if (m_idleCookie) {
        QDBusInterface ss("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", ses);
        ss.call("UnInhibit", m_idleCookie);
        m_idleCookie = 0;
    }
    if (m_notifyCookie) {
        QDBusInterface n("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", ses);
        n.call("UnInhibit", m_notifyCookie);
        m_notifyCookie = 0;
    }
    if (!m_bannersBefore.isEmpty()) {
        QProcess::execute("gsettings", {"set", "org.gnome.desktop.notifications", "show-banners", m_bannersBefore});
        m_bannersBefore.clear();
    }
    if (m_gamemodePid) {
        QDBusInterface gm("com.feralinteractive.GameMode", "/com/feralinteractive/GameMode", "com.feralinteractive.GameMode", ses);
        gm.call("UnregisterGame", int(m_gamemodePid));
        m_gamemodePid = 0;
    }
}

// ------------------------------------------------------------------ interface
static QVariantMap mkRow(const QString &type, const QString &key, const QString &label, const QString &detail = {})
{
    return {{"type", type}, {"key", key}, {"label", label}, {"detail", detail}};
}

QVariantList GameMode::optionRows() const
{
    QVariantList rows;
    rows << mkRow("header", "gm_h", "Modo jogo");
    QVariantMap fs = mkRow("toggle", "gm:fullscreen", "Sempre abrir os jogos em tela cheia",
                           "Vale mesmo se um console ou jogo estiver com a tela cheia desligada");
    fs.insert("value", forceFullscreen());
    rows << fs;
    const bool on = setting("enabled", true);
    QVariantMap en = mkRow("toggle", "gm:enabled", "Liberar a máquina para o jogo",
                           "Pausa programas em segundo plano enquanto o jogo roda e retoma tudo ao fechar");
    en.insert("value", on);
    rows << en;
    if (on) {
        QVariantMap act = mkRow("choice", "gm:action", "O que fazer com eles");
        act.insert("options", QVariantList{QVariantMap{{"label", "Pausar e retomar depois"}, {"value", "pause"}},
                                           QVariantMap{{"label", "Fechar"}, {"value", "close"}}});
        act.insert("value", QSettings().value("gameMode/action", "pause").toString());
        act.insert("detail", QSettings().value("gameMode/action", "pause").toString() == "close"
                                 ? QStringLiteral("⚠ Trabalho não salvo nesses programas se perde")
                                 : QStringLiteral("Nada se perde: eles só ficam congelados durante o jogo"));
        rows << act;
        for (const Category &c : categories()) {
            QVariantMap t = mkRow("toggle", QStringLiteral("gm:") + c.id, QString::fromUtf8(c.label), QString::fromUtf8(c.detail));
            t.insert("value", setting(c.id, c.byDefault));
            rows << t;
        }
        const QStringList now = preview();
        rows << mkRow("info", "gm_now", QStringLiteral("Se um jogo abrisse agora"),
                      now.isEmpty() ? QStringLiteral("Nada para pausar") : QStringLiteral("Pausaria: ") + now.join(", "));
        rows << mkRow("info", "gm_never", QStringLiteral("Nunca mexe em"),
                      QStringLiteral("Emulador, launcher, gravação de tela (OBS, GPU Screen Recorder…), métricas (LinOSD, MangoHud), áudio e a área de trabalho"));
    }
    QVariantMap perf = mkRow("toggle", "gm:performance", "Energia no modo desempenho",
                             "Durante o jogo (power-profiles-daemon; nem toda máquina tem)");
    perf.insert("value", setting("performance", true));
    QVariantMap dnd = mkRow("toggle", "gm:dnd", "Não perturbe durante o jogo", "Notificações não aparecem por cima do jogo");
    dnd.insert("value", setting("dnd", true));
    const bool hasGm = !QStandardPaths::findExecutable("gamemoded").isEmpty();
    QVariantMap gm = mkRow("toggle", "gm:gamemode", "GameMode (Feral)",
                           hasGm ? QStringLiteral("CPU e GPU no máximo enquanto o jogo roda")
                                 : QStringLiteral("⚠ Não instalado: sudo apt install gamemode"));
    gm.insert("value", setting("gamemode", true));
    rows << perf << dnd << gm;
    return rows;
}

QVariantList GameMode::openRows() const
{
    QVariantList rows;
    if (m_pausedNames.isEmpty())
        return rows;
    rows << mkRow("header", "gm_open", "Pausados durante o jogo");
    rows << mkRow("info", "gm_list", m_pausedNames.join(", "), QStringLiteral("Voltam sozinhos quando o jogo fechar"));
    rows << mkRow("action", "gm:resume", "Retomar agora", "Eles voltam a rodar junto com o jogo");
    return rows;
}

void GameMode::setOption(const QString &key, const QString &value)
{
    if (key == "gm:resume") {
        resumeAll();
        emit message(QStringLiteral("Programas retomados."));
        return;
    }
    const QString k = key.mid(3);
    if (k == "action")
        QSettings().setValue("gameMode/action", value);
    else
        QSettings().setValue("gameMode/" + k, value == "on" || value == "true");
    emit changed();
}
