#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Runner;

// Registros (logs) de cada execução de jogo/emulador.
// Um arquivo por execução em ~/.local/state/console-launcher/logs/, com um
// cabeçalho "# chave: valor" (jogo, console, emulador, comando) e uma linha
// "# fim: …" com o código de saída. ultimo-jogo.log aponta para o mais recente.
class GameLogs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit GameLogs(Runner *runner, QObject *parent = nullptr);

    int revision() const { return m_revision; }

    static QString dir();
    static QString latestLink();
    // Explica erros conhecidos a partir da saída do emulador ("" se nenhum)
    static QString diagnose(const QString &output, int exitCode);
    // Lê o cabeçalho e o rodapé de um registro: {title, system, systemName, emulator, rom, mode, when,
    // whenText, status (ok|error|running|stopped), exitCode, duration, hint, sizeText, path}
    static QVariantMap summary(const QString &path, bool withHint = true);
    // Mantém só os mais recentes
    static void prune(int keep = 80);

    Q_INVOKABLE QVariantList list(int limit = 40) const;
    Q_INVOKABLE QVariantMap read(const QString &path) const;
    Q_INVOKABLE QVariantList rows() const;
    Q_INVOKABLE bool remove(const QString &path);
    Q_INVOKABLE int clear();
    Q_INVOKABLE QString folder() const { return dir(); }

signals:
    void changed();

private:
    bool isOurs(const QString &path) const;
    Runner *m_runner;
    int m_revision = 0;
};
