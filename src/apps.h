#pragma once
#include <QList>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>

// Aplicativos instalados, lidos dos arquivos .desktop (padrão freedesktop),
// incluindo os exportados pelo Flatpak.
struct AppEntry {
    QString id, name, comment, icon, exec, workDir, file;
};

QStringList applicationDirs();
QList<AppEntry> scanDesktopApps(const QStringList &excludeIds);
// Abre o app em um grupo de processos próprio; *pid recebe o id do grupo
// (usado para listar o app em "Abertos" e fechá-lo com tudo o que ele abriu).
bool launchDesktopApp(const AppEntry &app, QString *error, qint64 *pid = nullptr);

// image://appicon/<nome-do-ícone ou caminho>
class AppIconProvider : public QQuickImageProvider
{
public:
    AppIconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

void setupIconTheme();
