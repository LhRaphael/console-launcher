#pragma once
#include "apps.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class Runner;

struct SystemDef {
    QString id, name, shortName, color, dir, emulator, exe;
    QStringList extensions, args, openArgs, folderMarkers;
    // Alternativas quando "exe" não está em ~/emulators: um programa no PATH
    // (pacote do sistema) ou "flatpak:ID" (ex.: "mgba-qt", "flatpak:io.mgba.mGBA")
    QStringList fallback;
    QJsonArray settings;  // opções do emulador (ver data/systems.json)
    QJsonObject bios;     // {"type": "pcsx2", "dir": "..."} — opcional (ver bios.cpp)
    bool overlay = true;
};

// "Super Mario World (USA) [!]" -> "Super Mario World", tags ["USA", "!"]
void splitGameTitle(const QString &base, QString *title, QStringList *tags);
QString tildePath(const QString &p);
QString expandHome(QString p);

// Biblioteca: consoles (systems.json), jogos, aplicativos e as opções que o
// usuário escolhe por console e por jogo (settings.json).
class Library : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList systems READ systems NOTIFY systemsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(int recentRevision READ recentRevision NOTIFY recentChanged)
    Q_PROPERTY(QString romRoot READ romRootDisplay NOTIFY systemsChanged)
    Q_PROPERTY(QString configPath READ configPathDisplay CONSTANT)
    Q_PROPERTY(bool overlayAvailable READ overlayAvailable CONSTANT)
    Q_PROPERTY(bool overlayEnabled READ overlayEnabled WRITE setOverlayEnabled NOTIFY overlayEnabledChanged)
    Q_PROPERTY(QString lastSystem READ lastSystem WRITE setLastSystem NOTIFY lastSystemChanged)
    Q_PROPERTY(QString startupMessage READ startupMessage CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    Library(const QString &configOverride, Runner *runner, QObject *parent = nullptr);

    QVariantList systems() const { return m_systemList; }
    int revision() const { return m_revision; }
    int recentRevision() const { return m_recentRevision; }
    QString romRootDisplay() const;
    QString configPathDisplay() const;
    bool overlayAvailable() const { return !m_wrapperPath.isEmpty(); }
    bool overlayEnabled() const { return m_overlayEnabled && overlayAvailable(); }
    void setOverlayEnabled(bool on);
    QString lastSystem() const { return m_lastSystem; }
    void setLastSystem(const QString &id);
    QString startupMessage() const { return m_startupMessage; }
    QString version() const;

    Q_INVOKABLE QVariantList games(const QString &systemId) const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool launch(const QString &systemId, const QString &path, const QString &title,
                            const QString &cover);
    Q_INVOKABLE bool launchApp(const QString &desktopFile);
    Q_INVOKABLE bool openEmulator(const QString &systemId);

    // Linhas dos painéis de configuração (type: header|info|choice|text|action)
    Q_INVOKABLE QVariantList systemOptions(const QString &systemId) const;
    Q_INVOKABLE QVariantList gameOptions(const QString &systemId, const QString &path) const;
    Q_INVOKABLE void setSystemOption(const QString &systemId, const QString &key, const QString &value);
    // value "__inherit__" volta a usar a opção do console
    Q_INVOKABLE void setGameOption(const QString &systemId, const QString &path, const QString &key,
                                   const QString &value);
    Q_INVOKABLE void resetSystemOptions(const QString &systemId);
    Q_INVOKABLE void resetGameOptions(const QString &systemId, const QString &path);
    Q_INVOKABLE bool isConsole(const QString &systemId) const { return findSystem(systemId) != nullptr; }

    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void reboot();

    bool systemDef(const QString &id, SystemDef *out) const;
    QList<SystemDef> systemDefs() const { return m_defs; }
    QString systemPath(const SystemDef &sys) const { return m_romRoot + '/' + sys.dir; }
    QString emulatorDir() const { return m_emuDir; }
    // Caminho do emulador do console (vazio + motivo se não estiver instalado)
    QString emulatorPath(const QString &systemId, QString *problem) const;
    // Abre o emulador com argumentos próprios (ex.: instalar firmware), pelo Runner
    bool startEmulator(const QString &systemId, const QStringList &args, const QVariantMap &info,
                       QString *error);

signals:
    void systemsChanged();
    void revisionChanged();
    void recentChanged();
    void overlayEnabledChanged();
    void lastSystemChanged();
    void message(const QString &text);
    void appStarted(qint64 pgid, const QString &name, const QString &icon);

private:
    bool loadConfig(const QString &path, QString *error);
    void ensureUserConfig();
    void scan();
    void rescanSystem(const QString &id);
    void scanApps();
    QVariantList scanSystem(const SystemDef &sys) const;
    QVariantList recentGames() const;
    QVariantList appList() const;
    void pushRecent(const QString &systemId, const QString &path, const QString &title);
    // prefix: argumentos que vão antes dos do console (ex.: "run ID" de um Flatpak)
    QString resolveExe(const SystemDef &sys, QString *problem, QStringList *prefix = nullptr) const;
    QString resolveFallback(const SystemDef &sys, QStringList *prefix) const;
    QStringList exeCandidates(const SystemDef &sys) const;
    const SystemDef *findSystem(const QString &id) const;
    void rebuildSystemList();

    bool lookup(const char *group, const QString &id, const QString &key, QString *out) const;
    void store(const char *group, const QString &id, const QString &key, const QString *value);
    void saveStore();
    QString effective(const SystemDef &sys, const QString &gamePath, const QString &key,
                      const QString &fallback) const;
    QVariantMap gameEntry(const QString &systemId, const QString &path) const;

    Runner *m_runner;
    QString m_configPath, m_storePath;
    QString m_romRoot, m_emuDir, m_wrapperPath;
    bool m_showEmpty = true;
    bool m_overlayEnabled = false;
    QString m_lastSystem;
    QString m_startupMessage;
    QList<SystemDef> m_defs;
    QHash<QString, QVariantList> m_games;
    QList<AppEntry> m_apps;
    QJsonObject m_store;
    QVariantList m_systemList;
    QFileSystemWatcher m_appWatch;
    QTimer m_appTimer;
    int m_revision = 0;
    int m_recentRevision = 0;
};
