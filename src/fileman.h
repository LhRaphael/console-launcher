#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <atomic>

class Library;
class QThread;

// Gerenciador de arquivos da interface: listar, abrir, renomear, nova pasta,
// lixeira, apagar e copiar/mover (em segundo plano, com progresso).
class FileManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(QString trashPath READ trashPath CONSTANT)

public:
    explicit FileManager(Library *library, QObject *parent = nullptr);
    ~FileManager() override;

    bool busy() const { return m_thread != nullptr; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString trashPath() const;

    Q_INVOKABLE QVariantList places() const;
    // sort: name | date | size
    Q_INVOKABLE QVariantMap list(const QString &path, bool showHidden, const QString &sort) const;
    Q_INVOKABLE QVariantMap details(const QString &path) const;
    // consoles que abrem este arquivo/pasta: [{id, name}]
    Q_INVOKABLE QVariantList gameSystems(const QString &path) const;
    Q_INVOKABLE bool open(const QString &path);

    // Retornam "" quando dá certo, ou a mensagem de erro.
    Q_INVOKABLE QString rename(const QString &path, const QString &newName);
    Q_INVOKABLE QString makeDir(const QString &dir, const QString &name);
    Q_INVOKABLE QString trash(const QStringList &paths);
    Q_INVOKABLE QString remove(const QStringList &paths);
    Q_INVOKABLE QString emptyTrash();
    Q_INVOKABLE QString paste(const QStringList &paths, const QString &destDir, bool move);
    Q_INVOKABLE void cancel() { m_cancel = true; }

    static QString humanSize(qint64 bytes);
    static bool isProtected(const QString &path);
    static bool isWritableArea(const QString &path);

    // usado pela cópia em segundo plano
    void postProgress(double p, const QString &status);
    bool cancelled() const { return m_cancel.load(); }

signals:
    void busyChanged();
    void progressChanged();
    void finished(const QString &message, const QString &destDir);

private:
    Library *m_lib;
    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel{false};
    double m_progress = 0;
    QString m_status;
};
