#include "apps.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QImageReader>
#include <QLocale>
#include <QMutex>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

namespace {

QStringList dataDirs()
{
    QStringList dirs;
    QString home = qEnvironmentVariable("XDG_DATA_HOME");
    if (home.isEmpty())
        home = QDir::homePath() + "/.local/share";
    dirs << home;
    QString sys = qEnvironmentVariable("XDG_DATA_DIRS");
    if (sys.isEmpty())
        sys = "/usr/local/share:/usr/share";
    dirs << sys.split(':', Qt::SkipEmptyParts);
    // Exportações do Flatpak (normalmente já estão no XDG_DATA_DIRS da sessão).
    dirs << home + "/flatpak/exports/share" << "/var/lib/flatpak/exports/share";
    dirs.removeDuplicates();
    return dirs;
}

QString unescape(const QString &v)
{
    QString out;
    for (int i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            const QChar n = v[++i];
            out += n == 's' ? QChar(' ') : n == 'n' ? QChar('\n') : n == 't' ? QChar('\t') : n;
        } else {
            out += v[i];
        }
    }
    return out;
}

bool truthy(const QString &v) { return v.trimmed().compare("true", Qt::CaseInsensitive) == 0; }

// Divide a linha Exec conforme a especificação (aspas duplas com escapes \ ).
// Aspas simples não são da especificação, mas aparecem em atalhos feitos à mão.
QStringList splitExec(const QString &s)
{
    QStringList args;
    QString cur;
    QChar quote;
    bool has = false;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (!quote.isNull()) {
            if (quote == '"' && c == '\\' && i + 1 < s.size())
                cur += s[++i];
            else if (c == quote)
                quote = QChar();
            else
                cur += c;
        } else if (c == '"' || c == '\'') {
            quote = c;
            has = true;
        } else if (c.isSpace()) {
            if (has || !cur.isEmpty())
                args << cur;
            cur.clear();
            has = false;
        } else {
            cur += c;
        }
    }
    if (has || !cur.isEmpty())
        args << cur;
    return args;
}

QHash<QString, QImage> g_iconCache;
QMutex g_iconMutex;

QImage findIconFile(const QString &name, int size)
{
    QStringList bases;
    for (const QString &d : dataDirs())
        bases << d + "/icons";
    bases << QDir::homePath() + "/.icons";
    static const QStringList sizes{"scalable", "512x512", "256x256", "192x192", "128x128", "96x96",
                                   "64x64",    "48x48",   "32x32"};
    static const QStringList exts{"svg", "png", "xpm"};
    QStringList candidates;
    for (const QString &b : bases)
        for (const QString &sz : sizes)
            for (const QString &e : exts)
                candidates << b + "/hicolor/" + sz + "/apps/" + name + '.' + e;
    for (const QString &e : exts)
        candidates << "/usr/share/pixmaps/" + name + '.' + e;
    for (const QString &c : candidates) {
        if (!QFileInfo::exists(c))
            continue;
        QImageReader r(c);
        if (c.endsWith(".svg"))
            r.setScaledSize(QSize(size, size));
        const QImage img = r.read();
        if (!img.isNull())
            return img;
    }
    return {};
}

}  // namespace

QStringList applicationDirs()
{
    QStringList out;
    for (const QString &d : dataDirs())
        out << d + "/applications";
    return out;
}

QList<AppEntry> scanDesktopApps(const QStringList &excludeIds)
{
    const QString lang = QLocale::system().name();          // pt_BR
    const QString shortLang = lang.section('_', 0, 0);       // pt
    QSet<QString> seen;
    QList<AppEntry> apps;

    for (const QString &dir : applicationDirs()) {
        QDirIterator it(dir, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            // ID do aplicativo: caminho relativo com "/" trocado por "-"
            const QString id = path.mid(dir.size() + 1).replace('/', '-');
            if (seen.contains(id))
                continue;  // uma pasta de maior prioridade já definiu este app
            seen.insert(id);

            QFile f(path);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            QHash<QString, QString> kv;
            bool inMain = false;
            QTextStream ts(&f);
            while (!ts.atEnd()) {
                const QString line = ts.readLine().trimmed();
                if (line.startsWith('[')) {
                    inMain = line == "[Desktop Entry]";
                    continue;
                }
                if (!inMain || line.startsWith('#'))
                    continue;
                const int eq = line.indexOf('=');
                if (eq > 0)
                    kv.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
            }
            if (kv.value("Type") != "Application" || truthy(kv.value("NoDisplay"))
                || truthy(kv.value("Hidden")) || truthy(kv.value("Terminal")) || kv.value("Exec").isEmpty()
                || excludeIds.contains(id))
                continue;
            const QString tryExec = kv.value("TryExec");
            if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty()
                && !QFileInfo(tryExec).isExecutable())
                continue;

            auto localized = [&](const QString &key) {
                for (const QString &k : {key + '[' + lang + ']', key + '[' + shortLang + ']', key})
                    if (kv.contains(k))
                        return unescape(kv.value(k));
                return QString();
            };
            AppEntry a;
            a.id = id;
            a.name = localized("Name");
            a.comment = localized("Comment");
            if (a.comment.isEmpty())
                a.comment = localized("GenericName");
            a.icon = kv.value("Icon");
            a.exec = unescape(kv.value("Exec"));
            a.workDir = kv.value("Path");
            a.file = path;
            if (!a.name.isEmpty())
                apps << a;
        }
    }

    QCollator coll;
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    coll.setNumericMode(true);
    std::sort(apps.begin(), apps.end(), [&](const AppEntry &a, const AppEntry &b) {
        return coll.compare(a.name, b.name) < 0;
    });
    return apps;
}

bool launchDesktopApp(const AppEntry &app, QString *error, qint64 *pid)
{
    QStringList args;
    for (const QString &tok : splitExec(app.exec)) {
        if (tok.size() == 2 && tok[0] == '%') {
            const QChar c = tok[1];
            if (c == 'c')
                args << app.name;
            else if (c == 'k')
                args << app.file;
            else if (c == '%')
                args << "%";
            continue;  // %f %F %u %U %i etc.: nada a passar
        }
        QString t = tok;
        t.replace(QRegularExpression("%[fFuUdDnNickvm]"), QString());
        t.replace("%%", "%");
        if (!t.isEmpty())
            args << t;
    }
    if (args.isEmpty()) {
        *error = QStringLiteral("linha Exec inválida em %1").arg(app.file);
        return false;
    }
    const QString program = args.takeFirst();
    QString workDir = app.workDir;
    if (workDir.isEmpty() || !QFileInfo(workDir).isDir())
        workDir = QDir::homePath();
    // setsid: o app fica num grupo de processos só dele
    const QString setsid = QStandardPaths::findExecutable("setsid");
    qint64 started = 0;
    const bool ok = setsid.isEmpty() ? QProcess::startDetached(program, args, workDir, &started)
                                     : QProcess::startDetached(setsid, QStringList{program} + args, workDir, &started);
    if (pid)
        *pid = started;
    if (!ok) {
        *error = QStringLiteral("não foi possível abrir %1").arg(app.name);
        return false;
    }
    return true;
}

void setupIconTheme()
{
    QStringList paths = QIcon::themeSearchPaths();
    for (const QString &d : dataDirs())
        paths << d + "/icons";
    paths.removeDuplicates();
    QIcon::setThemeSearchPaths(paths);
    QIcon::setFallbackThemeName("hicolor");
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == "hicolor") {
        for (const QString &t : {"Papirus", "breeze", "Yaru", "Adwaita"}) {
            for (const QString &p : paths)
                if (QFileInfo::exists(p + '/' + t + "/index.theme")) {
                    QIcon::setThemeName(t);
                    return;
                }
        }
        QIcon::setThemeName("hicolor");
    }
}

AppIconProvider::AppIconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage AppIconProvider::requestImage(const QString &rawId, QSize *size, const QSize &requestedSize)
{
    const QString id = QUrl::fromPercentEncoding(rawId.toUtf8());
    const int px = requestedSize.width() > 0 ? requestedSize.width() : 256;
    const QString cacheKey = id + '@' + QString::number(px);
    {
        QMutexLocker lock(&g_iconMutex);
        if (g_iconCache.contains(cacheKey)) {
            const QImage img = g_iconCache.value(cacheKey);
            if (size)
                *size = img.size();
            return img;
        }
    }

    QImage img;
    if (QDir::isAbsolutePath(id)) {
        QImageReader r(id);
        if (id.endsWith(".svg", Qt::CaseInsensitive))
            r.setScaledSize(QSize(px, px));
        img = r.read();
    } else if (!id.isEmpty()) {
        const QIcon icon = QIcon::fromTheme(id);
        if (!icon.isNull())
            img = icon.pixmap(px, px).toImage();
        if (img.isNull() || img.width() < 8)
            img = findIconFile(id, px);
    }
    if (!img.isNull() && (img.width() > px || img.height() > px))
        img = img.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QMutexLocker lock(&g_iconMutex);
    g_iconCache.insert(cacheKey, img);
    if (size)
        *size = img.size();
    return img;
}
