#include "systemcontrol.h"

#include <QDir>
#include <QFile>
#include <QHostInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>

#include <csignal>
#include <sys/types.h>
#include <unistd.h>

namespace {

QVariantMap row(const QString &type, const QString &key, const QString &label, const QString &detail = QString())
{
    return {{"type", type}, {"key", key}, {"label", label}, {"detail", detail}};
}

// nmcli -t: campos separados por ":", com "\:" e "\\" escapados
QStringList splitTerse(const QString &line)
{
    QStringList out;
    QString cur;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            cur += line[++i];
        } else if (c == ':') {
            out << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    out << cur;
    return out;
}

QString signalBars(int s)
{
    return s >= 75 ? "▂▄▆█" : s >= 50 ? "▂▄▆" : s >= 25 ? "▂▄" : "▂";
}

// Todos os processos de um grupo (o app e o que ele abriu) continuam vivos?
bool groupAlive(qint64 pgid)
{
    const QDir proc("/proc");
    for (const QString &pid : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!pid.front().isDigit())
            continue;
        QFile f("/proc/" + pid + "/stat");
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray stat = f.readAll();
        const int close = stat.lastIndexOf(')');  // o nome do processo pode ter espaços
        if (close < 0)
            continue;
        const QList<QByteArray> fields = stat.mid(close + 2).split(' ');
        // fields[0] = estado, [2] = grupo de processos
        if (fields.size() > 2 && fields[2].toLongLong() == pgid && fields[0] != "Z")
            return true;
    }
    return false;
}

}  // namespace

SystemControl::SystemControl(QObject *parent) : QObject(parent)
{
    m_audio = have("pactl") ? "pactl" : have("wpctl") ? "wpctl" : "";
    m_display = detectDisplayBackend();

    for (const char *app : {"gnome-control-center", "systemsettings", "xfce4-settings-manager",
                            "cinnamon-settings", "mate-control-center", "lxqt-config"})
        if (have(app)) {
            m_settingsApp = QString::fromLatin1(app);
            break;
        }

    m_btScanTimer.setInterval(3000);
    connect(&m_btScanTimer, &QTimer::timeout, this, &SystemControl::refreshBluetooth);

    m_appTimer.setInterval(2000);
    connect(&m_appTimer, &QTimer::timeout, this, &SystemControl::pruneApps);
    m_appTimer.start();

    m_revertTimer.setSingleShot(true);
    m_revertTimer.setInterval(15000);
    connect(&m_revertTimer, &QTimer::timeout, this, [this] { keepDisplay(false); });
}

bool SystemControl::have(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

void SystemControl::run(const QString &program, const QStringList &args, Done done, int timeoutMs)
{
    auto *p = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LC_ALL", "C");  // saída em inglês, previsível para ler
    env.insert("LANG", "C");
    p->setProcessEnvironment(env);
    p->setProcessChannelMode(QProcess::MergedChannels);
    p->setStandardInputFile(QProcess::nullDevice());
    auto *timer = new QTimer(p);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, p, [p] { p->kill(); });
    connect(p, &QProcess::finished, this, [p, done](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(p->readAll());
        p->deleteLater();
        if (done)
            done(st == QProcess::NormalExit ? code : -1, out);
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        if (done)
            done(-2, QString());
    });
    p->start(program, args);
    timer->start(timeoutMs);
}

QString SystemControl::userName() const
{
    const QString u = qEnvironmentVariable("USER");
    return u.isEmpty() ? QDir::home().dirName() : u;
}

QString SystemControl::hostName() const { return QHostInfo::localHostName(); }

QString SystemControl::sessionType() const
{
    const QString t = qEnvironmentVariable("XDG_SESSION_TYPE");
    return t.isEmpty() ? (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ? "wayland" : "x11") : t;
}

QVariantList SystemControl::rows(const QString &section) const
{
    if (section == "audio")
        return audioRows();
    if (section == "bluetooth")
        return bluetoothRows();
    if (section == "network")
        return networkRows();
    if (section == "display")
        return displayRows();
    if (section == "about")
        return aboutRows();
    return {};
}

void SystemControl::refresh(const QString &section)
{
    if (section == "audio")
        refreshAudio();
    else if (section == "bluetooth")
        refreshBluetooth();
    else if (section == "network")
        refreshNetwork();
    else if (section == "display")
        refreshDisplay();
    else if (section == "about")
        refreshAbout();
}

void SystemControl::act(const QString &section, const QString &key, const QString &value)
{
    if (section == "audio")
        audioAct(key, value);
    else if (section == "bluetooth")
        bluetoothAct(key, value);
    else if (section == "network")
        networkAct(key, value);
    else if (section == "display")
        displayAct(key, value);
}

// ============================================================== som

void SystemControl::refreshAudio()
{
    if (m_audio == "pactl") {
        run("pactl", {"get-sink-volume", "@DEFAULT_SINK@"}, [this](int code, const QString &out) {
            const auto m = QRegularExpression(R"((\d+)%)").match(out);
            m_volume = (code == 0 && m.hasMatch()) ? m.captured(1).toInt() : -1;
            emit changed("audio");
        });
        run("pactl", {"get-sink-mute", "@DEFAULT_SINK@"}, [this](int code, const QString &out) {
            m_muted = code == 0 && out.contains("yes");
            emit changed("audio");
        });
        run("pactl", {"get-default-sink"}, [this](int code, const QString &out) {
            if (code == 0)
                m_defaultSink = out.trimmed();
            emit changed("audio");
        });
        run("pactl", {"list", "sinks"}, [this](int code, const QString &out) {
            if (code != 0)
                return;
            QList<Sink> sinks;
            for (const QString &line : out.split('\n')) {
                const QString t = line.trimmed();
                if (t.startsWith("Sink #"))
                    sinks << Sink{t.mid(6), QString(), QString()};
                else if (!sinks.isEmpty() && t.startsWith("Name: "))
                    sinks.last().name = t.mid(6);
                else if (!sinks.isEmpty() && t.startsWith("Description: "))
                    sinks.last().description = t.mid(13);
            }
            for (Sink &s : sinks)
                s.id = s.name;  // pactl troca a saída padrão pelo nome
            m_sinks = sinks;
            emit changed("audio");
        });
    } else if (m_audio == "wpctl") {
        run("wpctl", {"get-volume", "@DEFAULT_AUDIO_SINK@"}, [this](int code, const QString &out) {
            const auto m = QRegularExpression(R"(Volume:\s*([\d.]+))").match(out);
            m_volume = (code == 0 && m.hasMatch()) ? qRound(m.captured(1).toDouble() * 100) : -1;
            m_muted = out.contains("MUTED");
            emit changed("audio");
        });
        run("wpctl", {"status"}, [this](int code, const QString &out) {
            if (code != 0)
                return;
            // Audio
            //  ├─ Sinks:
            //  │  *   46. Built-in Audio Analog Stereo   [vol: 0.40]
            QList<Sink> sinks;
            bool inSinks = false;
            static const QRegularExpression item(R"(^[\s│]*(\*)?\s*(\d+)\.\s+(.+?)\s*(\[vol:.*)?$)");
            for (const QString &line : out.split('\n')) {
                if (line.contains("Sinks:")) {
                    if (!sinks.isEmpty() || inSinks)
                        break;  // só a primeira lista (Audio)
                    inSinks = true;
                    continue;
                }
                if (!inSinks)
                    continue;
                if (line.contains(QStringLiteral("├─")) || line.contains(QStringLiteral("└─")) || line.trimmed().isEmpty())
                    break;
                const auto m = item.match(line);
                if (m.hasMatch()) {
                    sinks << Sink{m.captured(2), m.captured(2), m.captured(3)};
                    if (!m.captured(1).isEmpty())
                        m_defaultSink = m.captured(2);
                }
            }
            m_sinks = sinks;
            emit changed("audio");
        });
    }
}

QVariantList SystemControl::audioRows() const
{
    QVariantList rows;
    if (m_audio.isEmpty()) {
        rows << row("info", "a_none", "Controle de som indisponível",
                    "⚠ Instale pulseaudio-utils (pactl) ou wireplumber (wpctl)");
        return rows;
    }
    rows << row("header", "a_h1", "Volume");
    QVariantMap vol = row("slider", "volume", "Volume",
                          m_volume < 0 ? "Lendo…" : QStringLiteral("%1%").arg(m_volume));
    vol.insert("value", qMax(0, m_volume));
    rows << vol;
    QVariantMap mute = row("toggle", "mute", "Sem som");
    mute.insert("value", m_muted);
    rows << mute;

    rows << row("header", "a_h2", "Saída de áudio");
    if (m_sinks.size() > 1) {
        QVariantList opts;
        for (const Sink &s : m_sinks)
            opts << QVariantMap{{"label", s.description.isEmpty() ? s.name : s.description}, {"value", s.id}};
        QVariantMap r = row("choice", "sink", "Tocar em", "HDMI, fone de ouvido, caixa de som…");
        r.insert("options", opts);
        r.insert("value", m_defaultSink);
        rows << r;
    } else {
        rows << row("info", "a_sink", m_sinks.isEmpty() ? "Nenhuma saída de áudio encontrada"
                                                        : m_sinks.first().description,
                    m_sinks.isEmpty() ? QString() : "Única saída disponível");
    }
    return rows;
}

void SystemControl::audioAct(const QString &key, const QString &value)
{
    auto reload = [this](int, const QString &) { refreshAudio(); };
    if (key == "volume") {
        const int v = qBound(0, value.toInt(), 150);
        m_volume = v;
        emit changed("audio");
        if (m_audio == "pactl")
            run("pactl", {"set-sink-volume", "@DEFAULT_SINK@", QString::number(v) + "%"}, reload);
        else
            run("wpctl", {"set-volume", "-l", "1.5", "@DEFAULT_AUDIO_SINK@", QString::number(v / 100.0, 'f', 2)}, reload);
    } else if (key == "mute") {
        if (m_audio == "pactl")
            run("pactl", {"set-sink-mute", "@DEFAULT_SINK@", "toggle"}, reload);
        else
            run("wpctl", {"set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"}, reload);
    } else if (key == "sink") {
        if (m_audio == "pactl")
            run("pactl", {"set-default-sink", value}, reload);
        else
            run("wpctl", {"set-default", value}, reload);
    }
}

// ============================================================== Bluetooth

void SystemControl::refreshBluetooth()
{
    if (!have("bluetoothctl")) {
        m_btLoaded = true;
        m_btAvailable = false;
        m_btError = "⚠ bluetoothctl não encontrado (sudo apt install bluez)";
        emit changed("bluetooth");
        return;
    }
    run("bluetoothctl", {"show"}, [this](int code, const QString &out) {
        m_btLoaded = true;
        if (code != 0 || out.contains("No default controller") || !out.contains("Controller")) {
            m_btAvailable = false;
            m_btError = out.contains("No default controller") || code == 0
                            ? "Nenhum adaptador Bluetooth neste computador"
                            : "⚠ Serviço Bluetooth parado (sudo systemctl enable --now bluetooth)";
            emit changed("bluetooth");
            return;
        }
        m_btAvailable = true;
        m_btPowered = out.contains("Powered: yes");
        emit changed("bluetooth");
        if (!m_btPowered)
            return;
        run("bluetoothctl", {"devices"}, [this](int code, const QString &out) {
            if (code != 0)
                return;
            QHash<QString, BtDev> old;
            for (const BtDev &d : std::as_const(m_btDevices))
                old.insert(d.mac, d);
            QList<BtDev> list;
            static const QRegularExpression re(R"(^Device\s+([0-9A-F:]{17})\s+(.*)$)");
            for (const QString &line : out.split('\n')) {
                const auto m = re.match(line.trimmed());
                if (!m.hasMatch())
                    continue;
                BtDev d = old.value(m.captured(1));
                d.mac = m.captured(1);
                d.name = m.captured(2).trimmed();
                list << d;
            }
            m_btDevices = list.mid(0, 40);
            emit changed("bluetooth");
            for (const BtDev &d : std::as_const(m_btDevices)) {
                const QString mac = d.mac;
                run("bluetoothctl", {"info", mac}, [this, mac](int code, const QString &out) {
                    if (code != 0)
                        return;
                    for (BtDev &x : m_btDevices) {
                        if (x.mac != mac)
                            continue;
                        x.paired = out.contains("Paired: yes");
                        x.connected = out.contains("Connected: yes");
                        const auto ic = QRegularExpression(R"(Icon:\s*(\S+))").match(out);
                        x.icon = ic.hasMatch() ? ic.captured(1) : QString();
                        const auto al = QRegularExpression(R"(Alias:\s*(.+))").match(out);
                        if (al.hasMatch())
                            x.name = al.captured(1).trimmed();
                    }
                    emit changed("bluetooth");
                });
            }
        });
    });
}

QVariantList SystemControl::bluetoothRows() const
{
    QVariantList rows;
    if (!m_btLoaded)
        return {row("info", "b_load", "Lendo Bluetooth…")};
    if (!m_btAvailable)
        return {row("info", "b_none", "Bluetooth indisponível", m_btError)};

    QVariantMap power = row("toggle", "power", "Bluetooth", m_btPowered ? "Ligado" : "Desligado");
    power.insert("value", m_btPowered);
    rows << power;
    if (!m_btPowered)
        return rows;

    rows << row("action", "scan",
                m_btScanning ? "Procurando dispositivos…" : "Procurar dispositivos",
                "Antes, coloque o controle em modo de pareamento (Xbox: botão de parear · "
                "DualShock/DualSense: Share/Create + PS · Switch Pro: botão de sincronizar)");

    auto deviceKind = [](const BtDev &d) {
        if (d.icon.contains("gaming"))
            return QStringLiteral("Controle");
        if (d.icon.contains("audio") || d.icon.contains("headset") || d.icon.contains("headphone"))
            return QStringLiteral("Áudio");
        if (d.icon.contains("keyboard"))
            return QStringLiteral("Teclado");
        if (d.icon.contains("mouse"))
            return QStringLiteral("Mouse");
        return QString();
    };
    auto item = [&](const BtDev &d) {
        QString detail;
        if (m_btBusy == d.mac)
            detail = "Aguarde…";
        else if (d.connected)
            detail = "Conectado · A desconecta";
        else if (d.paired)
            detail = "Pareado · A conecta";
        else
            detail = "A pareia e conecta";
        const QString kind = deviceKind(d);
        if (!kind.isEmpty())
            detail = kind + " · " + detail;
        QVariantMap r = row("item", "dev:" + d.mac, d.name, detail);
        r.insert("secondary", d.paired ? "Esquecer" : "");
        r.insert("active", d.connected);
        return r;
    };

    bool any = false;
    for (const BtDev &d : m_btDevices)
        if (d.paired) {
            if (!any)
                rows << row("header", "b_h1", "Meus dispositivos");
            any = true;
            rows << item(d);
        }
    any = false;
    static const QRegularExpression macLike(R"(^([0-9A-F]{2}[-:]){5}[0-9A-F]{2}$)");
    for (const BtDev &d : m_btDevices)
        if (!d.paired && !macLike.match(d.name).hasMatch()) {  // sem nome = barulho de vizinhos
            if (!any)
                rows << row("header", "b_h2", "Encontrados");
            any = true;
            rows << item(d);
        }
    if (m_btDevices.isEmpty())
        rows << row("info", "b_empty", "Nenhum dispositivo ainda", "Use \"Procurar dispositivos\"");
    return rows;
}

void SystemControl::bluetoothAct(const QString &key, const QString &value)
{
    if (key == "power") {
        run("bluetoothctl", {"power", value == "on" ? "on" : "off"}, [this](int code, const QString &out) {
            if (code != 0 || out.contains("Failed"))
                emit message(out.contains("rfkill") || out.contains("Blocked")
                                 ? "Bluetooth bloqueado. Rode: rfkill unblock bluetooth"
                                 : "Não foi possível mudar o Bluetooth.");
            refreshBluetooth();
        });
        return;
    }
    if (key == "scan") {
        if (m_btScanning)
            return;
        m_btScanning = true;
        m_btScanTimer.start();
        emit changed("bluetooth");
        run("bluetoothctl", {"--timeout", "15", "scan", "on"}, [this](int, const QString &) {
            m_btScanning = false;
            m_btScanTimer.stop();
            refreshBluetooth();
        }, 25000);
        return;
    }
    if (!key.startsWith("dev:"))
        return;
    const QString mac = key.mid(4);
    BtDev dev;
    for (const BtDev &d : std::as_const(m_btDevices))
        if (d.mac == mac)
            dev = d;
    if (dev.mac.isEmpty() || !m_btBusy.isEmpty())
        return;
    m_btBusy = mac;
    emit changed("bluetooth");
    auto finish = [this, name = dev.name](bool ok, const QString &okText, const QString &failText) {
        m_btBusy.clear();
        emit message(ok ? okText.arg(name) : failText.arg(name));
        refreshBluetooth();
    };

    if (value == "secondary") {
        if (!dev.paired) {
            m_btBusy.clear();
            emit changed("bluetooth");
            return;
        }
        run("bluetoothctl", {"remove", mac}, [finish](int code, const QString &) {
            finish(code == 0, "%1 foi esquecido.", "Não foi possível esquecer %1.");
        });
    } else if (dev.connected) {
        run("bluetoothctl", {"disconnect", mac}, [finish](int code, const QString &) {
            finish(code == 0, "%1 desconectado.", "Não foi possível desconectar %1.");
        });
    } else if (dev.paired) {
        run("bluetoothctl", {"connect", mac}, [finish](int code, const QString &out) {
            finish(code == 0 && !out.contains("Failed"), "%1 conectado.",
                   "Não conectou %1. Ligue o dispositivo e tente de novo.");
        }, 30000);
    } else {
        // parear -> confiar (reconecta sozinho depois) -> conectar
        run("bluetoothctl", {"pair", mac}, [this, mac, finish](int code, const QString &out) {
            if (code != 0 || out.contains("Failed")) {
                finish(false, "", "Não pareou %1. Coloque-o em modo de pareamento e tente de novo.");
                return;
            }
            run("bluetoothctl", {"trust", mac}, [this, mac, finish](int, const QString &) {
                run("bluetoothctl", {"connect", mac}, [finish](int code, const QString &out) {
                    finish(code == 0 && !out.contains("Failed"), "%1 pareado e conectado.",
                           "%1 pareado, mas não conectou. Tente conectar de novo.");
                }, 30000);
            });
        }, 40000);
    }
}

// ============================================================== rede

void SystemControl::refreshNetwork(bool rescan)
{
    if (!have("nmcli")) {
        m_netLoaded = true;
        m_nmAvailable = false;
        m_netError = "⚠ NetworkManager não encontrado (sudo apt install network-manager)";
        emit changed("network");
        return;
    }
    run("nmcli", {"-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device", "status"}, [this, rescan](int code, const QString &out) {
        m_netLoaded = true;
        if (code != 0) {
            m_nmAvailable = false;
            m_netError = "⚠ O NetworkManager não está em execução";
            emit changed("network");
            return;
        }
        m_nmAvailable = true;
        QList<NetDev> devs;
        for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
            const QStringList f = splitTerse(line);
            if (f.size() >= 4 && (f[1] == "ethernet" || f[1] == "wifi"))
                devs << NetDev{f[0], f[1], f[2], f[3]};
        }
        m_netDevs = devs;
        emit changed("network");

        const bool hasWifi = std::any_of(devs.begin(), devs.end(), [](const NetDev &d) { return d.type == "wifi"; });
        if (!hasWifi)
            return;
        run("nmcli", {"-t", "radio", "wifi"}, [this](int, const QString &out) {
            m_wifiEnabled = out.trimmed() == "enabled";
            emit changed("network");
        });
        run("nmcli", {"-t", "-f", "NAME,TYPE", "connection", "show"}, [this](int code, const QString &out) {
            if (code != 0)
                return;
            QStringList known;
            for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
                const QStringList f = splitTerse(line);
                if (f.size() >= 2 && f[1] == "802-11-wireless")
                    known << f[0];
            }
            m_knownWifi = known;
            emit changed("network");
        });
        if (rescan) {
            m_wifiScanning = true;
            emit changed("network");
        }
        run("nmcli", {"-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", rescan ? "yes" : "auto"},
            [this](int code, const QString &out) {
                m_wifiScanning = false;
                if (code != 0) {
                    emit changed("network");
                    return;
                }
                QHash<QString, Wifi> best;
                for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
                    const QStringList f = splitTerse(line);
                    if (f.size() < 4 || f[1].isEmpty())
                        continue;  // rede oculta
                    Wifi w{f[1], f[3] == "--" ? QString() : f[3], f[2].toInt(), f[0].trimmed() == "*"};
                    const Wifi cur = best.value(w.ssid);
                    if (!best.contains(w.ssid) || w.signal > cur.signal || w.inUse) {
                        if (cur.inUse)
                            w.inUse = true;
                        best.insert(w.ssid, w);
                    }
                }
                QList<Wifi> list = best.values();
                std::sort(list.begin(), list.end(), [](const Wifi &a, const Wifi &b) {
                    if (a.inUse != b.inUse)
                        return a.inUse;
                    return a.signal > b.signal;
                });
                m_wifis = list;
                emit changed("network");
            }, 30000);
    });
}

QVariantList SystemControl::networkRows() const
{
    QVariantList rows;
    if (!m_netLoaded)
        return {row("info", "n_load", "Lendo a rede…")};
    if (!m_nmAvailable)
        return {row("info", "n_none", "Rede indisponível", m_netError)};

    rows << row("header", "n_h1", "Conexão");
    bool hasWifi = false;
    for (const NetDev &d : m_netDevs) {
        const bool up = d.state.startsWith("connected");
        QString state = up ? "Conectado · " + d.connection
                        : d.state == "unavailable" ? "Sem cabo / indisponível"
                        : d.state.startsWith("connecting") ? "Conectando…"
                                                           : "Desconectado";
        rows << row("info", "dev:" + d.device, (d.type == "wifi" ? "Wi‑Fi (" : "Cabo (") + d.device + ")", state);
        if (d.type == "wifi") {
            hasWifi = true;
            if (up)
                rows << row("action", "disconnect:" + d.device, "Desconectar de " + d.connection);
        }
    }
    if (m_netDevs.isEmpty())
        rows << row("info", "n_nodev", "Nenhuma placa de rede encontrada");
    if (!hasWifi) {
        rows << row("info", "n_nowifi", "Este computador não tem Wi‑Fi", "Use o cabo de rede");
        return rows;
    }

    rows << row("header", "n_h2", "Wi‑Fi");
    QVariantMap radio = row("toggle", "wifi", "Wi‑Fi", m_wifiEnabled ? "Ligado" : "Desligado");
    radio.insert("value", m_wifiEnabled);
    rows << radio;
    if (!m_wifiEnabled)
        return rows;
    rows << row("action", "rescan", m_wifiScanning ? "Procurando redes…" : "Procurar redes");
    for (const Wifi &w : m_wifis) {
        QStringList parts{signalBars(w.signal) + QStringLiteral(" %1%").arg(w.signal)};
        parts << (w.security.isEmpty() ? "aberta" : "protegida");
        if (w.inUse)
            parts << "conectada";
        else if (m_knownWifi.contains(w.ssid))
            parts << "salva";
        if (m_netBusy == w.ssid)
            parts = QStringList{"Conectando…"};
        QVariantMap r = row("item", "net:" + w.ssid, w.ssid, parts.join(" · "));
        r.insert("secondary", m_knownWifi.contains(w.ssid) ? "Esquecer" : "");
        r.insert("active", w.inUse);
        rows << r;
    }
    if (m_wifis.isEmpty())
        rows << row("info", "n_empty", m_wifiScanning ? "Procurando…" : "Nenhuma rede encontrada");
    return rows;
}

void SystemControl::networkAct(const QString &key, const QString &value)
{
    auto reload = [this](int, const QString &) { refreshNetwork(); };
    if (key == "wifi") {
        run("nmcli", {"radio", "wifi", value == "on" ? "on" : "off"}, reload);
    } else if (key == "rescan") {
        refreshNetwork(true);
    } else if (key.startsWith("disconnect:")) {
        run("nmcli", {"device", "disconnect", key.mid(11)}, reload);
    } else if (key.startsWith("net:")) {
        const QString ssid = key.mid(4);
        Wifi w;
        for (const Wifi &x : std::as_const(m_wifis))
            if (x.ssid == ssid)
                w = x;
        if (value == "secondary") {
            if (m_knownWifi.contains(ssid))
                run("nmcli", {"connection", "delete", "id", ssid}, [this, ssid](int code, const QString &) {
                    emit message(code == 0 ? QStringLiteral("Rede %1 esquecida.").arg(ssid)
                                           : QStringLiteral("Não foi possível esquecer %1.").arg(ssid));
                    refreshNetwork();
                });
            return;
        }
        if (w.inUse)
            return;
        if (!w.security.isEmpty() && !m_knownWifi.contains(ssid))
            emit passwordNeeded(ssid);
        else
            connectWifi(ssid, QString());
    }
}

void SystemControl::connectWifi(const QString &ssid, const QString &password)
{
    if (!m_netBusy.isEmpty())
        return;
    m_netBusy = ssid;
    emit changed("network");
    QStringList args{"device", "wifi", "connect", ssid};
    if (!password.isEmpty())
        args << "password" << password;
    run("nmcli", args, [this, ssid](int code, const QString &out) {
        m_netBusy.clear();
        if (code == 0) {
            emit message(QStringLiteral("Conectado a %1.").arg(ssid));
        } else if (out.contains("Secrets were required") || out.contains("psk") || out.contains("password")) {
            emit message(QStringLiteral("Senha incorreta para %1.").arg(ssid));
            // não deixa uma conexão salva com a senha errada
            run("nmcli", {"connection", "delete", "id", ssid}, nullptr);
            emit passwordNeeded(ssid);
        } else {
            const QString err = out.section('\n', -1).trimmed();
            emit message(QStringLiteral("Não conectou a %1%2").arg(ssid, err.isEmpty() ? "." : ": " + err.left(120)));
        }
        refreshNetwork();
    }, 60000);
}

// ============================================================== este computador

namespace {

QString readFirst(const QString &file, const QString &key)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    // arquivos de /proc dizem ter tamanho 0: lê tudo de uma vez em vez de testar atEnd()
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(key))
            return line.mid(key.size()).trimmed().remove(QRegularExpression("^[:=]\\s*")).remove('"');
    }
    return {};
}

QString gib(qint64 kb)
{
    return QStringLiteral("%1 GB").arg(QLocale(QLocale::Portuguese, QLocale::Brazil).toString(kb / 1048576.0, 'f', 1));
}

}  // namespace

void SystemControl::refreshAbout()
{
    if (m_gpu.isEmpty() && !have("lspci"))
        m_gpu = QStringLiteral("— (instale pciutils para ver)");
    if (m_gpu.isEmpty()) {
        run("lspci", {"-mm"}, [this](int code, const QString &out) {
            QStringList gpus;
            // 00:02.0 "VGA compatible controller" "Intel Corporation" "UHD Graphics 620" ...
            static const QRegularExpression re(R"re("(?:VGA compatible controller|3D controller|Display controller)"\s+"([^"]*)"\s+"([^"]*)")re");
            for (const QString &line : out.split('\n')) {
                const auto m = re.match(line);
                if (m.hasMatch())
                    gpus << (m.captured(1).section(' ', 0, 1).remove(" Corporation").remove(",") + " " + m.captured(2)).simplified();
            }
            m_gpu = code == 0 && !gpus.isEmpty() ? gpus.join(" + ") : QStringLiteral("—");
            emit changed("about");
        });
    }
    emit changed("about");
}

QVariantList SystemControl::aboutRows() const
{
    QVariantList rows;
    rows << row("header", "s_h1", "Armazenamento");
    QSet<QString> seen;
    QList<QStorageInfo> vols{QStorageInfo::root(), QStorageInfo(QDir::homePath())};
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
        const QString r = v.rootPath();
        if (r.startsWith("/media/") || r.startsWith("/run/media/") || (r.startsWith("/mnt/") && r.count('/') == 2))
            vols << v;
    }
    for (const QStorageInfo &v : vols) {
        if (!v.isValid() || v.bytesTotal() <= 0 || seen.contains(v.rootPath()))
            continue;
        seen.insert(v.rootPath());
        const qint64 total = v.bytesTotal(), free = v.bytesAvailable();
        const QString label = v.rootPath() == "/" ? QStringLiteral("Sistema (/)")
                              : v.rootPath() == QStorageInfo(QDir::homePath()).rootPath() && v.rootPath() != "/"
                                  ? QStringLiteral("Pasta pessoal (%1)").arg(v.rootPath())
                                  : (v.displayName().isEmpty() ? v.rootPath() : v.displayName());
        QVariantMap m = row("meter", "disk:" + v.rootPath(), label,
                            QStringLiteral("%1 livres de %2 · %3").arg(gib(free / 1024), gib(total / 1024),
                                                                       QString::fromUtf8(v.fileSystemType())));
        m.insert("value", int(100.0 * (total - free) / total));
        rows << m;
    }

    rows << row("header", "s_h2", "Memória");
    const qint64 memTotal = readFirst("/proc/meminfo", "MemTotal").section(' ', 0, 0).toLongLong();
    const qint64 memAvail = readFirst("/proc/meminfo", "MemAvailable").section(' ', 0, 0).toLongLong();
    if (memTotal > 0) {
        QVariantMap m = row("meter", "mem", "Memória RAM",
                            QStringLiteral("%1 em uso de %2 · %3 livres").arg(gib(memTotal - memAvail), gib(memTotal), gib(memAvail)));
        m.insert("value", int(100.0 * (memTotal - memAvail) / memTotal));
        rows << m;
    }

    rows << row("header", "s_h3", "Computador");
    QString os = readFirst("/etc/os-release", "PRETTY_NAME=");
    rows << row("info", "os", "Sistema", os.isEmpty() ? QSysInfo::prettyProductName() : os);
    rows << row("info", "kernel", "Kernel", QSysInfo::kernelVersion() + " · " + QSysInfo::currentCpuArchitecture());
    rows << row("info", "cpu", "Processador",
                QStringLiteral("%1 · %2 threads").arg(readFirst("/proc/cpuinfo", "model name").isEmpty() ? QStringLiteral("—")
                                                                                                        : readFirst("/proc/cpuinfo", "model name"))
                    .arg(QThread::idealThreadCount()));
    rows << row("info", "gpu", "Placa de vídeo", m_gpu.isEmpty() ? QStringLiteral("Lendo…") : m_gpu);
    QFile up("/proc/uptime");
    if (up.open(QIODevice::ReadOnly)) {
        const qint64 secs = qint64(QString::fromLatin1(up.readAll()).section(' ', 0, 0).toDouble());
        rows << row("info", "uptime", "Ligado há",
                    secs >= 86400 ? QStringLiteral("%1 d %2 h").arg(secs / 86400).arg((secs % 86400) / 3600)
                                  : QStringLiteral("%1 h %2 min").arg(secs / 3600).arg((secs % 3600) / 60));
    }
    rows << row("info", "host", "Nome do computador", hostName() + " · usuário " + userName());
    rows << row("info", "session", "Sessão gráfica",
                QStringLiteral("%1 · %2").arg(sessionType(), qEnvironmentVariable("XDG_CURRENT_DESKTOP", "—")));
    return rows;
}

// ============================================================== energia, apps

void SystemControl::power(const QString &what)
{
    if (what == "suspend") {
        QProcess::startDetached("systemctl", {"suspend"});
    } else if (what == "reboot") {
        QProcess::startDetached("systemctl", {"reboot"});
    } else if (what == "poweroff") {
        QProcess::startDetached("systemctl", {"poweroff"});
    } else if (what == "logout") {
        const QString id = qEnvironmentVariable("XDG_SESSION_ID");
        QProcess::startDetached("loginctl", {"terminate-session", id.isEmpty() ? QStringLiteral("self") : id});
    }
}

bool SystemControl::openSystemSettings()
{
    if (m_settingsApp.isEmpty())
        return false;
    return QProcess::startDetached(m_settingsApp, {});
}

void SystemControl::trackApp(qint64 pgid, const QString &name, const QString &icon)
{
    if (pgid <= 0)
        return;
    m_apps.erase(std::remove_if(m_apps.begin(), m_apps.end(), [&](const OpenApp &a) { return a.id == pgid; }),
                 m_apps.end());
    m_apps.prepend(OpenApp{pgid, name, icon});
    emit openAppsChanged();
}

void SystemControl::pruneApps()
{
    const int before = m_apps.size();
    m_apps.erase(std::remove_if(m_apps.begin(), m_apps.end(), [](const OpenApp &a) { return !groupAlive(a.id); }),
                 m_apps.end());
    if (m_apps.size() != before)
        emit openAppsChanged();
}

QVariantList SystemControl::openApps() const
{
    QVariantList out;
    for (const OpenApp &a : m_apps)
        out << QVariantMap{{"id", a.id}, {"name", a.name}, {"icon", a.icon}};
    return out;
}

void SystemControl::closeApp(qint64 id)
{
    ::kill(-pid_t(id), SIGTERM);
    QTimer::singleShot(5000, this, [this, id] {
        if (groupAlive(id))
            ::kill(-pid_t(id), SIGKILL);
        pruneApps();
    });
    QTimer::singleShot(800, this, &SystemControl::pruneApps);
}
