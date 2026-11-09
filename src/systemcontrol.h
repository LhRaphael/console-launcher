#pragma once
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <functional>

// Controles do sistema usados pelo menu lateral: som, Bluetooth, rede, tela,
// energia e os aplicativos abertos pelo launcher. Tudo por ferramentas padrão
// do Debian, rodadas em segundo plano para nunca travar a interface:
//   som: pactl (PulseAudio/PipeWire) ou wpctl (WirePlumber)
//   Bluetooth: bluetoothctl · rede: nmcli (NetworkManager)
//   tela: Mutter/GNOME (D-Bus), kscreen-doctor (KDE), wlr-randr ou xrandr
class SystemControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList openApps READ openApps NOTIFY openAppsChanged)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString hostName READ hostName CONSTANT)
    Q_PROPERTY(QString sessionType READ sessionType CONSTANT)
    Q_PROPERTY(bool hasSystemSettings READ hasSystemSettings CONSTANT)

public:
    explicit SystemControl(QObject *parent = nullptr);

    QVariantList openApps() const;
    QString userName() const;
    QString hostName() const;
    QString sessionType() const;
    bool hasSystemSettings() const { return !m_settingsApp.isEmpty(); }

    // Linhas da seção (audio | bluetooth | network | display), no mesmo formato
    // dos painéis de opções: header, info, toggle, slider, choice, action, item.
    Q_INVOKABLE QVariantList rows(const QString &section) const;
    Q_INVOKABLE void refresh(const QString &section);
    // value: novo valor (toggle/slider/choice) ou "secondary" para a ação X de um item
    Q_INVOKABLE void act(const QString &section, const QString &key, const QString &value);
    Q_INVOKABLE void connectWifi(const QString &ssid, const QString &password);
    Q_INVOKABLE void keepDisplay(bool keep);
    Q_INVOKABLE void power(const QString &what);  // suspend | logout | reboot | poweroff
    Q_INVOKABLE bool openSystemSettings();
    Q_INVOKABLE void closeApp(qint64 id);

    void trackApp(qint64 pgid, const QString &name, const QString &icon);

signals:
    void changed(const QString &section);
    void openAppsChanged();
    void message(const QString &text);
    void passwordNeeded(const QString &ssid);
    void displayConfirm(int seconds);

public:
    // ---- tela (display.cpp)
    struct Mode {
        QString id;
        int w = 0, h = 0;
        double refresh = 0;
        bool current = false, preferred = false;
        QList<double> scales;  // Mutter
    };
    struct Output {
        QString name, label;
        QList<Mode> modes;
    };
    struct MutterMonitor {
        QString connector, vendor, product, serial;
    };
    struct MutterLogical {
        int x = 0, y = 0;
        double scale = 1;
        uint transform = 0;
        bool primary = false;
        QList<MutterMonitor> monitors;
    };

private:
    using Done = std::function<void(int code, const QString &out)>;
    void run(const QString &program, const QStringList &args, Done done, int timeoutMs = 15000);
    static bool have(const QString &program);

    // som
    struct Sink {
        QString id, name, description;
    };
    void refreshAudio();
    QVariantList audioRows() const;
    void audioAct(const QString &key, const QString &value);
    QString m_audio;  // pactl | wpctl | ""
    int m_volume = -1;
    bool m_muted = false;
    QString m_defaultSink;
    QList<Sink> m_sinks;

    // Bluetooth
    struct BtDev {
        QString mac, name, icon;
        bool paired = false, connected = false;
    };
    void refreshBluetooth();
    QVariantList bluetoothRows() const;
    void bluetoothAct(const QString &key, const QString &value);
    bool m_btLoaded = false, m_btAvailable = false, m_btPowered = false, m_btScanning = false;
    QString m_btError, m_btBusy;
    QList<BtDev> m_btDevices;
    QTimer m_btScanTimer;

    // rede
    struct NetDev {
        QString device, type, state, connection;
    };
    struct Wifi {
        QString ssid, security;
        int signal = 0;
        bool inUse = false;
    };
    void refreshNetwork(bool rescan = false);
    QVariantList networkRows() const;
    void networkAct(const QString &key, const QString &value);
    bool m_netLoaded = false, m_nmAvailable = false, m_wifiEnabled = false, m_wifiScanning = false;
    QString m_netError, m_netBusy;
    QList<NetDev> m_netDevs;
    QList<Wifi> m_wifis;
    QStringList m_knownWifi;

    // tela
    void refreshDisplay();
    QVariantList displayRows() const;
    void displayAct(const QString &key, const QString &value);
    bool applyMode(const QString &output, const QString &modeId, bool persistent, QString *error);
    bool mutterLoad(QString *error);
    bool mutterApply(const QString &output, const QString &modeId, uint method, QString *error);
    QString detectDisplayBackend() const;
    QString m_display;  // mutter | kscreen | wlr | xrandr | ""
    bool m_dispLoaded = false;
    QString m_dispError;
    QList<Output> m_outputs;
    QHash<QString, QString> m_pendingRes, m_pendingHz;  // saída -> escolha ainda não aplicada
    quint32 m_mutterSerial = 0;
    QList<MutterLogical> m_mutterLogical;
    QString m_revertOutput, m_revertMode, m_keepOutput, m_keepMode;
    QTimer m_revertTimer;

    // este computador
    void refreshAbout();
    QVariantList aboutRows() const;
    QString m_gpu;

    // aplicativos abertos
    struct OpenApp {
        qint64 id;
        QString name, icon;
    };
    void pruneApps();
    QList<OpenApp> m_apps;
    QTimer m_appTimer;

    QString m_settingsApp;
};
