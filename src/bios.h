#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <atomic>

class Library;
class Runner;
class QThread;
struct SystemDef;

// BIOS e firmware dos emuladores. O launcher nunca traz nem baixa esses
// arquivos: ele só identifica os que o usuário já tem (dump do próprio
// console), copia cada um para a pasta que o emulador lê e ajusta a
// configuração do emulador quando é preciso (PCSX2.ini, xemu.toml, config.ini do mGBA).
//
// A API de importação (listDir/startDir/start/cancel/busy/progress/finished)
// é a mesma do Importer, para o painel de importação servir aos dois.
class BiosManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY progressChanged)
    Q_PROPERTY(QString item READ item NOTIFY progressChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    enum Kind { None, DuckStation, Pcsx2, Rpcs3, ShadPs4, Xemu, DolphinGc, Eden, Mgba };

    struct Paths {
        Kind kind = None;
        QString base;      // pasta de dados do emulador
        QString biosDir;   // BIOS (PS1/PS2), sys_modules (PS4), arquivos do xemu
        QString fontsDir;  // PS4
        QString config;    // settings.ini / PCSX2.ini / xemu.toml
        QString keysDir, ncaDir;  // Switch
        QString devFlash;  // PS3
        QString gcRoot;    // Dolphin: .../GC
    };

    BiosManager(Library *library, Runner *runner, QObject *parent = nullptr);
    ~BiosManager() override;

    bool busy() const { return m_thread != nullptr; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString item() const { return m_item; }
    int revision() const { return m_revision; }

    // --- importação (mesma forma do Importer)
    Q_INVOKABLE QVariantMap listDir(const QString &path, const QString &systemId) const;
    Q_INVOKABLE QString startDir(const QString &systemId) const;
    Q_INVOKABLE QString start(const QString &systemId, const QStringList &paths, bool cleanup);
    Q_INVOKABLE void cancel();

    // --- situação
    // {need: required|optional|none, ok, summary, dir, items:[{label, detail, state}]}
    Q_INVOKABLE QVariantMap state(const QString &systemId) const;
    Q_INVOKABLE bool handles(const QString &systemId) const;
    // false só quando o console precisa de BIOS e ela não foi encontrada
    Q_INVOKABLE bool ready(const QString &systemId) const;
    Q_INVOKABLE QString folder(const QString &systemId) const;
    // Linhas para o painel do console (F3) e para o menu lateral
    Q_INVOKABLE QVariantList optionRows(const QString &systemId) const;
    Q_INVOKABLE QVariantList overviewRows() const;
    Q_INVOKABLE void setOption(const QString &systemId, const QString &key, const QString &value);
    // Arquivo solto (gerenciador de arquivos): [{id, name, label}] dos consoles que o aceitam
    Q_INVOKABLE QVariantList detect(const QString &path) const;

    // PS3: o firmware é instalado pelo próprio RPCS3 (--installfw)
    Q_INVOKABLE bool firmwarePending(const QString &systemId) const;
    Q_INVOKABLE QString installFirmware(const QString &systemId);

    static Kind kindOf(const SystemDef &sys);
    Paths pathsFor(const SystemDef &sys) const;

    // trabalho em segundo plano
    void postProgress(double p, const QString &status, const QString &item);
    bool cancelled() const { return m_cancel.load(); }

signals:
    void busyChanged();
    void progressChanged();
    void finished(const QVariantMap &report);
    void changed();
    void message(const QString &text);

private:
    void bump();
    QString stagedPup() const;

    Library *m_lib;
    Runner *m_runner;
    QThread *m_thread = nullptr;
    std::atomic<bool> m_cancel{false};
    double m_progress = 0;
    QString m_status, m_item;
    int m_revision = 0;
    QString m_installing;  // console cujo firmware está sendo instalado
    QString m_fwBefore;
};
