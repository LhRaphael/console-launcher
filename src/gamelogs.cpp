#include "gamelogs.h"
#include "runner.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>

namespace {

QString stateBase()
{
    // XDG_STATE_HOME (QStandardPaths só tem StateLocation a partir do Qt 6.7)
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base))
        base = QDir::homePath() + "/.local/state";
    return base + "/console-launcher";
}

QString humanSize(qint64 b)
{
    return QLocale(QLocale::Portuguese, QLocale::Brazil).formattedDataSize(b, 1, QLocale::DataSizeTraditionalFormat);
}

QString whenText(const QDateTime &t)
{
    if (!t.isValid())
        return {};
    const QDate today = QDate::currentDate();
    const QString hm = t.toString("HH:mm");
    if (t.date() == today)
        return QStringLiteral("hoje, ") + hm;
    if (t.date() == today.addDays(-1))
        return QStringLiteral("ontem, ") + hm;
    return t.toString("dd/MM") + ", " + hm;
}

// Último argumento entre aspas/sem aspas que parece um arquivo (registros antigos sem cabeçalho)
QString romFromCommand(const QString &cmd)
{
    static const QRegularExpression quoted(R"('((?:[^']|'\\'')*)'|(\S+))");
    QString last;
    auto it = quoted.globalMatch(cmd);
    while (it.hasNext()) {
        const auto m = it.next();
        QString a = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
        a.replace(QStringLiteral("'\\''"), QStringLiteral("'"));
        if (a.startsWith('/') && QFileInfo(a).suffix().size() >= 2 && !a.endsWith(".AppImage", Qt::CaseInsensitive))
            last = a;
    }
    return last;
}

QString titleFromRom(const QString &rom)
{
    QString t = QFileInfo(rom).completeBaseName();
    t.remove(QRegularExpression(R"(\s*[\(\[].*$)"));
    return t.trimmed();
}

}  // namespace

GameLogs::GameLogs(Runner *runner, QObject *parent) : QObject(parent), m_runner(runner)
{
    QDir().mkpath(dir());
    // Versões antigas gravavam só ultimo-jogo.log (arquivo comum): vira o primeiro registro da lista.
    const QFileInfo old(latestLink());
    if (old.exists() && !old.isSymLink() && old.isFile()) {
        const QString name = old.lastModified().toString("yyyyMMdd-HHmmss") + "_anterior.log";
        QFile::rename(old.absoluteFilePath(), dir() + '/' + name);
    }
    connect(m_runner, &Runner::runningChanged, this, [this] {
        ++m_revision;
        emit changed();
    });
}

QString GameLogs::dir() { return stateBase() + "/logs"; }
QString GameLogs::latestLink() { return stateBase() + "/ultimo-jogo.log"; }

QString GameLogs::diagnose(const QString &raw, int exitCode)
{
    // só a saída do emulador: o cabeçalho/rodapé do launcher ("# …", "$ comando") fica de fora
    QStringList keep;
    for (const QString &line : raw.split('\n'))
        if (!line.startsWith(QLatin1String("# ")) && !line.startsWith(QLatin1String("$ ")))
            keep << line;
    const QString out = keep.join('\n');
    const QString l = out.toLower();
    auto has = [&](const char *s) { return l.contains(QLatin1String(s)); };

    if (has("libfuse.so.2") || has("appimages require fuse"))
        return QStringLiteral("Falta o FUSE 2, usado pelos emuladores em AppImage. Instale com "
                              "\"sudo apt install libfuse2t64\" (Debian 13) ou \"sudo apt install libfuse2\" (Debian 12).");
    if (has("/dev/fuse") || has("fuse: device not found") || has("fusermount"))
        return QStringLiteral("O FUSE não está disponível neste sistema. Instale libfuse2t64 e confira se o módulo fuse está carregado.");
    static const QRegularExpression sharedLib(R"(error while loading shared libraries: ([^:\s]+))");
    if (const auto m = sharedLib.match(out); m.hasMatch())
        return QStringLiteral("Falta a biblioteca %1 no sistema (procure o pacote com \"apt-file search %1\").").arg(m.captured(1));
    if (exitCode == 126 || (has("permission denied") && has("exec")))
        return QStringLiteral("O emulador não tem permissão para executar. Rode \"chmod +x\" no arquivo do emulador.");
    if (exitCode == 127)
        return QStringLiteral("O programa do emulador não foi encontrado.");
    if (has("bios") && (has("not found") || has("missing") || has("no bios") || has("failed to load bios")
                        || has("could not find") || has("unable to find") || has("not configured")))
        return QStringLiteral("O emulador não encontrou a BIOS. Importe em Configurar o console ▸ BIOS.");
    if (has("prod.keys") || (has("keys") && (has("missing") || has("not found"))))
        return QStringLiteral("Faltam as chaves do console (prod.keys). Importe em Configurar o console ▸ BIOS e firmware.");
    if (has("firmware") && (has("not installed") || has("missing") || has("not found")))
        return QStringLiteral("Falta o firmware do console. Importe em Configurar o console ▸ BIOS e firmware.");
    if (has("vulkan") && (has("failed") || has("error") || has("not supported") || has("vk_error")))
        return QStringLiteral("Problema com o Vulkan. Troque o renderizador para OpenGL nas opções do console "
                              "ou instale os drivers (sudo apt install mesa-vulkan-drivers).");
    if (has("cannot open display") || has("could not connect to display") || has("failed to open display")
        || (has("wayland") && has("failed to connect")) || has("could not initialize sdl") || has("no available video device"))
        return QStringLiteral("O emulador não conseguiu abrir a janela (problema com a sessão gráfica).");
    if (has("segmentation fault") || exitCode == 139 || exitCode == -11)
        return QStringLiteral("O emulador travou (falha de segmentação). Tente outra versão do emulador ou outro renderizador.");
    if ((has("unsupported") || has("not supported")) && (has("rom") || has("format") || has("file")))
        return QStringLiteral("O formato do jogo não é aceito por este emulador.");
    if (has("no such file or directory") && (has("rom") || has("open") || has("load")))
        return QStringLiteral("O emulador não encontrou um arquivo (o jogo foi movido ou apagado?).");
    return {};
}

QVariantMap GameLogs::summary(const QString &path, bool withHint)
{
    QVariantMap s{{"path", path}};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return s;
    const qint64 size = f.size();
    QHash<QString, QString> h;
    QString cmd, firstStamp;
    for (int i = 0; i < 20 && !f.atEnd(); ++i) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith("$ ")) {
            cmd = line.mid(2);
            break;
        }
        if (!line.startsWith('#'))
            continue;
        const QString body = line.mid(1).trimmed();
        const int colon = body.indexOf(": ");
        if (colon > 0 && !body.startsWith("cd "))
            h.insert(body.left(colon), body.mid(colon + 2));
        else if (firstStamp.isEmpty() && body.size() >= 19 && body[4] == '-')
            firstStamp = body;
    }
    // rodapé e trecho final (diagnóstico)
    const qint64 tailSize = qMin<qint64>(size, 64 * 1024);
    f.seek(size - tailSize);
    const QString tail = QString::fromUtf8(f.read(tailSize));
    static const QRegularExpression foot(R"(# fim: (\S+) \| código (-?\d+) \| ([\d.]+) s(?: \| (.*))?)");
    QRegularExpressionMatch fm;
    for (auto it = foot.globalMatch(tail); it.hasNext();)
        fm = it.next();

    const QString rom = h.value("arquivo", romFromCommand(cmd));
    QString title = h.value("jogo");
    if (title.isEmpty())
        title = rom.isEmpty() ? QStringLiteral("Execução sem nome") : titleFromRom(rom);
    const QDateTime when = QDateTime::fromString(h.value("data", firstStamp), Qt::ISODate);
    s.insert("title", title);
    s.insert("system", h.value("console-id"));
    s.insert("systemName", h.value("console"));
    s.insert("emulator", h.value("emulador", cmd.isEmpty() ? QString() : QFileInfo(cmd.section(' ', 0, 0).remove('\'')).fileName()));
    s.insert("rom", rom);
    s.insert("mode", h.value("modo", "game"));
    s.insert("note", h.value("nota"));
    s.insert("command", cmd);
    s.insert("when", when);
    s.insert("whenText", whenText(when.isValid() ? when : QFileInfo(path).lastModified()));
    s.insert("sizeText", humanSize(size));

    int code = 0;
    QString status = "unknown";
    if (fm.hasMatch()) {
        code = fm.captured(2).toInt();
        const QString extra = fm.captured(4);
        status = extra.contains("encerrado") ? "stopped" : code != 0 ? "error" : "ok";
        s.insert("duration", fm.captured(3).toDouble());
    }
    s.insert("exitCode", code);
    if (withHint) {
        const QString hint = diagnose(tail, fm.hasMatch() ? code : 0);
        s.insert("hint", hint);
        if (!hint.isEmpty() && status == "unknown")
            status = "error";
    }
    s.insert("status", status);
    return s;
}

void GameLogs::prune(int keep)
{
    QDir d(dir());
    const QFileInfoList files = d.entryInfoList({"*.log"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = keep; i < files.size(); ++i)
        QFile::remove(files[i].absoluteFilePath());
}

QVariantList GameLogs::list(int limit) const
{
    QVariantList out;
    const QString running = m_runner->running() ? QFileInfo(latestLink()).symLinkTarget() : QString();
    const QFileInfoList files = QDir(dir()).entryInfoList({"*.log"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &fi : files) {
        if (out.size() >= limit)
            break;
        QVariantMap s = summary(fi.absoluteFilePath());
        if (!running.isEmpty() && QFileInfo(running).canonicalFilePath() == fi.canonicalFilePath())
            s.insert("status", "running");
        out << s;
    }
    return out;
}

QVariantMap GameLogs::read(const QString &path) const
{
    if (!isOurs(path))
        return {{"error", QStringLiteral("Registro não encontrado.")}};
    QVariantMap s = summary(path);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {{"error", QStringLiteral("Não foi possível abrir o registro.")}};
    constexpr qint64 maxBytes = 400 * 1024;
    const bool truncated = f.size() > maxBytes;
    QString text;
    if (truncated) {
        // cabeçalho + final (onde costuma estar o erro)
        const QByteArray head = f.read(4096);
        f.seek(f.size() - maxBytes);
        text = QString::fromUtf8(head) + QStringLiteral("\n[… %1 omitidos …]\n").arg(humanSize(f.size() - maxBytes - 4096))
               + QString::fromUtf8(f.read(maxBytes));
    } else {
        text = QString::fromUtf8(f.readAll());
    }
    // remove códigos de cor ANSI que alguns emuladores imprimem
    static const QRegularExpression ansi(R"(\x1B\[[0-9;?]*[ -/]*[@-~])");
    text.remove(ansi);
    if (m_runner->running() && QFileInfo(QFileInfo(latestLink()).symLinkTarget()).canonicalFilePath()
                                   == QFileInfo(path).canonicalFilePath())
        s.insert("status", "running");
    s.insert("text", text);
    s.insert("truncated", truncated);
    s.insert("romExists", !s.value("rom").toString().isEmpty() && QFileInfo::exists(s.value("rom").toString()));
    return s;
}

QVariantList GameLogs::rows() const
{
    auto row = [](const QString &type, const QString &key, const QString &label, const QString &detail = {}) {
        return QVariantMap{{"type", type}, {"key", key}, {"label", label}, {"detail", detail}};
    };
    QVariantList rows;
    const QVariantList items = list(40);
    rows << row("header", "h_logs", "Últimas execuções");
    if (items.isEmpty())
        rows << row("info", "empty", QStringLiteral("Nenhum registro ainda"),
                    QStringLiteral("Cada vez que um jogo ou emulador abre, a saída dele fica salva aqui."));
    for (const QVariant &v : items) {
        const QVariantMap s = v.toMap();
        const QString st = s.value("status").toString();
        QString state = st == "running" ? QStringLiteral("▶ Em execução")
                        : st == "ok" ? QStringLiteral("✓ Fechou normalmente")
                        : st == "stopped" ? QStringLiteral("■ Fechado pelo launcher")
                        : st == "error" ? (s.value("exitCode").toInt() != 0
                                               ? QStringLiteral("⚠ Erro (código %1)").arg(s.value("exitCode").toInt())
                                               : QStringLiteral("⚠ Erro"))
                                        : QStringLiteral("· Sem registro de término");
        QStringList parts;
        for (const char *k : {"systemName", "emulator", "whenText"})
            if (!s.value(k).toString().isEmpty())
                parts << s.value(k).toString();
        const QString hint = s.value("hint").toString();
        QString label = s.value("title").toString();
        if (s.value("mode").toString() == "config")
            label = QStringLiteral("%1 (configurações)").arg(s.value("emulator").toString());
        rows << row("action", "log:" + s.value("path").toString(), label,
                    state + " · " + parts.join(" · ")
                        + (hint.isEmpty() || st == "ok" || st == "stopped" || st == "running" ? QString() : QStringLiteral(" — ") + hint));
    }
    rows << row("header", "h_logs2", "Registros");
    QVariantMap clearRow = row("action", "logs-clear", "Apagar todos os registros",
                               QStringLiteral("%1 registro(s) em %2").arg(QDir(dir()).entryList({"*.log"}, QDir::Files).size())
                                   .arg(QString(dir()).replace(QDir::homePath(), "~")));
    clearRow.insert("confirm", true);
    rows << row("action", "logs-folder", "Abrir a pasta dos registros", QString(dir()).replace(QDir::homePath(), "~"))
         << clearRow;
    return rows;
}

bool GameLogs::isOurs(const QString &path) const
{
    const QFileInfo fi(path);
    return fi.isFile() && fi.absolutePath() == QFileInfo(dir()).absoluteFilePath() && fi.suffix() == "log";
}

bool GameLogs::remove(const QString &path)
{
    if (!isOurs(path))
        return false;
    if (m_runner->running()
        && QFileInfo(QFileInfo(latestLink()).symLinkTarget()).canonicalFilePath() == QFileInfo(path).canonicalFilePath())
        return false;  // o jogo aberto ainda escreve nele
    const bool ok = QFile::remove(path);
    ++m_revision;
    emit changed();
    return ok;
}

int GameLogs::clear()
{
    int n = 0;
    const QString running = m_runner->running() ? QFileInfo(QFileInfo(latestLink()).symLinkTarget()).canonicalFilePath()
                                                : QString();
    for (const QFileInfo &fi : QDir(dir()).entryInfoList({"*.log"}, QDir::Files))
        if (fi.canonicalFilePath() != running && QFile::remove(fi.absoluteFilePath()))
            ++n;
    ++m_revision;
    emit changed();
    return n;
}
