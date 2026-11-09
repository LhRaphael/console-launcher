#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>

class Runner;
class SystemControl;

// Modo jogo: enquanto um jogo roda, deixa a máquina só para ele.
//  - pausa (SIGSTOP) ou fecha programas em segundo plano que não têm a ver com o
//    jogo (navegador, terminal, gerenciador de arquivos, indexação…) e retoma
//    tudo (SIGCONT) quando o jogo fecha;
//  - nunca mexe no emulador, no launcher, em gravação de tela, métricas, áudio
//    nem na área de trabalho;
//  - pede o perfil de energia "desempenho", segura o descanso de tela, silencia
//    as notificações e registra o jogo no GameMode (Feral), quando existem.
// Se o launcher cair no meio do jogo, o que foi pausado é retomado na próxima
// vez que ele abrir (ou com `console-launcher --resume-apps`).
class GameMode : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int pausedCount READ pausedCount NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

public:
    GameMode(Runner *runner, SystemControl *machine, QObject *parent = nullptr);
    ~GameMode() override;

    bool active() const { return m_active; }
    int pausedCount() const { return m_paused.size(); }
    QString summary() const { return m_summary; }

    // Linhas da seção "Launcher" do menu lateral e dos "Abertos"
    Q_INVOKABLE QVariantList optionRows() const;
    Q_INVOKABLE QVariantList openRows() const;
    Q_INVOKABLE void setOption(const QString &key, const QString &value);
    Q_INVOKABLE void resumeAll();
    // Programas que seriam pausados agora (prévia)
    Q_INVOKABLE QStringList preview() const;

    static bool forceFullscreen();
    // Retoma o que ficou pausado por uma sessão que caiu. Devolve quantos.
    static int recover();

signals:
    void changed();
    void message(const QString &text);

private:
    struct Proc {
        qint64 pid = 0, ppid = 0, pgid = 0;
        quint64 start = 0;
        QString comm, exe, argv0;
        char state = '?';
    };
    struct Paused {
        qint64 pid;
        quint64 start;
        QString name;
    };
    void activate();
    void deactivate();
    QList<Proc> scan() const;
    // alvos agrupados: nome amigável → pids (inclui os filhos)
    QList<QPair<QString, QList<Proc>>> targets(const QList<Proc> &procs) const;
    void saveState() const;
    void holdResources(const QString &title, qint64 gamePid);
    void releaseResources();

    Runner *m_runner;
    SystemControl *m_machine;
    bool m_active = false;
    QList<Paused> m_paused;
    QStringList m_pausedNames;
    QString m_summary;
    // recursos segurados
    quint32 m_profileCookie = 0;
    QString m_profileService, m_profilePath;
    quint32 m_idleCookie = 0;
    quint32 m_notifyCookie = 0;
    QString m_bannersBefore;  // GNOME: valor original de show-banners
    qint64 m_gamemodePid = 0;
};
