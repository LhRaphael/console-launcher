#pragma once
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class Library;

// Papel de parede do launcher: imagem ou vídeo, um geral e, se quiser, um próprio
// para cada aba de console. Também pode ser uma pasta: o fundo troca sozinho de
// tempos em tempos (em sequência ou aleatório). A troca em si (com transição
// suave) é feita em qml/Wallpaper.qml.
//
// Guardado em console-launcher.conf, grupo [wallpaper], uma "escala" por chave:
//   global/kind = default | file | folder
//   <console>/kind = inherit | default | file | folder
//   …/path, …/interval (min), …/order (seq|random), …/dim (0–80), …/fit (cover|contain)
class Wallpapers : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool videoSupported READ videoSupported CONSTANT)
    Q_PROPERTY(QString editScope READ editScope NOTIFY changed)
    // Temas por console (fonte, cores e fundo de cada aba) e animação do fundo do tema
    Q_PROPERTY(bool themes READ themes NOTIFY changed)
    Q_PROPERTY(bool themeMotion READ themeMotion NOTIFY changed)

public:
    explicit Wallpapers(Library *library, QObject *parent = nullptr);

    int revision() const { return m_revision; }
    bool videoSupported() const { return m_video; }
    QString editScope() const { return m_editScope; }
    bool themes() const;
    bool themeMotion() const;
    Q_INVOKABLE QVariantList themeRows() const;

    // Configuração que vale para a aba: {scope, kind, path, files[], interval, order, dim, fit}
    Q_INVOKABLE QVariantMap effective(const QString &systemId) const;
    // Linhas da seção "Tela" do menu lateral (consoleId = aba atual)
    Q_INVOKABLE QVariantList rows(const QString &consoleId) const;
    // Ação/alteração vinda das linhas (chaves "wp:…")
    Q_INVOKABLE void set(const QString &consoleId, const QString &key, const QString &value);
    // Resultado do seletor de arquivos: kind = file | folder
    Q_INVOKABLE void choose(const QString &scope, const QString &kind, const QString &path);
    Q_INVOKABLE void setScope(const QString &scope, const QString &kind, const QString &path) { choose(scope, kind, path); }
    Q_INVOKABLE QStringList mediaIn(const QString &folder) const;
    Q_INVOKABLE bool isMedia(const QString &path) const;
    Q_INVOKABLE bool isVideo(const QString &path) const;
    Q_INVOKABLE QString scopeName(const QString &scope) const;
    Q_INVOKABLE QUrl url(const QString &path) const { return QUrl::fromLocalFile(path); }

signals:
    void changed();
    void pickRequested(const QString &scope, const QString &kind, const QString &startDir);
    void nextRequested();
    void message(const QString &text);

private:
    QVariant value(const QString &scope, const QString &key, const QVariant &def) const;
    void put(const QString &scope, const QString &key, const QVariant &v);
    QString kindOf(const QString &scope) const;
    void bump();

    Library *m_lib;
    bool m_video = false;
    QString m_editScope = "global";
    int m_revision = 0;
};
