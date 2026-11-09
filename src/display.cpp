// Resolução da tela, por ambiente:
//   GNOME (Wayland/X11): D-Bus org.gnome.Mutter.DisplayConfig
//   KDE Plasma: kscreen-doctor     wlroots (Sway, labwc…): wlr-randr     X11: xrandr
// Toda mudança precisa ser confirmada em 15 s; sem confirmação, volta sozinha.
#include "systemcontrol.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {

const char *kMutterService = "org.gnome.Mutter.DisplayConfig";
const char *kMutterPath = "/org/gnome/Mutter/DisplayConfig";

// Tipos usados em ApplyMonitorsConfig: a(iiduba(ssa{sv}))
struct MonitorAssign {
    QString connector, mode;
    QVariantMap props;
};
struct LogicalApply {
    int x = 0, y = 0;
    double scale = 1;
    uint transform = 0;
    bool primary = false;
    QList<MonitorAssign> monitors;
};

}  // namespace

Q_DECLARE_METATYPE(MonitorAssign)
Q_DECLARE_METATYPE(LogicalApply)

QDBusArgument &operator<<(QDBusArgument &a, const MonitorAssign &m)
{
    a.beginStructure();
    a << m.connector << m.mode << m.props;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, MonitorAssign &m)
{
    a.beginStructure();
    a >> m.connector >> m.mode >> m.props;
    a.endStructure();
    return a;
}
QDBusArgument &operator<<(QDBusArgument &a, const LogicalApply &l)
{
    a.beginStructure();
    a << l.x << l.y << l.scale << l.transform << l.primary << l.monitors;
    a.endStructure();
    return a;
}
const QDBusArgument &operator>>(const QDBusArgument &a, LogicalApply &l)
{
    a.beginStructure();
    a >> l.x >> l.y >> l.scale >> l.transform >> l.primary >> l.monitors;
    a.endStructure();
    return a;
}

namespace {

QString modeLabel(const SystemControl::Mode &m) { return QStringLiteral("%1 × %2").arg(m.w).arg(m.h); }
QString resKey(int w, int h) { return QStringLiteral("%1x%2").arg(w).arg(h); }
QString hzKey(double r) { return QString::number(qRound(r * 100) / 100.0, 'f', 2); }

bool mutterRunning()
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    return bus && bus->isServiceRegistered(QString::fromLatin1(kMutterService));
}

}  // namespace

QString SystemControl::detectDisplayBackend() const
{
    auto has = [](const char *p) { return !QStandardPaths::findExecutable(p).isEmpty(); };
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    const bool wayland = sessionType() == "wayland";
    if (desktop.contains("gnome") && mutterRunning())
        return "mutter";
    if (desktop.contains("kde") && has("kscreen-doctor"))
        return "kscreen";
    if (wayland && has("wlr-randr"))
        return "wlr";
    if (!wayland && has("xrandr"))
        return "xrandr";
    if (mutterRunning())
        return "mutter";
    if (has("kscreen-doctor"))
        return "kscreen";
    return {};
}

// ------------------------------------------------------------------ Mutter

bool SystemControl::mutterLoad(QString *error)
{
    QDBusMessage call = QDBusMessage::createMethodCall(kMutterService, kMutterPath, kMutterService, "GetCurrentState");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 3) {
        *error = reply.errorMessage().isEmpty() ? QStringLiteral("o GNOME não respondeu") : reply.errorMessage();
        return false;
    }
    m_mutterSerial = reply.arguments().at(0).toUInt();

    // monitors: a((ssss)a(siiddada{sv})a{sv})
    QList<Output> outputs;
    const QDBusArgument mons = reply.arguments().at(1).value<QDBusArgument>();
    mons.beginArray();
    while (!mons.atEnd()) {
        Output o;
        mons.beginStructure();
        QString connector, vendor, product, serial;
        mons.beginStructure();
        mons >> connector >> vendor >> product >> serial;
        mons.endStructure();
        o.name = connector;
        mons.beginArray();
        while (!mons.atEnd()) {
            Mode m;
            QVariantMap props;
            double preferredScale = 1;
            mons.beginStructure();
            mons >> m.id >> m.w >> m.h >> m.refresh >> preferredScale >> m.scales >> props;
            mons.endStructure();
            m.current = props.value("is-current").toBool();
            m.preferred = props.value("is-preferred").toBool();
            o.modes << m;
        }
        mons.endArray();
        QVariantMap mprops;
        mons >> mprops;
        mons.endStructure();
        const QString display = mprops.value("display-name").toString();
        o.label = display.isEmpty() ? connector : QStringLiteral("%1 (%2)").arg(display, connector);
        outputs << o;
    }
    mons.endArray();

    // logical_monitors: a(iiduba(ssss)a{sv})
    QList<MutterLogical> logical;
    const QDBusArgument logs = reply.arguments().at(2).value<QDBusArgument>();
    logs.beginArray();
    while (!logs.atEnd()) {
        MutterLogical l;
        logs.beginStructure();
        logs >> l.x >> l.y >> l.scale >> l.transform >> l.primary;
        logs.beginArray();
        while (!logs.atEnd()) {
            MutterMonitor mm;
            logs.beginStructure();
            logs >> mm.connector >> mm.vendor >> mm.product >> mm.serial;
            logs.endStructure();
            l.monitors << mm;
        }
        logs.endArray();
        QVariantMap props;
        logs >> props;
        logs.endStructure();
        logical << l;
    }
    logs.endArray();

    m_outputs = outputs;
    m_mutterLogical = logical;
    return true;
}

bool SystemControl::mutterApply(const QString &output, const QString &modeId, uint method, QString *error)
{
    static bool registered = false;
    if (!registered) {
        qDBusRegisterMetaType<MonitorAssign>();
        qDBusRegisterMetaType<QList<MonitorAssign>>();
        qDBusRegisterMetaType<LogicalApply>();
        qDBusRegisterMetaType<QList<LogicalApply>>();
        registered = true;
    }
    if (!mutterLoad(error))  // serial e layout atuais
        return false;

    QHash<QString, Output> byName;
    for (const Output &o : std::as_const(m_outputs))
        byName.insert(o.name, o);

    QList<LogicalApply> config;
    for (const MutterLogical &l : std::as_const(m_mutterLogical)) {
        LogicalApply a;
        a.x = l.x;
        a.y = l.y;
        a.scale = l.scale;
        a.transform = l.transform;
        a.primary = l.primary;
        for (const MutterMonitor &mm : l.monitors) {
            const Output o = byName.value(mm.connector);
            QString mode;
            for (const Mode &m : o.modes)
                if ((mm.connector == output && m.id == modeId) || (mm.connector != output && m.current))
                    mode = m.id;
            if (mode.isEmpty()) {
                *error = QStringLiteral("modo %1 não existe em %2").arg(modeId, mm.connector);
                return false;
            }
            // a escala atual precisa ser válida para o novo modo
            if (mm.connector == output)
                for (const Mode &m : o.modes)
                    if (m.id == mode && !m.scales.isEmpty() && !m.scales.contains(a.scale))
                        a.scale = 1.0;
            a.monitors << MonitorAssign{mm.connector, mode, {}};
        }
        config << a;
    }

    QDBusMessage call = QDBusMessage::createMethodCall(kMutterService, kMutterPath, kMutterService, "ApplyMonitorsConfig");
    call << m_mutterSerial << method << QVariant::fromValue(config) << QVariantMap();
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 8000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        *error = reply.errorMessage();
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ carregar

void SystemControl::refreshDisplay()
{
    m_dispError.clear();
    if (m_display.isEmpty()) {
        m_dispLoaded = true;
        m_dispError = "⚠ Não sei mudar a resolução neste ambiente (falta xrandr, kscreen-doctor ou wlr-randr)";
        emit changed("display");
        return;
    }
    if (m_display == "mutter") {
        QString err;
        if (!mutterLoad(&err))
            m_dispError = "⚠ " + err;
        m_dispLoaded = true;
        emit changed("display");
        return;
    }
    if (m_display == "kscreen") {
        run("kscreen-doctor", {"-j"}, [this](int code, const QString &out) {
            m_dispLoaded = true;
            const QJsonObject root = QJsonDocument::fromJson(out.toUtf8()).object();
            if (code != 0 || root.isEmpty()) {
                m_dispError = "⚠ kscreen-doctor não respondeu";
                emit changed("display");
                return;
            }
            QList<Output> outs;
            for (const QJsonValue &v : root.value("outputs").toArray()) {
                const QJsonObject o = v.toObject();
                if (!o.value("connected").toBool() || !o.value("enabled").toBool(true))
                    continue;
                Output out;
                out.name = o.value("name").toString();
                out.label = out.name;
                const QString cur = o.value("currentModeId").toVariant().toString();
                QSet<QString> pref;
                for (const QJsonValue &p : o.value("preferredModes").toArray())
                    pref.insert(p.toVariant().toString());
                for (const QJsonValue &mv : o.value("modes").toArray()) {
                    const QJsonObject m = mv.toObject();
                    Mode mode;
                    mode.id = m.value("id").toVariant().toString();
                    mode.w = m.value("size").toObject().value("width").toInt();
                    mode.h = m.value("size").toObject().value("height").toInt();
                    mode.refresh = m.value("refreshRate").toDouble();
                    mode.current = mode.id == cur;
                    mode.preferred = pref.contains(mode.id);
                    out.modes << mode;
                }
                outs << out;
            }
            m_outputs = outs;
            emit changed("display");
        });
        return;
    }
    if (m_display == "wlr") {
        run("wlr-randr", {"--json"}, [this](int code, const QString &out) {
            m_dispLoaded = true;
            const QJsonArray arr = QJsonDocument::fromJson(out.toUtf8()).array();
            if (code != 0 || arr.isEmpty()) {
                m_dispError = "⚠ wlr-randr não respondeu";
                emit changed("display");
                return;
            }
            QList<Output> outs;
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                if (!o.value("enabled").toBool(true))
                    continue;
                Output output;
                output.name = o.value("name").toString();
                const QString desc = o.value("description").toString();
                output.label = desc.isEmpty() ? output.name : desc + " (" + output.name + ")";
                for (const QJsonValue &mv : o.value("modes").toArray()) {
                    const QJsonObject m = mv.toObject();
                    Mode mode;
                    mode.w = m.value("width").toInt();
                    mode.h = m.value("height").toInt();
                    mode.refresh = m.value("refresh").toDouble();
                    mode.id = QStringLiteral("%1x%2@%3Hz").arg(mode.w).arg(mode.h).arg(mode.refresh, 0, 'f', 3);
                    mode.current = m.value("current").toBool();
                    mode.preferred = m.value("preferred").toBool();
                    output.modes << mode;
                }
                outs << output;
            }
            m_outputs = outs;
            emit changed("display");
        });
        return;
    }
    // xrandr
    run("xrandr", {"--query"}, [this](int code, const QString &out) {
        m_dispLoaded = true;
        if (code != 0) {
            m_dispError = "⚠ xrandr não respondeu";
            emit changed("display");
            return;
        }
        QList<Output> outs;
        static const QRegularExpression head(R"(^(\S+) connected)");
        static const QRegularExpression modeRe(R"(^\s+(\d+)x(\d+)i?\s+(.*)$)");
        static const QRegularExpression rateRe(R"(([\d.]+)([*\s]?)(\+?))");
        bool inOutput = false;
        for (const QString &line : out.split('\n')) {
            const auto h = head.match(line);
            if (h.hasMatch()) {
                outs << Output{h.captured(1), h.captured(1), {}};
                inOutput = true;
                continue;
            }
            if (!line.startsWith(' ')) {
                inOutput = false;
                continue;
            }
            const auto m = modeRe.match(line);
            if (!inOutput || !m.hasMatch())
                continue;
            auto it = rateRe.globalMatch(m.captured(3));
            while (it.hasNext()) {
                const auto r = it.next();
                Mode mode;
                mode.w = m.captured(1).toInt();
                mode.h = m.captured(2).toInt();
                mode.refresh = r.captured(1).toDouble();
                mode.current = r.captured(2) == "*";
                mode.preferred = r.captured(3) == "+";
                mode.id = QStringLiteral("%1x%2@%3").arg(mode.w).arg(mode.h).arg(r.captured(1));
                outs.last().modes << mode;
            }
        }
        m_outputs = outs;
        emit changed("display");
    });
}

// ------------------------------------------------------------------ linhas

QVariantList SystemControl::displayRows() const
{
    QVariantList rows;
    auto r = [](const QString &type, const QString &key, const QString &label, const QString &detail = QString()) {
        return QVariantMap{{"type", type}, {"key", key}, {"label", label}, {"detail", detail}};
    };
    if (!m_dispLoaded)
        return {r("info", "d_load", "Lendo a tela…")};
    if (!m_dispError.isEmpty())
        return {r("info", "d_err", "Tela", m_dispError)};
    if (m_outputs.isEmpty())
        return {r("info", "d_none", "Nenhuma tela encontrada")};

    for (const Output &o : m_outputs) {
        Mode cur;
        for (const Mode &m : o.modes)
            if (m.current)
                cur = m;
        rows << r("header", "h:" + o.name, o.label);
        rows << r("info", "cur:" + o.name, "Atual",
                  cur.w ? QStringLiteral("%1 × %2 · %3 Hz").arg(cur.w).arg(cur.h).arg(qRound(cur.refresh)) : "desconhecida");

        // resoluções únicas, da maior para a menor
        QList<QPair<int, int>> sizes;
        for (const Mode &m : o.modes)
            if (!sizes.contains(qMakePair(m.w, m.h)))
                sizes << qMakePair(m.w, m.h);
        std::sort(sizes.begin(), sizes.end(), [](auto a, auto b) { return a.first * a.second > b.first * b.second; });
        QVariantList resOpts;
        for (const auto &s : sizes) {
            bool pref = false;
            for (const Mode &m : o.modes)
                if (m.w == s.first && m.h == s.second && m.preferred)
                    pref = true;
            resOpts << QVariantMap{{"label", QStringLiteral("%1 × %2%3").arg(s.first).arg(s.second).arg(pref ? "  (recomendada)" : "")},
                                   {"value", resKey(s.first, s.second)}};
        }
        const QString res = m_pendingRes.value(o.name, resKey(cur.w, cur.h));
        QVariantMap resRow = r("choice", "res:" + o.name, "Resolução");
        resRow.insert("options", resOpts);
        resRow.insert("value", res);
        rows << resRow;

        QVariantList hzOpts;
        QList<double> rates;
        for (const Mode &m : o.modes)
            if (resKey(m.w, m.h) == res && !rates.contains(m.refresh))
                rates << m.refresh;
        std::sort(rates.begin(), rates.end(), std::greater<double>());
        for (double hz : rates)
            hzOpts << QVariantMap{{"label", QStringLiteral("%1 Hz").arg(QString::number(hz, 'f', hz == qRound(hz) ? 0 : 2))},
                                  {"value", hzKey(hz)}};
        QString hz = m_pendingHz.value(o.name);
        if (hz.isEmpty() || std::none_of(rates.begin(), rates.end(), [&](double x) { return hzKey(x) == hz; }))
            hz = (res == resKey(cur.w, cur.h)) ? hzKey(cur.refresh) : (rates.isEmpty() ? QString() : hzKey(rates.first()));
        QVariantMap hzRow = r("choice", "hz:" + o.name, "Taxa de atualização");
        hzRow.insert("options", hzOpts);
        hzRow.insert("value", hz);
        rows << hzRow;

        const bool changedSel = res != resKey(cur.w, cur.h) || hz != hzKey(cur.refresh);
        if (changedSel)
            rows << r("action", "apply:" + o.name, "Aplicar " + QString(res).replace('x', QStringLiteral(" × ")) + " · " + QString::number(qRound(hz.toDouble())) + " Hz",
                      "Volta sozinha em 15 s se você não confirmar");
    }
    if (m_display == "xrandr")
        rows << r("info", "d_note", "Observação", "No X11 a mudança vale até sair da sessão");
    return rows;
}

void SystemControl::displayAct(const QString &key, const QString &value)
{
    const QString output = key.section(':', 1);
    if (key.startsWith("res:")) {
        m_pendingRes.insert(output, value);
        m_pendingHz.remove(output);
        emit changed("display");
        return;
    }
    if (key.startsWith("hz:")) {
        m_pendingHz.insert(output, value);
        emit changed("display");
        return;
    }
    if (!key.startsWith("apply:"))
        return;

    // descobre o modo escolhido e o atual (para poder voltar)
    const QVariantList rowsNow = displayRows();
    QString res, hz;
    for (const QVariant &v : rowsNow) {
        const QVariantMap m = v.toMap();
        if (m.value("key") == "res:" + output)
            res = m.value("value").toString();
        if (m.value("key") == "hz:" + output)
            hz = m.value("value").toString();
    }
    QString target, current;
    for (const Output &o : std::as_const(m_outputs)) {
        if (o.name != output)
            continue;
        for (const Mode &m : o.modes) {
            if (m.current)
                current = m.id;
            if (resKey(m.w, m.h) == res && hzKey(m.refresh) == hz && target.isEmpty())
                target = m.id;
        }
    }
    if (target.isEmpty()) {
        emit message("Modo de tela não encontrado.");
        return;
    }
    QString err;
    if (!applyMode(output, target, false, &err)) {
        emit message("Não foi possível mudar a resolução: " + err);
        refreshDisplay();
        return;
    }
    m_revertOutput = output;
    m_revertMode = current;
    m_keepOutput = output;
    m_keepMode = target;
    m_pendingRes.remove(output);
    m_pendingHz.remove(output);
    m_revertTimer.start();
    emit displayConfirm(m_revertTimer.interval() / 1000);
    QTimer::singleShot(600, this, &SystemControl::refreshDisplay);
}

bool SystemControl::applyMode(const QString &output, const QString &modeId, bool persistent, QString *error)
{
    if (m_display == "mutter")
        return mutterApply(output, modeId, persistent ? 2 : 1, error);

    QString program;
    QStringList args;
    if (m_display == "kscreen") {
        program = "kscreen-doctor";
        args << QStringLiteral("output.%1.mode.%2").arg(output, modeId);
    } else if (m_display == "wlr") {
        program = "wlr-randr";
        args << "--output" << output << "--mode" << modeId;
    } else {
        program = "xrandr";
        args << "--output" << output << "--mode" << modeId.section('@', 0, 0) << "--rate" << modeId.section('@', 1);
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        *error = QString::fromUtf8(p.readAll()).trimmed().section('\n', -1);
        if (error->isEmpty())
            *error = program + " falhou";
        return false;
    }
    return true;
}

void SystemControl::keepDisplay(bool keep)
{
    if (m_revertOutput.isEmpty())
        return;
    m_revertTimer.stop();
    QString err;
    if (keep) {
        // GNOME: até aqui a mudança era temporária; agora grava
        if (m_display == "mutter" && !applyMode(m_keepOutput, m_keepMode, true, &err))
            emit message("A resolução ficou, mas não foi salva: " + err);
        else
            emit message("Resolução mantida.");
    } else {
        if (!m_revertMode.isEmpty() && !applyMode(m_revertOutput, m_revertMode, false, &err))
            emit message("Não consegui voltar a resolução: " + err);
        else
            emit message("Resolução anterior restaurada.");
    }
    m_revertOutput.clear();
    m_revertMode.clear();
    QTimer::singleShot(600, this, &SystemControl::refreshDisplay);
}
