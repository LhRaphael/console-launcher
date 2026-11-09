#include "importer.h"
#include "library.h"
#include "sfo.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QUuid>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {

const QStringList kArchiveExt{"zip", "7z", "rar", "tar", "tgz", "txz", "tbz2", "gz", "xz", "bz2", "zst"};
const QStringList kJunkExt{"txt", "nfo", "diz", "url", "lnk", "sfv", "md5", "sha1", "sha256", "crc",
                           "htm", "html", "torrent", "website"};
const QStringList kJunkNames{"thumbs.db", "desktop.ini", ".ds_store"};
const QStringList kImageExt{"png", "jpg", "jpeg", "webp"};
// em pastas escolhidas para importar, também são restos depois que os jogos saem
const QStringList kLeftoverExt{"jpg", "jpeg", "png", "webp", "gif", "bmp", "pdf"};

QString humanSize(qint64 b)
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

// Partes 2, 3… de arquivos divididos (.7z.002, .part2.rar, .z01, .r00): só a primeira é processada.
bool isSecondaryPart(const QString &name)
{
    static const QRegularExpression num(R"(\.(\d{3})$)");
    static const QRegularExpression part(R"(\.part0*(\d+)\.rar$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression zr(R"(\.(z|r)\d\d$)", QRegularExpression::CaseInsensitiveOption);
    auto m = num.match(name);
    if (m.hasMatch())
        return m.captured(1).toInt() != 1;
    m = part.match(name);
    if (m.hasMatch())
        return m.captured(1).toInt() != 1;
    return zr.match(name).hasMatch();
}

bool isArchive(const QString &name)
{
    const QString n = name.toLower();
    if (isSecondaryPart(n))
        return false;
    if (n.endsWith(".001"))
        return true;
    for (const QString &e : kArchiveExt)
        if (n.endsWith('.' + e))
            return true;
    return false;
}

bool isTarLike(const QString &name)
{
    const QString n = name.toLower();
    for (const char *e : {".tar", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2", ".tar.zst"})
        if (n.endsWith(QLatin1String(e)))
            return true;
    return false;
}

// "Jogo (USA).7z.001" -> "Jogo (USA)"
QString archiveStem(const QString &name)
{
    QString s = name;
    s.remove(QRegularExpression(R"(\.(\d{3})$)"));
    s.remove(QRegularExpression(R"(\.part0*\d+\.rar$)", QRegularExpression::CaseInsensitiveOption));
    s.remove(QRegularExpression(R"(\.(tar\.)?(zip|7z|rar|tar|tgz|txz|tbz2|gz|xz|bz2|zst)$)",
                                QRegularExpression::CaseInsensitiveOption));
    return s;
}

// Todas as partes de um arquivo dividido (para apagar depois da extração).
QStringList archiveParts(const QString &path)
{
    const QFileInfo fi(path);
    const QString name = fi.fileName();
    const QDir dir = fi.dir();
    QStringList out{path};
    QRegularExpression re;
    if (QRegularExpression(R"(\.001$)").match(name).hasMatch())
        re.setPattern('^' + QRegularExpression::escape(name.chopped(4)) + R"(\.\d{3}$)");
    else if (QRegularExpression(R"(\.part0*1\.rar$)", QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())
        re.setPattern('^' + QRegularExpression::escape(archiveStem(name)) + R"(\.part\d+\.rar$)");
    else if (name.endsWith(".zip", Qt::CaseInsensitive))
        re.setPattern('^' + QRegularExpression::escape(name.chopped(4)) + R"(\.z\d\d$)");
    else if (name.endsWith(".rar", Qt::CaseInsensitive))
        re.setPattern('^' + QRegularExpression::escape(name.chopped(4)) + R"(\.r\d\d$)");
    if (re.pattern().isEmpty())
        return out;
    re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    for (const QFileInfo &f : dir.entryInfoList(QDir::Files))
        if (f.absoluteFilePath() != path && re.match(f.fileName()).hasMatch())
            out << f.absoluteFilePath();
    return out;
}

QString findCaseInsensitive(const QDir &dir, const QString &rel)
{
    const QString direct = QDir::cleanPath(dir.absoluteFilePath(rel));
    if (QFileInfo::exists(direct))
        return direct;
    const QFileInfo want(direct);
    for (const QFileInfo &f : QDir(want.absolutePath()).entryInfoList(QDir::Files))
        if (f.fileName().compare(want.fileName(), Qt::CaseInsensitive) == 0)
            return f.absoluteFilePath();
    return {};
}

// Arquivos de que um jogo depende: faixas do .cue, discos do .m3u, .img/.sub do .ccd…
QStringList companions(const QString &primary, QHash<QString, QString> *rename)
{
    QStringList out;
    QSet<QString> seen{QDir::cleanPath(primary)};
    std::function<void(const QString &)> add = [&](const QString &p) {
        const QFileInfo fi(p);
        const QString ext = fi.suffix().toLower();
        const QDir base = fi.dir();
        auto push = [&](const QString &rel) {
            const QString abs = findCaseInsensitive(base, rel);
            if (abs.isEmpty() || seen.contains(abs) || !QFileInfo(abs).isFile())
                return;
            seen.insert(abs);
            // .cue pede "Jogo (Track 1).bin" mas o arquivo é "jogo (track 1).bin": no Linux
            // maiúsculas importam, então o arquivo é renomeado para o nome pedido.
            const QString wanted = QFileInfo(rel).fileName();
            if (rename && QFileInfo(abs).fileName() != wanted)
                rename->insert(abs, wanted);
            out << abs;
            add(abs);
        };
        if (ext == "cue" || ext == "m3u") {
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text) || f.size() > 1024 * 1024)
                return;
            static const QRegularExpression fileRe(R"re(^\s*FILE\s+(?:"([^"]+)"|(\S+))\s)re",
                                                   QRegularExpression::CaseInsensitiveOption);
            while (!f.atEnd()) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (ext == "m3u") {
                    if (!line.isEmpty() && !line.startsWith('#'))
                        push(line);
                } else {
                    const auto m = fileRe.match(line + ' ');
                    if (m.hasMatch())
                        push(m.captured(1).isEmpty() ? m.captured(2) : m.captured(1));
                }
            }
        } else if (ext == "ccd") {
            push(fi.completeBaseName() + ".img");
            push(fi.completeBaseName() + ".sub");
        } else if (ext == "mds") {
            push(fi.completeBaseName() + ".mdf");
        }
    };
    add(primary);
    // capa com o mesmo nome do jogo
    const QFileInfo pi(primary);
    for (const QString &e : kImageExt) {
        const QString img = findCaseInsensitive(pi.dir(), pi.completeBaseName() + '.' + e);
        if (!img.isEmpty() && !seen.contains(img)) {
            seen.insert(img);
            out << img;
            break;
        }
    }
    return out;
}

// "Final Fantasy VII (USA) (Disc 2)" -> chave "Final Fantasy VII (USA)", disco 2
bool discInfo(const QString &base, QString *key, int *disc)
{
    static const QRegularExpression re(R"(\s*[\(\[]\s*(?:disc|disk|disco|cd)\s*(\d+)(?:\s*(?:of|de)\s*\d+)?\s*[\)\]])",
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(base);
    if (!m.hasMatch())
        return false;
    *disc = m.captured(1).toInt();
    QString k = base;
    k.remove(m.capturedStart(), m.capturedLength());
    *key = k.simplified();
    return true;
}

QString uniqueName(const QString &name)
{
    QString n = name;
    n.replace('/', '-');
    return n.trimmed().isEmpty() ? QStringLiteral("jogo") : n.trimmed();
}

// --------------------------------------------------------------------------

class ImportJob
{
public:
    Importer *imp;
    SystemDef sys;
    QString dest;
    QStringList sources;
    bool cleanup = true;

    QVariantList imported, skipped;
    QStringList deleted;
    qint64 freed = 0;
    int failures = 0;
    bool wasCancelled = false;

    void run();

private:
    int index = 0, total = 1;
    QString current;
    QSet<QString> movedFrom;  // pastas de onde saíram jogos (para limpar)
    QHash<QString, QString> renames;  // arquivo -> nome pedido pelo .cue/.m3u
    QElapsedTimer lastPost;

    QStringList gameExt() const
    {
        QStringList out;
        for (const QString &e : sys.extensions)
            if (!kArchiveExt.contains(e.toLower()))
                out << e.toLower();
        return out;
    }
    bool isGame(const QFileInfo &fi) const { return gameExt().contains(fi.suffix().toLower()); }

    void progress(double frac, const QString &status, bool force = false)
    {
        if (!force && lastPost.isValid() && lastPost.elapsed() < 120)
            return;
        lastPost.start();
        imp->postProgress((index + qBound(0.0, frac, 1.0)) / total, status, current);
    }
    void skip(const QString &name, const QString &reason, bool failure = true)
    {
        skipped << QVariantMap{{"name", name}, {"reason", reason}};
        if (failure)
            ++failures;
    }
    bool cancelled() const { return imp->cancelled(); }

    bool moveFile(const QString &src, const QString &dst, QString *err);
    bool moveDir(const QString &src, const QString &dst, QString *err);
    bool copyFile(const QString &src, const QString &dst, qint64 *done, qint64 total, QString *err);
    bool extract(const QString &archive, const QString &out, QString *err);
    bool runTool(const QString &program, const QStringList &args, bool parsePercent, QString *output);

    int processArchive(const QString &path, int depth);
    int processDir(const QString &dir, bool temp, const QString &hint, int depth);
    int importLoose(const QStringList &files);
    bool moveGame(const QString &primary, const QStringList &comps, const QString &destDir,
                  const QString &title);
    void cleanupTree(const QString &root);
    QString removeFile(const QString &path);
};

bool ImportJob::copyFile(const QString &src, const QString &dst, qint64 *done, qint64 totalBytes, QString *err)
{
    QFile in(src), out(dst);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
        *err = QStringLiteral("não foi possível copiar %1").arg(QFileInfo(src).fileName());
        return false;
    }
    QByteArray buf(4 * 1024 * 1024, Qt::Uninitialized);
    while (true) {
        if (cancelled()) {
            out.remove();
            *err = QStringLiteral("cancelado");
            return false;
        }
        const qint64 n = in.read(buf.data(), buf.size());
        if (n < 0 || (n > 0 && out.write(buf.constData(), n) != n)) {
            out.remove();
            *err = QStringLiteral("erro ao copiar (disco cheio?)");
            return false;
        }
        if (n == 0)
            break;
        *done += n;
        progress(0.5 + 0.45 * double(*done) / qMax<qint64>(1, totalBytes),
                 QStringLiteral("Copiando… %1 de %2").arg(humanSize(*done), humanSize(totalBytes)));
    }
    out.setPermissions(in.permissions());
    return out.flush();
}

bool ImportJob::moveFile(const QString &src, const QString &dst, QString *err)
{
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (::rename(QFile::encodeName(src).constData(), QFile::encodeName(dst).constData()) == 0)
        return true;
    if (errno != EXDEV) {
        *err = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    qint64 done = 0;
    if (!copyFile(src, dst, &done, QFileInfo(src).size(), err))
        return false;
    QFile::remove(src);
    return true;
}

bool ImportJob::moveDir(const QString &src, const QString &dst, QString *err)
{
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (::rename(QFile::encodeName(src).constData(), QFile::encodeName(dst).constData()) == 0)
        return true;
    if (errno != EXDEV) {
        *err = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    // outro disco (pendrive → HD): copia tudo e só então apaga a origem
    QStringList files;
    qint64 totalBytes = 0;
    QDirIterator it(src, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files << it.next();
        totalBytes += it.fileInfo().size();
    }
    qint64 done = 0;
    for (const QString &f : files) {
        const QString target = dst + f.mid(src.size());
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!copyFile(f, target, &done, totalBytes, err)) {
            QDir(dst).removeRecursively();
            return false;
        }
    }
    QDir(src).removeRecursively();
    return true;
}

bool ImportJob::runTool(const QString &program, const QStringList &args, bool parsePercent, QString *output)
{
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setStandardInputFile(QProcess::nullDevice());  // arquivo com senha falha em vez de travar
    p.start();
    if (!p.waitForStarted(5000)) {
        *output = p.errorString();
        return false;
    }
    static const QRegularExpression pct(R"((\d{1,3})%)");
    QByteArray tail;
    while (p.state() != QProcess::NotRunning) {
        if (cancelled()) {
            p.kill();
            p.waitForFinished(3000);
            *output = QStringLiteral("cancelado");
            return false;
        }
        p.waitForReadyRead(200);
        const QByteArray d = p.readAll();
        if (d.isEmpty())
            continue;
        tail = (tail + d).right(4000);
        if (parsePercent) {
            auto it = pct.globalMatch(QString::fromLatin1(d));
            int last = -1;
            while (it.hasNext())
                last = it.next().captured(1).toInt();
            if (last >= 0)
                progress(0.05 + 0.45 * last / 100.0, QStringLiteral("Extraindo… %1%").arg(last));
        }
    }
    p.waitForFinished();
    tail += p.readAll();
    *output = QString::fromLocal8Bit(tail).trimmed();
    const int code = p.exitCode();
    // 7z: 1 = aviso (ainda extraiu)
    return p.exitStatus() == QProcess::NormalExit && (code == 0 || (parsePercent && code == 1));
}

bool ImportJob::extract(const QString &archive, const QString &out, QString *err)
{
    const QString name = QFileInfo(archive).fileName().toLower();
    struct Cmd {
        QString program;
        QStringList args;
        bool percent;
    };
    QList<Cmd> cmds;
    auto have = [](const QString &p) { return QStandardPaths::findExecutable(p); };

    QString sevenZip;
    for (const char *c : {"7z", "7zz", "7za"})
        if (sevenZip.isEmpty())
            sevenZip = have(c);
    if (isTarLike(name) && !have("tar").isEmpty())
        cmds << Cmd{have("tar"), {"-xf", archive, "-C", out}, false};
    if (!sevenZip.isEmpty() && !isTarLike(name))
        cmds << Cmd{sevenZip, {"x", "-y", "-bsp1", "-bso0", "-bb0", "-o" + out, archive}, true};
    if (!have("bsdtar").isEmpty())
        cmds << Cmd{have("bsdtar"), {"-xf", archive, "-C", out}, false};
    if (name.endsWith(".zip") && !have("unzip").isEmpty())
        cmds << Cmd{have("unzip"), {"-o", "-q", archive, "-d", out}, false};
    if ((name.endsWith(".rar")) && !have("unrar").isEmpty())
        cmds << Cmd{have("unrar"), {"x", "-o+", "-y", "-p-", archive, out + '/'}, false};
    if (!have("unar").isEmpty() && !isTarLike(name))
        cmds << Cmd{have("unar"), {"-q", "-f", "-D", "-o", out, archive}, false};

    if (cmds.isEmpty()) {
        *err = QStringLiteral("nenhum programa de extração instalado (sudo apt install 7zip unar)");
        return false;
    }
    QString output;
    for (const Cmd &c : cmds) {
        progress(0.05, QStringLiteral("Extraindo com %1…").arg(QFileInfo(c.program).fileName()), true);
        if (runTool(c.program, c.args, c.percent, &output) && !QDir(out).isEmpty())
            return true;
        if (cancelled()) {
            *err = QStringLiteral("cancelado");
            return false;
        }
        QDir(out).removeRecursively();  // tentativa parcial: recomeça limpo
        QDir().mkpath(out);
    }
    const QString lastLine = output.section('\n', -1).trimmed();
    if (output.contains("password", Qt::CaseInsensitive) || output.contains("encrypted", Qt::CaseInsensitive))
        *err = QStringLiteral("arquivo protegido por senha");
    else if (output.contains("Unsupported", Qt::CaseInsensitive) && name.endsWith(".rar"))
        *err = QStringLiteral("formato RAR não suportado — instale o unar (sudo apt install unar)");
    else
        *err = QStringLiteral("falha ao extrair%1").arg(lastLine.isEmpty() ? QString() : ": " + lastLine.left(160));
    return false;
}

QString ImportJob::removeFile(const QString &path)
{
    const QFileInfo fi(path);
    const qint64 size = fi.size();
    if (QFile::remove(path)) {
        freed += size;
        deleted << fi.fileName();
        return {};
    }
    return fi.fileName();
}

bool ImportJob::moveGame(const QString &primary, const QStringList &comps, const QString &destDir,
                         const QString &title)
{
    const QString baseDir = QFileInfo(primary).absolutePath();
    QList<QPair<QString, QString>> plan;
    for (const QString &f : QStringList{primary} + comps) {
        QString rel = QDir(baseDir).relativeFilePath(f);
        if (rel.startsWith(".."))
            rel = QFileInfo(f).fileName();
        if (renames.contains(f)) {
            const int slash = rel.lastIndexOf('/');
            rel = rel.left(slash + 1) + renames.value(f);
        }
        plan << qMakePair(f, destDir + '/' + rel);
    }
    const QString name = QFileInfo(primary).fileName();
    for (const auto &p : plan) {
        if (QFileInfo(p.first).absoluteFilePath() == QFileInfo(p.second).absoluteFilePath()) {
            skip(name, QStringLiteral("já está na biblioteca"), false);
            return false;
        }
        if (QFileInfo::exists(p.second)) {
            skip(name, QStringLiteral("já existe %1 na biblioteca (mantido na origem)")
                           .arg(QFileInfo(p.second).fileName()));
            return false;
        }
    }
    for (const auto &p : plan) {
        QString err;
        progress(0.5, QStringLiteral("Movendo %1").arg(QFileInfo(p.first).fileName()));
        if (!moveFile(p.first, p.second, &err)) {
            skip(name, err);
            return false;
        }
        movedFrom.insert(QFileInfo(p.first).absolutePath());
    }
    imported << QVariantMap{{"title", title}, {"file", QFileInfo(plan.first().second).fileName()}};
    return true;
}

int ImportJob::importLoose(const QStringList &files)
{
    QHash<QString, QStringList> comps;
    QSet<QString> referenced;
    for (const QString &f : files) {
        comps[f] = companions(f, &renames);
        for (const QString &c : comps[f])
            referenced.insert(c);
    }
    QStringList primaries;
    for (const QString &f : files)
        if (!referenced.contains(QDir::cleanPath(f)))
            primaries << f;

    int n = 0;
    // Jogos de vários discos sem .m3u: cria a pasta do jogo + playlist (só no PS1, que usa .m3u)
    if (gameExt().contains("m3u")) {
        QMap<QString, QMap<int, QString>> groups;  // chave -> disco -> arquivo
        for (const QString &p : primaries) {
            const QFileInfo fi(p);
            QString key;
            int disc = 0;
            if (fi.suffix().compare("m3u", Qt::CaseInsensitive) != 0
                && discInfo(fi.completeBaseName(), &key, &disc))
                groups[fi.absolutePath() + '\n' + key].insert(disc, p);
        }
        for (auto g = groups.cbegin(); g != groups.cend(); ++g) {
            if (g.value().size() < 2)
                continue;
            const QString key = g.key().section('\n', 1);
            const QString folder = dest + '/' + uniqueName(key);
            const QString m3u = dest + '/' + uniqueName(key) + ".m3u";
            if (QFileInfo::exists(folder) || QFileInfo::exists(m3u)) {
                skip(key, QStringLiteral("já existe na biblioteca (mantido na origem)"));
                for (const QString &p : g.value())
                    primaries.removeAll(p);
                continue;
            }
            QStringList lines;
            bool ok = true;
            QString title;
            splitGameTitle(key, &title, nullptr);
            const int before = imported.size();
            for (const QString &p : g.value()) {
                primaries.removeAll(p);
                if (!ok)
                    continue;
                ok = moveGame(p, comps[p], folder, title);
                lines << uniqueName(key) + '/' + QFileInfo(p).fileName();
            }
            while (imported.size() > before)
                imported.removeLast();  // um item só para o jogo inteiro
            if (ok) {
                QFile f(m3u);
                if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                    f.write((lines.join('\n') + '\n').toUtf8());
                imported << QVariantMap{{"title", title},
                                        {"file", QStringLiteral("%1.m3u (%2 discos)").arg(uniqueName(key)).arg(lines.size())}};
                ++n;
            }
        }
    }

    for (const QString &p : primaries) {
        if (cancelled())
            break;
        QString title;
        splitGameTitle(QFileInfo(p).completeBaseName(), &title, nullptr);
        if (moveGame(p, comps[p], dest, title))
            ++n;
    }
    return n;
}

int ImportJob::processDir(const QString &dir, bool temp, const QString &hint, int depth)
{
    int n = 0;

    // compactados dentro da pasta (inclusive um dentro do outro, até 2 níveis)
    if (depth < 2) {
        QStringList archives;
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (isArchive(it.fileName()) && QDir(dir).relativeFilePath(p).count('/') < 5
                && !gameExt().contains(it.fileInfo().suffix().toLower()))
                archives << p;
        }
        for (const QString &a : archives) {
            if (cancelled())
                return n;
            n += processArchive(a, depth + 1);
        }
    }

    if (!sys.folderMarkers.isEmpty()) {
        // procura as pastas de jogo (PS3_GAME/USRDIR/EBOOT.BIN, eboot.bin…)
        QStringList roots, queue{dir};
        QHash<QString, int> level{{dir, 0}};
        while (!queue.isEmpty()) {
            const QString d = queue.takeFirst();
            bool isRoot = false;
            for (const QString &m : sys.folderMarkers)
                if (QFileInfo(d + '/' + m).isFile())
                    isRoot = true;
            if (isRoot) {
                roots << d;
                continue;
            }
            if (level[d] >= 4)
                continue;
            for (const QFileInfo &sub : QDir(d).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                level[sub.absoluteFilePath()] = level[d] + 1;
                queue << sub.absoluteFilePath();
            }
        }
        for (const QString &root : roots) {
            if (cancelled())
                return n;
            const QString folderName = uniqueName(root == dir ? hint : QFileInfo(root).fileName());
            const QString target = dest + '/' + folderName;
            if (QFileInfo(root).absoluteFilePath() == QFileInfo(target).absoluteFilePath()) {
                skip(folderName, QStringLiteral("já está na biblioteca"), false);
                continue;
            }
            if (QFileInfo::exists(target)) {
                skip(folderName, QStringLiteral("já existe na biblioteca (mantido na origem)"));
                continue;
            }
            QString title;
            for (const QString &sfo : {root + "/PS3_GAME/PARAM.SFO", root + "/PARAM.SFO", root + "/sce_sys/param.sfo"})
                if (title.isEmpty())
                    title = readSfoTitle(sfo);
            if (title.isEmpty())
                splitGameTitle(folderName, &title, nullptr);
            progress(0.5, QStringLiteral("Movendo a pasta do jogo…"), true);
            QString err;
            if (!moveDir(root, target, &err)) {
                skip(folderName, err);
                continue;
            }
            movedFrom.insert(QFileInfo(root).absolutePath());
            imported << QVariantMap{{"title", title}, {"file", folderName + '/'}};
            ++n;
        }
    }

    if (!gameExt().isEmpty() && QFileInfo(dir).isDir()) {
        QStringList files;
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (isGame(it.fileInfo()) && QDir(dir).relativeFilePath(p).count('/') < 5)
                files << p;
        }
        n += importLoose(files);
    }
    Q_UNUSED(temp);
    return n;
}

int ImportJob::processArchive(const QString &path, int depth)
{
    const QFileInfo fi(path);
    const QString name = fi.fileName();
    if (cancelled())
        return 0;

    const QStringList parts = archiveParts(path);
    qint64 size = 0;
    for (const QString &p : parts)
        size += QFileInfo(p).size();
    QStorageInfo storage(dest);
    if (storage.isValid() && storage.bytesAvailable() < size) {
        skip(name, QStringLiteral("espaço insuficiente (precisa de pelo menos %1 livres)").arg(humanSize(size)));
        return 0;
    }

    const QString tmp = dest + "/.importando-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
    QDir().mkpath(tmp);
    QString err;
    if (!extract(path, tmp, &err)) {
        QDir(tmp).removeRecursively();
        if (err == "cancelado")
            wasCancelled = true;
        else
            skip(name, err);
        return 0;
    }
    const int failuresBefore = failures;
    const int n = processDir(tmp, true, archiveStem(name), depth);
    QDir(tmp).removeRecursively();  // o que sobrou da extração é descartado

    if (n > 0 && failures == failuresBefore && !cancelled() && cleanup) {
        QStringList notRemoved;
        for (const QString &p : parts) {
            const QString r = removeFile(p);
            if (!r.isEmpty())
                notRemoved << r;
        }
        if (!notRemoved.isEmpty())
            skip(notRemoved.join(", "), QStringLiteral("jogo importado, mas não consegui apagar o compactado"), false);
        movedFrom.insert(fi.absolutePath());
    } else if (n == 0 && failures == failuresBefore && !cancelled()) {
        skip(name, QStringLiteral("nenhum jogo de %1 dentro do arquivo (mantido)").arg(sys.name));
    }
    return n;
}

void ImportJob::cleanupTree(const QString &root)
{
    if (!QFileInfo(root).isDir())
        return;
    // pastas da mais funda para a mais rasa
    QStringList dirs{root};
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        dirs << it.next();
    std::sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) { return a.count('/') > b.count('/'); });

    for (const QString &d : dirs) {
        for (const QFileInfo &f : QDir(d).entryInfoList(QDir::Files | QDir::Hidden | QDir::System)) {
            const QString ext = f.suffix().toLower();
            if (kJunkExt.contains(ext) || kJunkNames.contains(f.fileName().toLower()) || kLeftoverExt.contains(ext))
                removeFile(f.absoluteFilePath());
        }
        if (QDir(d).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            QDir().rmdir(d);
    }
}

void ImportJob::run()
{
    QDir().mkpath(dest);
    total = qMax(1, int(sources.size()));
    for (index = 0; index < sources.size(); ++index) {
        if (cancelled()) {
            wasCancelled = true;
            break;
        }
        const QFileInfo fi(sources[index]);
        current = fi.fileName();
        movedFrom.clear();
        progress(0, QStringLiteral("Preparando…"), true);
        if (!fi.exists()) {
            skip(current, QStringLiteral("não encontrado"));
            continue;
        }
        const int failuresBefore = failures, skippedBefore = skipped.size();
        int n = 0;
        if (fi.isDir()) {
            n = processDir(fi.absoluteFilePath(), false, fi.fileName(), 0);
            if (n > 0 && cleanup && !cancelled() && failures == failuresBefore)
                cleanupTree(fi.absoluteFilePath());
        } else if (isArchive(fi.fileName())) {
            n = processArchive(fi.absoluteFilePath(), 0);
        } else if (isGame(fi)) {
            n = importLoose({fi.absoluteFilePath()});
        } else {
            skip(current, QStringLiteral("não é um arquivo de %1").arg(sys.name));
            continue;
        }
        if (n == 0 && failures == failuresBefore && skipped.size() == skippedBefore && !cancelled())
            skip(current, QStringLiteral("nenhum jogo de %1 encontrado").arg(sys.name));
        progress(1, QStringLiteral("Concluído"), true);
    }
    if (cancelled())
        wasCancelled = true;
}

}  // namespace

// --------------------------------------------------------------------------

Importer::Importer(Library *library, QObject *parent) : QObject(parent), m_lib(library) {}

Importer::~Importer()
{
    if (m_thread) {
        m_cancel = true;
        m_thread->wait(10000);
    }
}

bool Importer::isProtectedDir(const QString &path)
{
    const QString p = QDir::cleanPath(path);
    const QString home = QDir::homePath();
    if (p == "/" || p == home || !p.startsWith('/') || p.count('/') <= 1)
        return true;
    for (auto loc : {QStandardPaths::DownloadLocation, QStandardPaths::DocumentsLocation,
                     QStandardPaths::DesktopLocation, QStandardPaths::MusicLocation,
                     QStandardPaths::PicturesLocation, QStandardPaths::MoviesLocation,
                     QStandardPaths::PublicShareLocation, QStandardPaths::TemplatesLocation})
        if (QDir::cleanPath(QStandardPaths::writableLocation(loc)) == p)
            return true;
    if (p == home + "/.config" || p == home + "/.local" || p == home + "/emulators" || p == home + "/ROMs")
        return true;
    // raiz de pendrive/HD montado, ou a pasta que reúne os pontos de montagem
    if (QStorageInfo(p).rootPath() == p)
        return true;
    const QString user = qEnvironmentVariable("USER");
    if (p == "/media/" + user || p == "/run/media/" + user || p == "/run/media")
        return true;
    return false;
}

bool Importer::isAllowedSource(const QString &path)
{
    const QString p = QDir::cleanPath(path);
    for (const QString &base : {QDir::homePath(), QStringLiteral("/media"), QStringLiteral("/run/media"),
                                QStringLiteral("/mnt")})
        if (p.startsWith(base + '/'))
            return true;
    return false;
}

QVariantList Importer::places() const
{
    QVariantList out;
    auto add = [&](const QString &label, const QString &path) {
        if (QFileInfo(path).isDir())
            out << QVariantMap{{"label", label}, {"path", path}};
    };
    add("Downloads", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    add("Pasta pessoal", QDir::homePath());
    add("Área de trabalho", QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
        const QString root = v.rootPath();
        if (!v.isValid() || !v.isReady() || v.isReadOnly())
            continue;
        // pendrives/HDs externos montados pelo sistema; em /mnt só os pontos diretos (/mnt/hd)
        const bool removable = root.startsWith("/media/") || root.startsWith("/run/media/");
        const bool mnt = root.startsWith("/mnt/") && root.count('/') == 2;
        if (removable || mnt)
            add(QStringLiteral("Disco: %1").arg(v.displayName().isEmpty() ? QFileInfo(root).fileName() : v.displayName()),
                root);
    }
    return out;
}

QString Importer::startDir(const QString &systemId) const
{
    const QString saved = QSettings().value("importDir/" + systemId).toString();
    if (!saved.isEmpty() && QFileInfo(saved).isDir())
        return saved;
    const QString dl = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return QFileInfo(dl).isDir() ? dl : QDir::homePath();
}

QVariantMap Importer::listDir(const QString &path, const QString &systemId) const
{
    SystemDef sys;
    if (!m_lib->systemDef(systemId, &sys))
        return {{"error", "Console desconhecido"}};
    const QDir d(path);
    if (!d.exists() || !d.isReadable())
        return {{"error", QStringLiteral("Não foi possível abrir %1").arg(tildePath(path))}};

    QStringList gameExt;
    for (const QString &e : sys.extensions)
        if (!kArchiveExt.contains(e.toLower()))
            gameExt << e.toLower();

    QVariantList entries;
    int hidden = 0;
    const QFileInfoList list = d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                               QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : list) {
        if (fi.fileName().startsWith('.'))
            continue;
        if (fi.isDir()) {
            bool root = false;
            for (const QString &m : sys.folderMarkers)
                if (QFileInfo(fi.absoluteFilePath() + '/' + m).isFile())
                    root = true;
            entries << QVariantMap{{"name", fi.fileName()}, {"path", fi.absoluteFilePath()}, {"kind", "dir"},
                                   {"gameRoot", root}, {"protected", isProtectedDir(fi.absoluteFilePath())},
                                   {"sizeText", ""}};
            continue;
        }
        QString kind;
        qint64 size = fi.size();
        if (isSecondaryPart(fi.fileName())) {
            ++hidden;
            continue;
        }
        if (gameExt.contains(fi.suffix().toLower())) {
            kind = "game";
        } else if (isArchive(fi.fileName())) {
            kind = "archive";
            size = 0;
            for (const QString &p : archiveParts(fi.absoluteFilePath()))
                size += QFileInfo(p).size();
        } else {
            ++hidden;
            continue;
        }
        entries << QVariantMap{{"name", fi.fileName()}, {"path", fi.absoluteFilePath()}, {"kind", kind},
                               {"gameRoot", false}, {"protected", false}, {"sizeText", humanSize(size)}};
    }
    QString parent;
    QDir up(d.absolutePath());
    if (up.cdUp() && up.absolutePath() != d.absolutePath())
        parent = up.absolutePath();
    return {{"path", d.absolutePath()},
            {"display", tildePath(d.absolutePath())},
            {"parent", parent},
            {"entries", entries},
            {"hidden", hidden},
            {"isDest", QDir::cleanPath(d.absolutePath()) == QDir::cleanPath(m_lib->systemPath(sys))}};
}

QString Importer::start(const QString &systemId, const QStringList &paths, bool cleanup)
{
    if (m_thread)
        return QStringLiteral("Já existe uma importação em andamento.");
    SystemDef sys;
    if (!m_lib->systemDef(systemId, &sys))
        return QStringLiteral("Console desconhecido.");
    if (paths.isEmpty())
        return QStringLiteral("Nada selecionado.");
    const QString dest = QDir::cleanPath(m_lib->systemPath(sys));

    QStringList sources;
    for (const QString &raw : paths) {
        const QString p = QDir::cleanPath(QFileInfo(raw).absoluteFilePath());
        if (!isAllowedSource(p))
            return QStringLiteral("Só é possível importar da pasta pessoal ou de discos externos (%1).").arg(tildePath(p));
        if (p == dest || dest.startsWith(p + '/'))
            return QStringLiteral("%1 contém a própria biblioteca.").arg(tildePath(p));
        if (QFileInfo(p).isDir() && isProtectedDir(p))
            return QStringLiteral("Escolha os jogos dentro de %1, não a pasta inteira.").arg(tildePath(p));
        sources << p;
    }
    QSettings().setValue("importDir/" + systemId, QFileInfo(sources.first()).absolutePath());

    auto job = std::make_shared<ImportJob>();
    job->imp = this;
    job->sys = sys;
    job->dest = dest;
    job->sources = sources;
    job->cleanup = cleanup;

    m_cancel = false;
    m_progress = 0;
    m_status = QStringLiteral("Preparando…");
    m_item = QFileInfo(sources.first()).fileName();
    emit progressChanged();

    m_thread = QThread::create([job] { job->run(); });
    connect(m_thread, &QThread::finished, this, [this, job] {
        m_thread->deleteLater();
        m_thread = nullptr;
        QStringList names;
        QVariantMap report{{"imported", job->imported},
                           {"skipped", job->skipped},
                           {"deleted", job->deleted.size()},
                           {"freed", job->freed},
                           {"freedText", humanSize(job->freed)},
                           {"cancelled", job->wasCancelled},
                           {"systemId", job->sys.id}};
        emit busyChanged();
        emit finished(report);
    });
    m_thread->start();
    emit busyChanged();
    return {};
}

void Importer::cancel() { m_cancel = true; }

void Importer::postProgress(double p, const QString &status, const QString &item)
{
    QMetaObject::invokeMethod(this, [this, p, status, item] {
        m_progress = p;
        m_status = status;
        m_item = item;
        emit progressChanged();
    }, Qt::QueuedConnection);
}
