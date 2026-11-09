#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <atomic>

class Library;
class QThread;

// Importa jogos de qualquer pasta para a pasta do console (~/ROMs/<console>):
// extrai arquivos compactados, leva junto os arquivos de que o jogo depende
// (.bin do .cue, discos do .m3u, capas), agrupa jogos de vários discos e, no
// fim, apaga os compactados e os restos (txt, nfo, pastas vazias).
class Importer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(QString item READ item NOTIFY progressChanged)

public:
    explicit Importer(Library *library, QObject *parent = nullptr);
    ~Importer() override;

    bool busy() const { return m_thread != nullptr; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString item() const { return m_item; }

    // {path, display, parent, entries:[{name,path,kind,gameRoot,protected,sizeText}], hidden, error}
    Q_INVOKABLE QVariantMap listDir(const QString &path, const QString &systemId) const;
    Q_INVOKABLE QVariantList places() const;
    Q_INVOKABLE QString startDir(const QString &systemId) const;
    // Retorna "" se começou, ou o motivo da recusa.
    Q_INVOKABLE QString start(const QString &systemId, const QStringList &paths, bool cleanup);
    Q_INVOKABLE void cancel();

    // Pastas que nunca podem ser importadas inteiras (pasta pessoal, Downloads…)
    static bool isProtectedDir(const QString &path);
    static bool isAllowedSource(const QString &path);

    // usado pelo trabalho em segundo plano
    void postProgress(double p, const QString &status, const QString &item);
    bool cancelled() const { return m_cancel.load(); }

signals:
    void busyChanged();
    void progressChanged();
    void finished(const QVariantMap &report);

private:
    Library *m_lib;
    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel{false};
    double m_progress = 0;
    QString m_status, m_item;
};
