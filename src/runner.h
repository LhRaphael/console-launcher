#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class QProcess;

// Executa um emulador por vez, em um grupo de processos próprio, para que
// "fechar o jogo" encerre também os filhos (AppImage, flatpak, linosd-run).
class Runner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QVariantMap game READ game NOTIFY runningChanged)

public:
    explicit Runner(QObject *parent = nullptr);
    ~Runner() override;

    bool running() const { return m_proc != nullptr; }
    QVariantMap game() const { return m_game; }

    // game: {title, systemName, emulator, cover}
    bool start(const QString &program, const QStringList &args, const QString &workDir,
               const QVariantMap &game, QString *error);

    Q_INVOKABLE void stop();
    static QString logPath();          // ultimo-jogo.log (aponta para o registro mais recente)
    QString currentLog() const { return m_log; }
    qint64 pid() const { return m_pid; }

signals:
    void runningChanged();
    void finished(int exitCode);
    void failed(const QString &message, const QString &logFile);

private:
    void onFinished(int exitCode, int exitStatus);
    void signalGroup(int sig);

    QProcess *m_proc = nullptr;
    qint64 m_pid = 0;
    QElapsedTimer m_clock;
    QVariantMap m_game;
    QString m_log;
    bool m_stopping = false;
};
