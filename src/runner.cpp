#include "runner.h"
#include "gamelogs.h"

#include <QCoreApplication>
#include <QProcessEnvironment>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <csignal>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>

Runner::Runner(QObject *parent) : QObject(parent) {}

Runner::~Runner()
{
    // Ao fechar o launcher, o jogo continua rodando (não é filho "preso" a nós).
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->setParent(nullptr);
    }
}

QString Runner::logPath() { return GameLogs::latestLink(); }

namespace {

bool isAppImage(const QString &path)
{
    if (path.endsWith(".AppImage", Qt::CaseInsensitive))
        return true;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray h = f.read(11);
    return h.size() == 11 && h.mid(8, 2) == "AI" && (h[10] == 1 || h[10] == 2);
}

// O runtime clássico do AppImage monta a imagem com o FUSE 2 (libfuse.so.2 + /dev/fuse).
// Sem ele, o próprio runtime sabe extrair e rodar (APPIMAGE_EXTRACT_AND_RUN=1).
QString fuseProblem()
{
    static int cached = -1;  // 0 ok, 1 sem libfuse2, 2 sem /dev/fuse
    if (cached < 0) {
        void *h = dlopen("libfuse.so.2", RTLD_LAZY | RTLD_LOCAL);
        if (h)
            dlclose(h);
        cached = !h ? 1 : !QFileInfo::exists("/dev/fuse") ? 2 : 0;
    }
    if (cached == 1)
        return QStringLiteral("FUSE 2 ausente");
    if (cached == 2)
        return QStringLiteral("/dev/fuse ausente");
    return {};
}

QString safeName(QString s)
{
    s.replace(QRegularExpression(R"([^\w.-]+)", QRegularExpression::UseUnicodePropertiesOption), "_");
    return s.left(60);
}

}  // namespace

static QString shellQuote(const QString &s)
{
    if (!s.isEmpty() && !s.contains(QRegularExpression(R"([^\w@%+=:,./-])")))
        return s;
    QString q = s;
    q.replace('\'', "'\\''");
    return '\'' + q + '\'';
}

bool Runner::start(const QString &program, const QStringList &args, const QString &workDir,
                   const QVariantMap &game, QString *error)
{
    if (m_proc) {
        *error = QStringLiteral("Já existe um jogo em execução.");
        return false;
    }

    // AppImage sem FUSE 2: extrai e roda (mais lento ao abrir, mas funciona)
    QString note;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString fuse = fuseProblem();
    if (!fuse.isEmpty() && (isAppImage(program) || (!args.isEmpty() && isAppImage(args.first())))) {
        env.insert("APPIMAGE_EXTRACT_AND_RUN", "1");
        // /tmp costuma ficar na memória (tmpfs): extrai no disco
        const QString tmp = QDir::homePath() + "/.cache/console-launcher/appimage-tmp";
        QDir().mkpath(tmp);
        env.insert("TMPDIR", tmp);
        note = fuse + QStringLiteral(": AppImage aberto no modo extrair-e-rodar (instale libfuse2t64 para abrir mais rápido)");
    }

    // Um registro por execução, com o comando exato e o que o emulador imprimir.
    GameLogs::prune();
    const QDateTime now = QDateTime::currentDateTime();
    const QString title = game.value("title").toString();
    const QString sysId = game.value("systemId").toString();
    const QString log = GameLogs::dir() + '/' + now.toString("yyyyMMdd-HHmmss") + '_'
                        + safeName(sysId.isEmpty() ? QStringLiteral("app") : sysId) + '_' + safeName(title) + ".log";
    QDir().mkpath(GameLogs::dir());
    {
        QFile f(log);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QStringList parts{shellQuote(program)};
            for (const QString &a : args)
                parts << shellQuote(a);
            QString head = QStringLiteral("# console-launcher: %1\n# data: %2\n")
                               .arg(QCoreApplication::applicationVersion(), now.toString(Qt::ISODate));
            const QList<QPair<const char *, const char *>> keys{{"jogo", "title"},       {"console", "systemName"},
                                                                {"console-id", "systemId"}, {"emulador", "emulator"},
                                                                {"arquivo", "rom"},      {"modo", "mode"}};
            for (const auto &k : keys) {
                const QString v = game.value(QLatin1String(k.second)).toString().simplified();
                if (!v.isEmpty())
                    head += QStringLiteral("# %1: %2\n").arg(QLatin1String(k.first), v);
            }
            if (!note.isEmpty())
                head += QStringLiteral("# nota: %1\n").arg(note);
            head += QStringLiteral("# cd %1\n$ %2\n\n").arg(shellQuote(workDir), parts.join(' '));
            f.write(head.toUtf8());
        }
    }
    QFile::remove(logPath());
    QFile::link(log, logPath());

    auto *proc = new QProcess(this);
    proc->setProcessEnvironment(env);
    proc->setProgram(program);
    proc->setArguments(args);
    if (!workDir.isEmpty())
        proc->setWorkingDirectory(workDir);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->setStandardOutputFile(log, QIODevice::Append);
    proc->setStandardInputFile(QProcess::nullDevice());
    proc->setChildProcessModifier([] { ::setpgid(0, 0); });

    proc->start();
    if (!proc->waitForStarted(8000)) {
        *error = QStringLiteral("Não foi possível iniciar %1: %2")
                     .arg(QFileInfo(program).fileName(), proc->errorString());
        delete proc;
        return false;
    }

    m_proc = proc;
    m_pid = proc->processId();
    m_game = game;
    m_log = log;
    m_stopping = false;
    m_clock.start();
    connect(proc, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus st) { onFinished(code, int(st)); });
    emit runningChanged();
    return true;
}

void Runner::signalGroup(int sig)
{
    if (m_pid > 0)
        ::kill(-pid_t(m_pid), sig);
}

void Runner::stop()
{
    if (!m_proc || m_stopping)
        return;
    m_stopping = true;
    signalGroup(SIGTERM);
    // Se o emulador não fechar sozinho, força depois de alguns segundos.
    const qint64 pid = m_pid;
    QTimer::singleShot(6000, this, [this, pid] {
        if (m_proc && m_pid == pid)
            signalGroup(SIGKILL);
    });
}

void Runner::onFinished(int exitCode, int exitStatus)
{
    const qint64 ms = m_clock.elapsed();
    const bool crashed = exitStatus == QProcess::CrashExit;
    const QString emulator = m_game.value("emulator").toString();
    const QString log = m_log;

    // rodapé: código de saída e duração (lido pela lista de registros)
    {
        QFile f(log);
        if (f.open(QIODevice::Append)) {
            QString extra = m_stopping ? QStringLiteral(" | encerrado pelo launcher")
                            : crashed  ? QStringLiteral(" | travou")
                                       : QString();
            f.write(QStringLiteral("\n# fim: %1 | código %2 | %3 s%4\n")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                        .arg(crashed ? -1 : exitCode)
                        .arg(QString::number(ms / 1000.0, 'f', 1), extra)
                        .toUtf8());
        }
    }

    m_proc->deleteLater();
    m_proc = nullptr;
    m_pid = 0;
    m_game.clear();
    m_log.clear();
    emit runningChanged();

    // Fechou quase na hora com erro: quase sempre é BIOS/firmware faltando,
    // parâmetro errado ou arquivo incompatível. O log mostra o motivo.
    if (!m_stopping && (crashed || exitCode != 0) && ms < 8000) {
        QString hint;
        QFile f(log);
        if (f.open(QIODevice::ReadOnly)) {
            f.seek(qMax<qint64>(0, f.size() - 64 * 1024));
            hint = GameLogs::diagnose(QString::fromUtf8(f.readAll()), crashed ? -1 : exitCode);
        }
        emit failed(QStringLiteral("%1 fechou logo ao abrir (código %2). %3Veja em Menu ▸ Registros dos jogos.")
                        .arg(emulator.isEmpty() ? QStringLiteral("O emulador") : emulator)
                        .arg(crashed ? -1 : exitCode)
                        .arg(hint.isEmpty() ? QString() : hint + ' '),
                    log);
    }
    m_stopping = false;
    emit finished(exitCode);
}
