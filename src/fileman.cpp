#include "fileman.h"
#include "library.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QLocale>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QUrl>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {

const QStringList kImage{"png", "jpg", "jpeg", "webp", "gif", "bmp", "svg", "avif", "tiff", "tif"};
const QStringList kVideo{"mp4", "mkv", "webm", "avi", "mov", "m4v", "wmv", "flv", "ts"};
const QStringList kAudio{"mp3", "flac", "ogg", "opus", "wav", "m4a", "aac", "wma"};
const QStringList kArchive{"zip", "7z", "rar", "tar", "gz", "xz", "bz2", "zst", "tgz", "001"};
const QStringList kDoc{"txt", "md", "pdf", "doc", "docx", "odt", "rtf", "csv", "xls", "xlsx", "ods", "json", "xml", "html", "log", "ini", "cfg", "conf", "yml", "yaml", "toml"};
const QStringList kExec{"appimage", "sh", "run", "deb", "flatpak", "flatpakref", "desktop", "exe"};

QString category(const QFileInfo &fi, const QStringList &gameExt)
{
    if (fi.isDir())
        return "dir";
    const QString e = fi.suffix().toLower();
    if (kArchive.contains(e))
        return "archive";  // .zip também é ROM de SNES: o menu X ainda oferece "Jogar"
    if (gameExt.contains(e))
        return "game";
    if (kImage.contains(e))
        return "image";
    if (kVideo.contains(e))
        return "video";
    if (kAudio.contains(e))
        return "audio";
    if (kExec.contains(e) || (fi.isExecutable() && e.isEmpty()))
        return "exec";
    if (kDoc.contains(e))
        return "doc";
    return "other";
}

QString fmtDate(const QDateTime &d)
{
    if (!d.isValid())
        return {};
    const QDate today = QDate::currentDate();
    if (d.date() == today)
        return "hoje, " + d.toString("HH:mm");
    if (d.date() == today.addDays(-1))
        return "ontem, " + d.toString("HH:mm");
    return d.toString("dd/MM/yyyy HH:mm");
}

QVariantMap storageOf(const QString &path)
{
    const QStorageInfo st(path);
    if (!st.isValid() || st.bytesTotal() <= 0)
        return {};
    const qint64 total = st.bytesTotal(), free = st.bytesAvailable();
    return {{"free", free},
            {"total", total},
            {"freeText", FileManager::humanSize(free)},
            {"totalText", FileManager::humanSize(total)},
            {"usedPct", int(100.0 * (total - free) / total)},
            {"root", st.rootPath()},
            {"readOnly", st.isReadOnly()}};
}

QString uniqueTarget(const QString &dir, const QString &name)
{
    QString target = dir + '/' + name;
    if (!QFileInfo::exists(target))
        return target;
    const QFileInfo fi(name);
    // "jogo.iso" -> "jogo (2).iso"; pastas e nomes sem extensão: "pasta (2)"
    const QString base = fi.completeSuffix().isEmpty() || name.startsWith('.') ? name : fi.baseName();
    const QString ext = base == name ? QString() : name.mid(base.size());
    for (int i = 2; i < 1000; ++i) {
        target = QStringLiteral("%1/%2 (%3)%4").arg(dir, base).arg(i).arg(ext);
        if (!QFileInfo::exists(target))
            return target;
    }
    return {};
}

// ------------------------------------------------------------------ copiar/mover

class PasteJob
{
public:
    FileManager *fm;
    QStringList sources;
    QString dest;
    bool move = false;
    QStringList errors;
    int done = 0;

    void run()
    {
        // tamanho total para a barra de progresso
        qint64 total = 0;
        for (const QString &s : sources) {
            const QFileInfo fi(s);
            if (fi.isDir()) {
                QDirIterator it(s, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    it.next();
                    total += it.fileInfo().size();
                }
            } else {
                total += fi.size();
            }
        }
        m_total = qMax<qint64>(1, total);

        for (const QString &src : sources) {
            if (fm->cancelled())
                break;
            const QFileInfo fi(src);
            if (!fi.exists()) {
                errors << fi.fileName() + ": não existe mais";
                continue;
            }
            const QString cleanDest = QDir::cleanPath(dest), cleanSrc = QDir::cleanPath(fi.absoluteFilePath());
            if (fi.isDir() && (cleanDest == cleanSrc || cleanDest.startsWith(cleanSrc + '/'))) {
                errors << fi.fileName() + ": não dá para colocar uma pasta dentro dela mesma";
                continue;
            }
            if (move && QDir::cleanPath(fi.absolutePath()) == cleanDest)
                continue;  // recortar e colar no mesmo lugar: nada a fazer
            const QString target = uniqueTarget(dest, fi.fileName());
            if (target.isEmpty()) {
                errors << fi.fileName() + ": nome em uso";
                continue;
            }
            QString err;
            bool ok = false;
            if (move && ::rename(QFile::encodeName(src).constData(), QFile::encodeName(target).constData()) == 0)
                ok = true;  // mesmo disco: instantâneo
            else if (fi.isDir())
                ok = copyDir(src, target, &err) && (!move || QDir(src).removeRecursively());
            else
                ok = copyFile(src, target, &err) && (!move || QFile::remove(src));
            if (ok)
                ++done;
            else
                errors << fi.fileName() + ": " + (err.isEmpty() ? QStringLiteral("falhou") : err);
        }
    }

private:
    qint64 m_total = 1, m_copied = 0;
    QElapsedTimer m_tick;

    void report()
    {
        if (m_tick.isValid() && m_tick.elapsed() < 120)
            return;
        m_tick.start();
        fm->postProgress(double(m_copied) / m_total,
                         QStringLiteral("%1 de %2").arg(FileManager::humanSize(m_copied), FileManager::humanSize(m_total)));
    }

    bool copyFile(const QString &src, const QString &dst, QString *err)
    {
        QFile in(src), out(dst);
        if (!in.open(QIODevice::ReadOnly)) {
            *err = in.errorString();
            return false;
        }
        if (!out.open(QIODevice::WriteOnly)) {
            *err = out.errorString();
            return false;
        }
        QByteArray buf(4 * 1024 * 1024, Qt::Uninitialized);
        while (true) {
            if (fm->cancelled()) {
                out.remove();
                *err = "cancelado";
                return false;
            }
            const qint64 n = in.read(buf.data(), buf.size());
            if (n < 0 || (n > 0 && out.write(buf.constData(), n) != n)) {
                out.remove();
                *err = "erro de leitura/escrita (disco cheio?)";
                return false;
            }
            if (n == 0)
                break;
            m_copied += n;
            report();
        }
        out.setPermissions(in.permissions());
        return true;
    }

    bool copyDir(const QString &src, const QString &dst, QString *err)
    {
        if (!QDir().mkpath(dst)) {
            *err = "não foi possível criar a pasta";
            return false;
        }
        QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            const QString target = dst + p.mid(src.size());
            if (it.fileInfo().isDir()) {
                QDir().mkpath(target);
            } else if (!copyFile(p, target, err)) {
                QDir(dst).removeRecursively();
                return false;
            }
        }
        return true;
    }
};

}  // namespace

// ---------------------------------------------------------------------------

FileManager::FileManager(Library *library, QObject *parent) : QObject(parent), m_lib(library) {}

FileManager::~FileManager()
{
    if (m_thread) {
        m_cancel = true;
        m_thread->wait(10000);
    }
}

QString FileManager::humanSize(qint64 b)
{
    if (b < 1024)
        return QStringLiteral("%1 B").arg(b);
    const char *u[] = {"KB", "MB", "GB", "TB"};
    double v = b;
    int i = -1;
    while (v >= 1024 && i < 3) {
        v /= 1024;
        ++i;
    }
    return QStringLiteral("%1 %2").arg(QLocale(QLocale::Portuguese, QLocale::Brazil).toString(v, 'f', v < 10 ? 1 : 0), u[i]);
}

QString FileManager::trashPath() const
{
    QString base = qEnvironmentVariable("XDG_DATA_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + "/.local/share";
    return base + "/Trash/files";
}

bool FileManager::isProtected(const QString &path)
{
    const QString p = QDir::cleanPath(path);
    const QString home = QDir::homePath();
    if (p == "/" || p == home || p.count('/') <= 1)
        return true;
    for (auto loc : {QStandardPaths::DownloadLocation, QStandardPaths::DocumentsLocation,
                     QStandardPaths::DesktopLocation, QStandardPaths::MusicLocation,
                     QStandardPaths::PicturesLocation, QStandardPaths::MoviesLocation})
        if (QDir::cleanPath(QStandardPaths::writableLocation(loc)) == p)
            return true;
    for (const char *sub : {"/ROMs", "/emulators", "/.config", "/.local", "/.local/share"})
        if (p == home + QLatin1String(sub))
            return true;
    return QStorageInfo(p).rootPath() == p;  // raiz de um disco
}

// Só altera arquivos na pasta pessoal e em discos externos.
bool FileManager::isWritableArea(const QString &path)
{
    const QString p = QDir::cleanPath(path);
    for (const QString &base : {QDir::homePath(), QStringLiteral("/media"), QStringLiteral("/run/media"),
                                QStringLiteral("/mnt")})
        if (p.startsWith(base + '/'))
            return true;
    return false;
}

QVariantList FileManager::places() const
{
    QVariantList out;
    auto add = [&](const QString &label, const QString &icon, const QString &path, bool disk = false) {
        if (path.isEmpty() || !QFileInfo(path).isDir())
            return;
        QVariantMap m{{"label", label}, {"icon", icon}, {"path", path}, {"disk", disk}};
        if (disk)
            m.insert("storage", storageOf(path));
        out << m;
    };
    const QString home = QDir::homePath();
    add("Pasta pessoal", "⌂", home);
    add("Downloads", "↓", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    add("Documentos", "▤", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    add("Imagens", "▧", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    add("Vídeos", "▶", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    add("Músicas", "♪", QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    add("Jogos (ROMs)", "◉", home + "/ROMs");
    add("Emuladores", "⚙", home + "/emulators");
    QDir().mkpath(trashPath());
    add("Lixeira", "✕", trashPath());
    add("Sistema", "■", "/", true);
    if (QStorageInfo(home).rootPath() != "/")
        add("Disco da pasta pessoal", "■", QStorageInfo(home).rootPath(), true);
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
        const QString root = v.rootPath();
        if (!v.isValid() || !v.isReady())
            continue;
        const bool removable = root.startsWith("/media/") || root.startsWith("/run/media/");
        const bool mnt = root.startsWith("/mnt/") && root.count('/') == 2;
        if (removable || mnt)
            add(v.displayName().isEmpty() ? QFileInfo(root).fileName() : v.displayName(), "▣", root, true);
    }
    return out;
}

QVariantList FileManager::gameSystems(const QString &path) const
{
    QVariantList out;
    const QFileInfo fi(path);
    for (const SystemDef &s : m_lib->systemDefs()) {
        bool match = false;
        if (fi.isDir()) {
            for (const QString &m : s.folderMarkers)
                if (QFileInfo(path + '/' + m).isFile())
                    match = true;
        } else {
            match = s.extensions.contains(fi.suffix().toLower());
        }
        if (match)
            out << QVariantMap{{"id", s.id}, {"name", s.name}, {"path", path}};
    }
    return out;
}

QVariantMap FileManager::list(const QString &path, bool showHidden, const QString &sort) const
{
    const QDir d(path);
    if (!d.exists())
        return {{"error", QStringLiteral("A pasta %1 não existe.").arg(tildePath(path))}};
    if (!d.isReadable())
        return {{"error", QStringLiteral("Sem permissão para abrir %1.").arg(tildePath(path))}};

    QStringList gameExt;
    for (const SystemDef &s : m_lib->systemDefs())
        gameExt << s.extensions;

    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
    if (showHidden)
        filters |= QDir::Hidden;
    QDir::SortFlags order = QDir::DirsFirst | QDir::IgnoreCase;
    order |= sort == "date" ? QDir::Time : sort == "size" ? QDir::Size : QDir::Name;
    const QFileInfoList infos = d.entryInfoList(filters, order);

    QVariantList entries;
    const bool countChildren = infos.size() < 1500;
    for (const QFileInfo &fi : infos) {
        QVariantMap e{{"name", fi.fileName()},
                      {"path", fi.absoluteFilePath()},
                      {"isDir", fi.isDir()},
                      {"kind", category(fi, gameExt)},
                      {"ext", fi.isDir() ? QString() : fi.suffix().toUpper().left(4)},
                      {"hidden", fi.isHidden()},
                      {"link", fi.isSymLink()},
                      {"modified", fmtDate(fi.lastModified())}};
        if (fi.isDir()) {
            if (countChildren) {
                const int n = QDir(fi.absoluteFilePath()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
                e.insert("sizeText", n == 0 ? QStringLiteral("vazia") : n == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 itens").arg(n));
            }
        } else {
            e.insert("sizeText", humanSize(fi.size()));
        }
        entries << e;
    }

    QString parent;
    QDir up(d.absolutePath());
    if (up.cdUp() && up.absolutePath() != d.absolutePath())
        parent = up.absolutePath();
    return {{"path", d.absolutePath()},
            {"display", tildePath(d.absolutePath())},
            {"name", d.absolutePath() == "/" ? QStringLiteral("Sistema")
                     : d.absolutePath() == QDir::homePath() ? QStringLiteral("Pasta pessoal")
                     : QDir::cleanPath(d.absolutePath()) == QDir::cleanPath(trashPath()) ? QStringLiteral("Lixeira")
                                                                                       : d.dirName()},
            {"parent", parent},
            {"entries", entries},
            {"storage", storageOf(d.absolutePath())},
            {"isTrash", QDir::cleanPath(d.absolutePath()) == QDir::cleanPath(trashPath())},
            {"writable", QFileInfo(d.absolutePath()).isWritable() && isWritableArea(d.absolutePath() + "/x")}};
}

QVariantMap FileManager::details(const QString &path) const
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return {};
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(fi);
    static const QHash<QString, QString> kindName{{"image", "Imagem"}, {"video", "Vídeo"}, {"audio", "Áudio"},
                                                 {"archive", "Arquivo compactado"}, {"game", "Jogo / imagem de disco"},
                                                 {"doc", "Documento"}, {"exec", "Programa"}, {"other", "Arquivo"}};
    QStringList gameExt;
    for (const SystemDef &s : m_lib->systemDefs())
        gameExt << s.extensions;
    const QString kind = category(fi, gameExt);
    QString type = fi.isDir() ? QStringLiteral("Pasta") : mime.comment();
    if (type.isEmpty() || mime.isDefault())
        type = kindName.value(kind) + (fi.suffix().isEmpty() ? QString() : " " + fi.suffix().toUpper());
    QVariantMap m{{"name", fi.fileName()},
                  {"path", fi.absoluteFilePath()},
                  {"location", tildePath(fi.absolutePath())},
                  {"isDir", fi.isDir()},
                  {"type", type},
                  {"modified", fmtDate(fi.lastModified())},
                  {"access", fi.isWritable() ? QStringLiteral("Leitura e escrita") : QStringLiteral("Somente leitura")},
                  {"owner", fi.owner()},
                  {"url", QUrl::fromLocalFile(fi.absoluteFilePath()).toString()},
                  {"image", false}};
    if (fi.isDir()) {
        const int n = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).size();
        m.insert("size", QStringLiteral("%1 itens").arg(n));
    } else {
        m.insert("size", humanSize(fi.size()) + (fi.size() >= 1024 ? QStringLiteral(" (%1 bytes)").arg(QLocale(QLocale::Portuguese, QLocale::Brazil).toString(fi.size())) : QString()));
        if ((mime.name().startsWith("image/") || kind == "image") && fi.size() < 60 * 1024 * 1024) {
            const QSize sz = QImageReader(path).size();
            m.insert("image", true);
            if (sz.isValid())
                m.insert("dimensions", QStringLiteral("%1 × %2").arg(sz.width()).arg(sz.height()));
        }
    }
    return m;
}

bool FileManager::open(const QString &path)
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString FileManager::rename(const QString &path, const QString &newName)
{
    const QString name = newName.trimmed();
    if (name.isEmpty() || name.contains('/') || name == "." || name == "..")
        return QStringLiteral("Nome inválido.");
    if (isProtected(path) || !isWritableArea(path))
        return QStringLiteral("Esta pasta não pode ser renomeada.");
    const QFileInfo fi(path);
    const QString target = fi.absolutePath() + '/' + name;
    if (target == fi.absoluteFilePath())
        return {};
    if (QFileInfo::exists(target))
        return QStringLiteral("Já existe \"%1\" aqui.").arg(name);
    if (!QFile::rename(path, target))
        return QStringLiteral("Não foi possível renomear (sem permissão?).");
    return {};
}

QString FileManager::makeDir(const QString &dir, const QString &name)
{
    const QString n = name.trimmed().isEmpty() ? QStringLiteral("Nova pasta") : name.trimmed();
    if (n.contains('/'))
        return QStringLiteral("Nome inválido.");
    if (!isWritableArea(dir + "/x"))
        return QStringLiteral("Não é possível criar pastas aqui.");
    const QString target = uniqueTarget(dir, n);
    if (target.isEmpty() || !QDir().mkdir(target))
        return QStringLiteral("Não foi possível criar a pasta (sem permissão?).");
    return {};
}

QString FileManager::trash(const QStringList &paths)
{
    QStringList failed;
    for (const QString &p : paths) {
        if (isProtected(p) || !isWritableArea(p)) {
            failed << QFileInfo(p).fileName() + " (protegido)";
            continue;
        }
        if (!QFile::moveToTrash(p))
            failed << QFileInfo(p).fileName();
    }
    if (failed.isEmpty())
        return {};
    return QStringLiteral("Não foi para a lixeira: %1. Em discos externos use \"Apagar\".").arg(failed.join(", "));
}

QString FileManager::remove(const QStringList &paths)
{
    QStringList failed;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (isProtected(p) || !isWritableArea(p)) {
            failed << fi.fileName() + " (protegido)";
            continue;
        }
        const bool ok = fi.isDir() && !fi.isSymLink() ? QDir(p).removeRecursively() : QFile::remove(p);
        if (!ok)
            failed << fi.fileName();
    }
    return failed.isEmpty() ? QString() : QStringLiteral("Não foi possível apagar: %1").arg(failed.join(", "));
}

QString FileManager::emptyTrash()
{
    const QString base = QFileInfo(trashPath()).absolutePath();  // .../Trash
    bool ok = true;
    for (const char *sub : {"files", "info"}) {
        const QDir d(base + '/' + QLatin1String(sub));
        for (const QFileInfo &fi : d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            ok &= fi.isDir() && !fi.isSymLink() ? QDir(fi.absoluteFilePath()).removeRecursively()
                                                : QFile::remove(fi.absoluteFilePath());
    }
    return ok ? QString() : QStringLiteral("Alguns itens não puderam ser apagados.");
}

QString FileManager::paste(const QStringList &paths, const QString &destDir, bool move)
{
    if (m_thread)
        return QStringLiteral("Já existe uma cópia em andamento.");
    if (paths.isEmpty())
        return QStringLiteral("Nada para colar.");
    if (!isWritableArea(destDir + "/x") || !QFileInfo(destDir).isWritable())
        return QStringLiteral("Não é possível colar aqui.");
    if (move)
        for (const QString &p : paths)
            if (isProtected(p) || !isWritableArea(p))
                return QStringLiteral("%1 não pode ser movido.").arg(QFileInfo(p).fileName());

    auto job = std::make_shared<PasteJob>();
    job->fm = this;
    job->sources = paths;
    job->dest = destDir;
    job->move = move;
    m_cancel = false;
    m_progress = 0;
    m_status = move ? QStringLiteral("Movendo…") : QStringLiteral("Copiando…");
    emit progressChanged();
    m_thread = QThread::create([job] { job->run(); });
    connect(m_thread, &QThread::finished, this, [this, job, move, destDir] {
        m_thread->deleteLater();
        m_thread = nullptr;
        QString msg = job->errors.isEmpty()
                          ? QStringLiteral("%1 %2 item(ns).").arg(move ? "Movido(s)" : "Copiado(s)").arg(job->done)
                          : QStringLiteral("%1 de %2 concluído(s). Problemas: %3")
                                .arg(job->done).arg(job->sources.size()).arg(job->errors.mid(0, 3).join("; "));
        if (m_cancel)
            msg = QStringLiteral("Cancelado (%1 concluído(s)).").arg(job->done);
        emit busyChanged();
        emit finished(msg, destDir);
    });
    m_thread->start();
    emit busyChanged();
    return {};
}

void FileManager::postProgress(double p, const QString &status)
{
    QMetaObject::invokeMethod(this, [this, p, status] {
        m_progress = p;
        m_status = status;
        emit progressChanged();
    }, Qt::QueuedConnection);
}
