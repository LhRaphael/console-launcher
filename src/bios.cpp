#include "bios.h"
#include "emuconfig.h"
#include "importer.h"
#include "library.h"
#include "runner.h"

#include <QCryptographicHash>
#include <QDateTime>
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
#include <QThread>
#include <QUuid>
#include <QtEndian>

#include <functional>
#include <memory>

namespace {

using Kind = BiosManager::Kind;

constexpr qint64 KB = 1024, MB = 1024 * 1024;

// ------------------------------------------------------------------ tabelas
// Somente as somas MD5 (para dar nome e região aos dumps). Fonte:
// libretro-database, dat/System.dat. Nenhum arquivo de BIOS vem com o launcher.
struct KnownDump {
    const char *md5, *file, *model, *region;  // region: J | U | E | * (todas)
};
const KnownDump kPs1[] = {
    {"239665b1a3dade1b5a52c06338011044", "scph1000.bin", "SCPH-1000", "J"},
    {"924e392ed05558ffdb115408c263dccf", "scph1001.bin", "SCPH-1001", "U"},
    {"54847e693405ffeb0359c6287434cbef", "scph1002.bin", "SCPH-1002", "E"},
    {"8abc1b549a4a80954addc48ef02c4521", "scph100.bin", "SCPH-100 (PSone)", "J"},
    {"6e3735ff4c7dc899ee98981385f6f3d0", "scph101.bin", "SCPH-101 (PSone)", "U"},
    {"b10f5e0e3d9eb60e5159690680b1e774", "scph102A.bin", "SCPH-102 (PSone)", "E"},
    {"de93caec13d1a141a40a79f5c86168d6", "scph102B.bin", "SCPH-102 (PSone)", "E"},
    {"849515939161e62f6b866f6853006780", "scph3000.bin", "SCPH-3000", "J"},
    {"cba733ceeff5aef5c32254f1d617fa62", "scph3500.bin", "SCPH-3500", "J"},
    {"eb201d2d98251a598af467d4347bb62f", "scph5000.bin", "SCPH-5000", "J"},
    {"8dd7d5296a650fac7319bce665a6a53c", "scph5500.bin", "SCPH-5500", "J"},
    {"490f666e1afb15b7362b406ed1cea246", "scph5501.bin", "SCPH-5501/7003", "U"},
    {"32736f17079d0b2b7024407c39bd3050", "scph5502.bin", "SCPH-5502/5552", "E"},
    {"1e68c231d0896b7eadcad1d7d8e76129", "scph7001.bin", "SCPH-7001", "U"},
    {"b9d9a0286c33dc6b7237bb13cd46fdee", "scph7502.bin", "SCPH-7002/7502/9002", "E"},
    {"c53ca5908936d412331790f4426c6c33", "psxonpsp660.bin", "PSP (psxonpsp660)", "*"},
    {"81bbe60ba7a3d1cea1d48c14cbcc647b", "ps1_rom.bin", "PS3 (ps1_rom)", "*"},
};
// Game Boy / Color / Advance (libretro System.dat). Todas opcionais no mGBA.
const KnownDump kGameBoy[] = {
    {"a860e8c0b6d573d191e4ec7db1b1e4f6", "gba_bios.bin", "Game Boy Advance", "*"},
    {"32fbbd84168d3482956eb3c5051637f5", "gb_bios.bin", "Game Boy", "*"},
    {"dbfce9db9deaa2567f6a84fde55f9680", "gbc_bios.bin", "Game Boy Color", "*"},
};
const KnownDump kGcIpl[] = {
    {"fc924a7c879b661abc37cec4f018fdf3", "IPL.bin", "NTSC 1.0", "U"},
    {"019e39822a9ca3029124f74dd4d55ac4", "IPL.bin", "NTSC 1.1", "U"},
    {"b17148254a5799684c7d783206504926", "IPL.bin", "NTSC 1.2", "U"},
    {"0cdda509e2da83c85bfe423dd87346cc", "IPL.bin", "PAL 1.0", "E"},
    {"339848a0b7c2124cf155276c1e79cbd0", "IPL.bin", "PAL 1.1", "E"},
    {"db92574caab77a7ec99d4605fd6f2450", "IPL.bin", "PAL 1.2", "E"},
};
// MCPX 1.0 (a única aceita pelo xemu) — docs.libretro.com/library/xemu
const char *kMcpx10 = "d49c52a4102f6df7bcf8d0617ac475ed";

const QStringList kPs4Modules{"libSceCesCs.sprx", "libSceFont.sprx", "libSceFontFt.sprx", "libSceFreeTypeOt.sprx",
                              "libSceJson.sprx", "libSceJson2.sprx", "libSceLibcInternal.sprx",
                              "libSceNgs2.sprx", "libSceRtc.sprx", "libSceUlt.sprx"};
const QStringList kArchiveExt{"zip", "7z", "rar", "tar", "tgz", "txz", "tbz2"};
constexpr int kMinFirmwareNca = 50;  // o firmware do Switch tem centenas de .nca

QString regionName(const QString &r)
{
    if (r == "J") return QStringLiteral("Japão");
    if (r == "U") return QStringLiteral("EUA");
    if (r == "E") return QStringLiteral("Europa");
    if (r == "*") return QStringLiteral("todas as regiões");
    return r;
}

QString humanSize(qint64 b)
{
    return QLocale(QLocale::Portuguese, QLocale::Brazil).formattedDataSize(b, 1, QLocale::DataSizeTraditionalFormat);
}

QString md5Of(const QString &path, qint64 maxSize = 64 * MB)
{
    QFile f(path);
    if (f.size() > maxSize || !f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

const KnownDump *findDump(const KnownDump *table, int n, const QString &md5)
{
    for (int i = 0; i < n; ++i)
        if (md5 == QLatin1String(table[i].md5))
            return &table[i];
    return nullptr;
}
template <int N>
const KnownDump *findDump(const KnownDump (&t)[N], const QString &md5) { return findDump(t, N, md5); }

QString xdg(const char *var, const QString &fallback)
{
    const QString v = qEnvironmentVariable(var);
    return v.isEmpty() ? QDir::homePath() + fallback : v;
}

QString underBase(const QString &base, const QString &value, const QString &fallback)
{
    if (value.isEmpty())
        return base + '/' + fallback;
    const QString v = expandHome(value);
    return QDir::isAbsolutePath(v) ? QDir::cleanPath(v) : QDir::cleanPath(base + '/' + v);
}

// ------------------------------------------------------------------ identificação
struct Ident {
    QString role;      // bios | pup | sprx | fontdir | mcpx | flash | eeprom | hdd | prodkeys | titlekeys | nca
    QString label;     // texto para o usuário
    QString destName;  // nome do arquivo no destino
    QString region;    // J | U | E | * (PS1, GameCube, PS2)
    QString version;   // PS2/PS3
    QString md5;
    QString reject;    // parece BIOS mas não serve (motivo)
};

// PS2: tabela ROMDIR (mesma leitura do PCSX2, pcsx2/ps2/BiosTools.cpp)
bool ps2Info(const QString &path, QString *label, QString *region, QString *version)
{
    QFile f(path);
    if (f.size() < 4 * MB || f.size() > 8 * MB || !f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.read(f.size());
    const int maxScan = qMin<int>(data.size() - 16, 512 * 1024 * 16);
    int pos = -1;
    for (int i = 0; i + 16 <= maxScan; i += 16)
        if (qstrncmp(data.constData() + i, "RESET", 10) == 0 && data[i + 5] == '\0') {
            pos = i;
            break;
        }
    if (pos < 0)
        return false;
    qint64 fileOffset = 0;
    QByteArray romver;
    for (int i = pos; i + 16 <= data.size(); i += 16) {
        const char *e = data.constData() + i;
        if (e[0] == '\0' || qstrnlen(e, 10) == 10)
            break;
        const quint32 size = qFromLittleEndian<quint32>(e + 12);
        if (qstrncmp(e, "ROMVER", 10) == 0) {
            if (fileOffset + 14 <= data.size())
                romver = data.mid(fileOffset, 14);
            break;
        }
        fileOffset += (size % 16 == 0) ? size : ((size + 16) & ~quint32(15));
        if (fileOffset > data.size())
            break;
    }
    if (romver.size() != 14)
        return false;
    const QString rv = QString::fromLatin1(romver);
    const QChar z = rv[4];
    QString zone;
    switch (z.toLatin1()) {
    case 'J': zone = "Japão"; *region = "J"; break;
    case 'A': zone = "EUA"; *region = "U"; break;
    case 'E': zone = "Europa"; *region = "E"; break;
    case 'H': zone = "Ásia"; *region = "H"; break;
    case 'C': zone = "China"; *region = "C"; break;
    case 'T': zone = "T10K"; *region = "T"; break;
    case 'X': zone = "Teste"; *region = "X"; break;
    default: zone = QString(z); *region = QString(z); break;
    }
    *version = QString::number(rv.mid(0, 2).toInt()) + QLatin1Char('.') + rv.mid(2, 2);
    const QString date = rv.mid(12, 2) + '/' + rv.mid(10, 2) + '/' + rv.mid(6, 4);
    *label = QStringLiteral("PS2 v%1 · %2 · %3%4").arg(*version, zone, date,
                                                        rv[5] == 'D' ? QStringLiteral(" · devkit") : QString());
    return true;
}

// PS3: PS3UPDAT.PUP ("SCEUF"), versão no arquivo 0x100 (version.txt) do pacote
bool pupInfo(const QString &path, QString *version)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(0x30);
    if (head.size() < 0x30 || !head.startsWith("SCEUF"))
        return false;
    const quint64 count = qFromBigEndian<quint64>(head.constData() + 0x18);
    if (count == 0 || count > 256)
        return true;
    const QByteArray table = f.read(qint64(count) * 0x20);
    for (quint64 i = 0; i < count && (i + 1) * 0x20 <= quint64(table.size()); ++i) {
        const char *e = table.constData() + i * 0x20;
        if (qFromBigEndian<quint64>(e) != 0x100)
            continue;
        const quint64 off = qFromBigEndian<quint64>(e + 8), len = qFromBigEndian<quint64>(e + 16);
        if (len > 0 && len < 64 && off + len <= quint64(f.size()) && f.seek(qint64(off)))
            *version = QString::fromLatin1(f.read(qint64(len))).trimmed();
        break;
    }
    return true;
}

// Switch: arquivo de chaves em texto (nome = hex)
QString keysRole(const QString &path)
{
    QFile f(path);
    if (f.size() > 512 * KB || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    static const QRegularExpression line(R"(^\s*([A-Za-z0-9_]+)\s*=\s*([0-9A-Fa-f]{16,})\s*$)");
    static const QRegularExpression prodName(R"(^(header_key|master_key_|key_area_key_|titlekek_|package2_key_|aes_kek_generation_source|eticket_rsa_kek))");
    static const QRegularExpression hex32(R"(^[0-9A-Fa-f]{32}$)");
    int matches = 0, prod = 0, title = 0;
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
    for (const QString &l : lines) {
        const auto m = line.match(l);
        if (!m.hasMatch())
            continue;
        ++matches;
        if (prodName.match(m.captured(1)).hasMatch())
            ++prod;
        else if (hex32.match(m.captured(1)).hasMatch())
            ++title;
    }
    if (matches < 2)
        return {};
    if (prod > 0)
        return QStringLiteral("prodkeys");
    if (title * 2 >= matches)
        return QStringLiteral("titlekeys");
    return {};
}

bool looksLikeRom(const QString &path)
{
    // dump de verdade não é só 0x00/0xFF
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray d = f.read(64 * KB);
    QSet<char> seen;
    for (char c : d) {
        seen.insert(c);
        if (seen.size() > 24)
            return true;
    }
    return false;
}

Ident identify(Kind kind, const QFileInfo &fi)
{
    Ident id;
    const QString path = fi.absoluteFilePath();
    const qint64 size = fi.size();
    const QString ext = fi.suffix().toLower();
    switch (kind) {
    case Kind::DuckStation: {
        if (size == 512 * KB) {
            id.md5 = md5Of(path);
            if (const KnownDump *k = findDump(kPs1, id.md5)) {
                id.role = "bios";
                id.region = k->region;
                id.destName = k->file;
                id.label = QStringLiteral("PS1 %1 · %2").arg(k->model, regionName(k->region));
                return id;
            }
            QFile f(path);
            if (f.open(QIODevice::ReadOnly) && f.readAll().contains("Sony Computer Entertainment")) {
                id.role = "bios";
                id.destName = fi.fileName();
                id.label = QStringLiteral("BIOS do PS1 não catalogada");
                return id;
            }
        } else if (size >= 4 * MB && size <= 8 * MB) {
            // o DuckStation também aceita a BIOS do PS2 (ela contém a do PS1)
            QString label, region, version;
            if (ps2Info(path, &label, &region, &version)) {
                id.role = "bios";
                id.region = region;
                id.destName = fi.fileName();
                id.label = label + QStringLiteral(" (o DuckStation também aceita)");
                id.md5 = md5Of(path);
                return id;
            }
        }
        break;
    }
    case Kind::Pcsx2: {
        QString label, region, version;
        if (ps2Info(path, &label, &region, &version)) {
            id.role = "bios";
            id.label = label;
            id.region = region;
            id.version = version;
            id.destName = fi.fileName();
            id.md5 = md5Of(path);
        }
        break;
    }
    case Kind::Rpcs3: {
        QString version;
        if ((ext == "pup" || size > 50 * MB) && pupInfo(path, &version)) {
            id.role = "pup";
            id.version = version;
            id.destName = "PS3UPDAT.PUP";
            id.label = version.isEmpty() ? QStringLiteral("Firmware do PS3")
                                         : QStringLiteral("Firmware do PS3 %1").arg(version);
        }
        break;
    }
    case Kind::ShadPs4:
        if (ext == "sprx" && size > 0) {
            id.role = "sprx";
            id.destName = fi.fileName();
            id.label = QStringLiteral("Módulo do PS4 %1").arg(fi.fileName());
        }
        break;
    case Kind::Xemu:
        if (size == 512) {
            id.md5 = md5Of(path);
            if (id.md5 == QLatin1String(kMcpx10)) {
                id.role = "mcpx";
                id.destName = "mcpx_1.0.bin";
                id.label = QStringLiteral("MCPX 1.0 (boot ROM)");
            } else {
                id.reject = QStringLiteral("boot ROM MCPX diferente da 1.0 — o xemu só aceita a 1.0");
            }
        } else if (size == 256) {
            id.role = "eeprom";
            id.destName = "eeprom.bin";
            id.label = QStringLiteral("EEPROM do Xbox");
        } else if ((size == 256 * KB || size == MB) && looksLikeRom(path)) {
            id.role = "flash";
            id.destName = fi.fileName();
            id.label = QStringLiteral("BIOS do Xbox (flash, %1)").arg(size == MB ? "1 MB" : "256 KB");
        } else if (size > 64 * MB) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly) && f.read(4) == QByteArray("QFI\xfb", 4)) {
                id.role = "hdd";
                id.destName = fi.fileName();
                id.label = QStringLiteral("Disco rígido do Xbox (%1)").arg(humanSize(size));
            }
        }
        break;
    case Kind::DolphinGc:
        if (size == 2 * MB) {
            id.md5 = md5Of(path);
            if (const KnownDump *k = findDump(kGcIpl, id.md5)) {
                id.role = "bios";
                id.region = k->region;
                id.label = QStringLiteral("IPL do GameCube %1").arg(k->model);
            } else {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    const QByteArray h = f.read(0x100);
                    if (h.contains("Nintendo")) {
                        id.role = "bios";
                        id.region = h.contains("PAL") ? "E" : "U";
                        id.label = QStringLiteral("IPL do GameCube (%1)").arg(id.region == "E" ? "PAL" : "NTSC");
                    }
                }
            }
            id.destName = "IPL.bin";
        }
        break;
    case Kind::Mgba:
        if (size == 16 * KB || size == 256 || size == 2304) {
            id.md5 = md5Of(path);
            if (const KnownDump *k = findDump(kGameBoy, id.md5)) {
                id.role = k->file;  // um papel por aparelho
                id.destName = k->file;
                id.label = QStringLiteral("BIOS do %1").arg(k->model);
            } else if (size == 16 * KB && (ext == "bin" || ext == "rom")) {
                // outras revisões (ex.: a do modo GBA do Nintendo DS) também funcionam
                id.role = "gba_bios.bin";
                id.destName = "gba_bios.bin";
                id.label = QStringLiteral("BIOS do Game Boy Advance (revisão não catalogada)");
            }
        }
        break;
    case Kind::Eden:
        if (ext == "nca" && size > 0) {
            id.role = "nca";
            id.destName = fi.fileName();
            id.label = fi.fileName();
        } else if (ext == "keys" || ext == "txt" || ext == "ini" || ext.isEmpty()) {
            id.role = keysRole(path);
            if (id.role == "prodkeys") {
                id.destName = "prod.keys";
                id.label = QStringLiteral("Chaves do console (prod.keys)");
            } else if (id.role == "titlekeys") {
                id.destName = "title.keys";
                id.label = QStringLiteral("Chaves dos jogos (title.keys)");
            }
        }
        break;
    case Kind::None:
        break;
    }
    return id;
}

// Filtro barato para a lista do painel (não lê o conteúdo)
QString quickBadge(Kind kind, const QFileInfo &fi)
{
    const qint64 s = fi.size();
    const QString ext = fi.suffix().toLower();
    switch (kind) {
    case Kind::DuckStation: return (s == 512 * KB || (s >= 4 * MB && s <= 8 * MB)) ? "BIOS?" : QString();
    case Kind::Pcsx2: return (s >= 4 * MB && s <= 8 * MB) ? "BIOS?" : QString();
    case Kind::Rpcs3: return ext == "pup" ? "firmware" : QString();
    case Kind::ShadPs4: return ext == "sprx" ? "módulo" : QString();
    case Kind::Xemu:
        if (s == 512 || s == 256 * KB || s == MB) return "BIOS?";
        if (s == 256) return "EEPROM?";
        if (ext == "qcow2") return "disco";
        return {};
    case Kind::DolphinGc: return s == 2 * MB ? "IPL?" : QString();
    case Kind::Mgba: return (s == 16 * KB || s == 256 || s == 2304) ? "BIOS?" : QString();
    case Kind::Eden:
        if (ext == "keys") return "chaves";
        if (ext == "nca") return "firmware";
        return {};
    case Kind::None: return {};
    }
    return {};
}

bool isArchiveName(const QString &name)
{
    const QString l = name.toLower();
    if (l.endsWith(".tar.gz") || l.endsWith(".tar.xz") || l.endsWith(".tar.bz2") || l.endsWith(".tar.zst"))
        return true;
    return kArchiveExt.contains(QFileInfo(l).suffix());
}

int ncaCount(const QString &dir, int limit = 100000)
{
    int n = 0;
    QDirIterator it(dir, {"*.nca", "*.NCA"}, QDir::Files);
    while (it.hasNext() && n < limit) {
        it.next();
        ++n;
    }
    return n;
}

QString rpcs3Firmware(const QString &devFlash)
{
    QFile f(devFlash + "/vsh/etc/version.txt");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    static const QRegularExpression re(R"(release:\s*0*(\d+)\.(\d\d))");
    const auto m = re.match(QString::fromLatin1(f.read(4096)));
    return m.hasMatch() ? m.captured(1) + '.' + m.captured(2) : QStringLiteral("instalado");
}

// ------------------------------------------------------------------ trabalho
struct Candidate {
    QString path;
    Ident id;
    qint64 size = 0;
};

struct BiosJob {
    BiosManager *mgr = nullptr;
    SystemDef sys;
    BiosManager::Paths paths;
    QStringList sources;
    QString tmpRoot, pupStage;

    QVariantList imported, skipped, present;
    QStringList notes;
    QVariantMap firmware;
    bool wasCancelled = false;

    QList<Candidate> found;
    QStringList rejected;
    qint64 totalBytes = 0, doneBytes = 0;
    QString current;
    QElapsedTimer lastPost;

    bool cancelled() const { return mgr->cancelled(); }
    void progress(double frac, const QString &status, bool force = false)
    {
        if (!force && lastPost.isValid() && lastPost.elapsed() < 120)
            return;
        lastPost.start();
        mgr->postProgress(qBound(0.0, frac, 1.0), status, current);
    }
    void skip(const QString &name, const QString &reason) { skipped << QVariantMap{{"name", name}, {"reason", reason}}; }
    void ok(const QString &label, const QString &dest) { imported << QVariantMap{{"title", label}, {"file", tildePath(dest)}}; }

    bool runTool(const QString &program, const QStringList &args, QString *output);
    bool extract(const QString &archive, const QString &out, QString *err);
    void gather(const QString &path, int depth);
    bool copyFile(const QString &src, const QString &dst, QString *err);
    QString place(const Candidate &c, const QString &dir, const QString &name, bool backupOld);
    void install();
    void run();
};

bool BiosJob::runTool(const QString &program, const QStringList &args, QString *output)
{
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setStandardInputFile(QProcess::nullDevice());
    p.start();
    if (!p.waitForStarted(5000)) {
        *output = p.errorString();
        return false;
    }
    QByteArray tail;
    while (p.state() != QProcess::NotRunning) {
        if (cancelled()) {
            p.kill();
            p.waitForFinished(3000);
            *output = QStringLiteral("cancelado");
            return false;
        }
        p.waitForReadyRead(200);
        tail = (tail + p.readAll()).right(4000);
    }
    p.waitForFinished();
    tail += p.readAll();
    *output = QString::fromLocal8Bit(tail).trimmed();
    return p.exitStatus() == QProcess::NormalExit && (p.exitCode() == 0 || p.exitCode() == 1);
}

bool BiosJob::extract(const QString &archive, const QString &out, QString *err)
{
    const QString name = archive.toLower();
    const bool tarLike = name.contains(".tar") || name.endsWith(".tgz") || name.endsWith(".txz") || name.endsWith(".tbz2");
    auto have = [](const char *p) { return QStandardPaths::findExecutable(QString::fromLatin1(p)); };
    QList<QPair<QString, QStringList>> cmds;
    QString sz;
    for (const char *c : {"7z", "7zz", "7za"})
        if (sz.isEmpty())
            sz = have(c);
    if (tarLike && !have("tar").isEmpty())
        cmds << qMakePair(have("tar"), QStringList{"-xf", archive, "-C", out});
    if (!sz.isEmpty() && !tarLike)
        cmds << qMakePair(sz, QStringList{"x", "-y", "-bso0", "-bsp0", "-o" + out, archive});
    if (!have("bsdtar").isEmpty())
        cmds << qMakePair(have("bsdtar"), QStringList{"-xf", archive, "-C", out});
    if (name.endsWith(".zip") && !have("unzip").isEmpty())
        cmds << qMakePair(have("unzip"), QStringList{"-o", "-q", archive, "-d", out});
    if (!have("unar").isEmpty() && !tarLike)
        cmds << qMakePair(have("unar"), QStringList{"-q", "-f", "-D", "-o", out, archive});
    if (cmds.isEmpty()) {
        *err = QStringLiteral("nenhum programa de extração instalado (sudo apt install 7zip unar)");
        return false;
    }
    QString output;
    for (const auto &c : cmds) {
        progress(0.05, QStringLiteral("Extraindo com %1…").arg(QFileInfo(c.first).fileName()), true);
        if (runTool(c.first, c.second, &output) && !QDir(out).isEmpty())
            return true;
        if (cancelled()) {
            *err = QStringLiteral("cancelado");
            return false;
        }
        QDir(out).removeRecursively();
        QDir().mkpath(out);
    }
    *err = output.contains("password", Qt::CaseInsensitive) || output.contains("encrypted", Qt::CaseInsensitive)
               ? QStringLiteral("arquivo protegido por senha")
               : QStringLiteral("falha ao extrair");
    return false;
}

void BiosJob::gather(const QString &path, int depth)
{
    if (cancelled() || depth > 6 || found.size() > 5000)
        return;
    const QFileInfo fi(path);
    if (fi.isSymLink() && depth > 0)
        return;
    if (fi.isDir()) {
        const QString n = fi.fileName().toLower();
        if (paths.kind == Kind::ShadPs4 && (n == "font" || n == "font2")) {
            Candidate c;
            c.path = fi.absoluteFilePath();
            c.id.role = "fontdir";
            c.id.destName = n;
            c.id.label = QStringLiteral("Fontes do PS4 (%1)").arg(n);
            QDirIterator it(c.path, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                c.size += it.fileInfo().size();
            }
            found << c;
            return;
        }
        const QFileInfoList list = QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                                            QDir::Name);
        for (const QFileInfo &e : list)
            gather(e.absoluteFilePath(), depth + 1);
        return;
    }
    if (!fi.isFile())
        return;
    current = fi.fileName();
    if (isArchiveName(fi.fileName())) {
        if (fi.size() > 2048 * MB) {
            skip(fi.fileName(), QStringLiteral("grande demais para ser BIOS/firmware (%1)").arg(humanSize(fi.size())));
            return;
        }
        const QString out = tmpRoot + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QDir().mkpath(out);
        QString err;
        progress(0.05, QStringLiteral("Extraindo %1…").arg(fi.fileName()), true);
        if (!extract(fi.absoluteFilePath(), out, &err)) {
            if (!cancelled())
                skip(fi.fileName(), err);
            return;
        }
        gather(out, depth + 1);
        return;
    }
    if (found.size() % 50 == 0)
        progress(0.1, QStringLiteral("Procurando BIOS e firmware…"));
    Candidate c;
    c.path = fi.absoluteFilePath();
    c.size = fi.size();
    c.id = identify(paths.kind, fi);
    if (!c.id.role.isEmpty())
        found << c;
    else if (!c.id.reject.isEmpty())
        skip(fi.fileName(), c.id.reject);
}

bool BiosJob::copyFile(const QString &src, const QString &dst, QString *err)
{
    QDir().mkpath(QFileInfo(dst).absolutePath());
    const QString tmp = dst + ".part";
    QFile in(src), out(tmp);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
        *err = QStringLiteral("não foi possível gravar em %1").arg(tildePath(QFileInfo(dst).absolutePath()));
        return false;
    }
    QByteArray buf(4 * MB, Qt::Uninitialized);
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
        doneBytes += n;
        progress(0.2 + 0.75 * double(doneBytes) / qMax<qint64>(1, totalBytes),
                 QStringLiteral("Copiando… %1 de %2").arg(humanSize(doneBytes), humanSize(totalBytes)));
    }
    if (!out.flush()) {
        out.remove();
        *err = QStringLiteral("erro ao gravar");
        return false;
    }
    out.close();
    QFile::remove(dst);
    if (!QFile::rename(tmp, dst)) {
        QFile::remove(tmp);
        *err = QStringLiteral("não foi possível gravar %1").arg(QFileInfo(dst).fileName());
        return false;
    }
    return true;
}

// Copia c para dir/name. Mesmo conteúdo já presente → não copia.
// Mesmo nome com conteúdo diferente → backupOld ? guarda o antigo em .bak : novo nome.
// Devolve o caminho final, ou "" (e registra o motivo em skipped).
QString BiosJob::place(const Candidate &c, const QString &dir, const QString &name, bool backupOld)
{
    QString dest = dir + '/' + name;
    const QFileInfo existing(dest);
    if (existing.exists()) {
        const bool same = existing.size() == c.size
                          && (c.size > 64 * MB || md5Of(dest) == (c.id.md5.isEmpty() ? md5Of(c.path) : c.id.md5));
        if (same) {
            present << QVariantMap{{"title", c.id.label}, {"file", tildePath(dest)}};
            return QStringLiteral("=") + dest;  // presente, sem cópia
        }
        if (backupOld) {
            QFile::remove(dest + ".bak");
            QFile::rename(dest, dest + ".bak");
        } else {
            const QFileInfo nfi(name);
            for (int i = 2; QFileInfo::exists(dest); ++i)
                dest = dir + '/' + nfi.completeBaseName() + QStringLiteral(" (%1).").arg(i) + nfi.suffix();
        }
    }
    QString err;
    if (!copyFile(c.path, dest, &err)) {
        if (!cancelled())
            skip(QFileInfo(c.path).fileName(), err);
        return {};
    }
    QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    return dest;
}

void BiosJob::install()
{
    const QString dirLabel = tildePath(paths.biosDir);
    // um arquivo com o mesmo conteúdo aparece uma vez só (ex.: zip + pasta extraída)
    QList<Candidate> list;
    QSet<QString> seenMd5;
    for (const Candidate &c : found) {
        if (!c.id.md5.isEmpty()) {
            if (seenMd5.contains(c.id.md5 + c.id.role))
                continue;
            seenMd5.insert(c.id.md5 + c.id.role);
        }
        list << c;
    }
    if (paths.kind == Kind::DuckStation) {
        bool realPs1 = false;
        for (const Candidate &c : list)
            realPs1 |= c.size == 512 * KB;
        for (const QFileInfo &fi : QDir(paths.biosDir).entryInfoList(QDir::Files))
            realPs1 |= fi.size() == 512 * KB && identify(Kind::DuckStation, fi).role == "bios";
        if (realPs1) {
            QList<Candidate> keep;
            for (const Candidate &c : list)
                if (c.size == 512 * KB)
                    keep << c;
            list = keep;
        }
    }
    for (const Candidate &c : list)
        totalBytes += c.size;

    switch (paths.kind) {
    case Kind::DuckStation:
    case Kind::Pcsx2: {
        QString firstNew;
        for (const Candidate &c : list) {
            current = QFileInfo(c.path).fileName();
            const QString d = place(c, paths.biosDir, c.id.destName, false);
            if (d.isEmpty())
                continue;
            if (!d.startsWith('='))
                ok(c.id.label, d);
            if (firstNew.isEmpty())
                firstNew = QFileInfo(d.startsWith('=') ? d.mid(1) : d).fileName();
        }
        if (paths.kind == Kind::Pcsx2 && !firstNew.isEmpty()) {
            if (!QFileInfo::exists(paths.config)) {
                notes << QStringLiteral("Na primeira vez, o PCSX2 abre o assistente: a BIOS já aparece na lista, é só escolher e concluir.");
            } else {
                const QString cur = EmuConfig::getIni(paths.config, "Filenames", "BIOS");
                QString err;
                if (cur.isEmpty() || !QFileInfo::exists(paths.biosDir + '/' + cur)) {
                    // prefere EUA → Europa → Japão entre as importadas agora
                    QString best = firstNew;
                    int bestRank = 99;
                    for (const Candidate &c : list) {
                        const int rank = c.id.region == "U" ? 0 : c.id.region == "E" ? 1 : c.id.region == "J" ? 2 : 3;
                        const QString n = c.id.destName;
                        if (rank < bestRank && QFileInfo::exists(paths.biosDir + '/' + n)) {
                            bestRank = rank;
                            best = n;
                        }
                    }
                    if (EmuConfig::setIni(paths.config, "Filenames", "BIOS", best, &err))
                        notes << QStringLiteral("PCSX2 configurado para usar %1.").arg(best);
                }
                EmuConfig::setIni(paths.config, "UI", "SetupWizardIncomplete", "false", &err);
            }
        }
        if (paths.kind == Kind::DuckStation && !imported.isEmpty())
            notes << QStringLiteral("O DuckStation escolhe a BIOS pela região de cada jogo.");
        break;
    }
    case Kind::Rpcs3: {
        const Candidate *best = nullptr;
        for (const Candidate &c : list)
            if (!best || c.id.version > best->id.version)
                best = &c;
        if (!best)
            break;
        current = QFileInfo(best->path).fileName();
        QString err;
        QFile::remove(pupStage);
        if (!copyFile(best->path, pupStage, &err)) {
            if (!cancelled())
                skip(current, err);
            break;
        }
        ok(best->id.label, tildePath(paths.devFlash) + QStringLiteral(" (instalado pelo RPCS3)"));
        firmware = QVariantMap{{"version", best->id.version}, {"label", best->id.label}};
        notes << QStringLiteral("O RPCS3 vai abrir para instalar o firmware. Quando aparecer a mensagem de sucesso, feche o RPCS3 (segure o Guia).");
        break;
    }
    case Kind::ShadPs4: {
        for (const Candidate &c : list) {
            current = QFileInfo(c.path).fileName();
            if (c.id.role == "fontdir") {
                QDirIterator it(c.path, QDir::Files, QDirIterator::Subdirectories);
                int n = 0;
                bool failed = false;
                while (it.hasNext() && !cancelled()) {
                    const QString f = it.next();
                    const QString dst = paths.fontsDir + '/' + c.id.destName + f.mid(c.path.size());
                    QString err;
                    if (!copyFile(f, dst, &err)) {
                        skip(it.fileName(), err);
                        failed = true;
                        break;
                    }
                    ++n;
                }
                if (!failed && n > 0)
                    ok(QStringLiteral("%1 (%2 arquivo%3)").arg(c.id.label).arg(n).arg(n == 1 ? "" : "s"),
                       paths.fontsDir + '/' + c.id.destName);
                continue;
            }
            const QString d = place(c, paths.biosDir, c.id.destName, true);
            if (!d.isEmpty() && !d.startsWith('='))
                ok(c.id.label, d);
        }
        break;
    }
    case Kind::Xemu: {
        const QHash<QString, QString> key{{"mcpx", "bootrom_path"}, {"flash", "flashrom_path"},
                                          {"eeprom", "eeprom_path"}, {"hdd", "hdd_path"}};
        QSet<QString> done;
        for (const Candidate &c : list) {
            if (done.contains(c.id.role) && c.id.role != "flash")
                continue;  // um de cada (o primeiro encontrado)
            current = QFileInfo(c.path).fileName();
            const QString d = place(c, paths.biosDir, c.id.destName, c.id.role == "eeprom" || c.id.role == "mcpx");
            if (d.isEmpty())
                continue;
            const QString finalPath = d.startsWith('=') ? d.mid(1) : d;
            if (!d.startsWith('='))
                ok(c.id.label, finalPath);
            QString err;
            if (!done.contains(c.id.role)) {
                QDir().mkpath(QFileInfo(paths.config).absolutePath());
                if (!EmuConfig::setTomlString(paths.config, "sys.files", key.value(c.id.role), finalPath, true, &err))
                    skip(current, QStringLiteral("não consegui ajustar o xemu.toml: %1").arg(err));
            }
            done.insert(c.id.role);
        }
        if (!done.isEmpty())
            notes << QStringLiteral("xemu.toml atualizado com os caminhos (%1).").arg(tildePath(paths.config));
        break;
    }
    case Kind::DolphinGc: {
        for (const Candidate &c : list) {
            current = QFileInfo(c.path).fileName();
            const QStringList regions = c.id.region == "E" ? QStringList{"EUR"} : QStringList{"USA", "JAP"};
            for (const QString &r : regions) {
                const QString dir = paths.gcRoot + '/' + r;
                if (r == "JAP" && QFileInfo::exists(dir + "/IPL.bin"))
                    continue;  // NTSC vai para o Japão só se não houver uma própria
                const QString d = place(c, dir, "IPL.bin", true);
                if (!d.isEmpty() && !d.startsWith('='))
                    ok(QStringLiteral("%1 → %2").arg(c.id.label, r), d);
            }
        }
        if (!imported.isEmpty())
            notes << QStringLiteral("Com o IPL, o Dolphin mostra a abertura do GameCube (desligue \"Pular a BIOS\" nas configurações dele).");
        break;
    }
    case Kind::Mgba: {
        static const char *keys[][2] = {{"gba_bios.bin", "gba.bios"}, {"gb_bios.bin", "gb.bios"}, {"gbc_bios.bin", "gbc.bios"}};
        QSet<QString> done;
        for (const Candidate &c : list) {
            if (done.contains(c.id.role))
                continue;
            current = QFileInfo(c.path).fileName();
            const QString d = place(c, paths.biosDir, c.id.destName, true);
            if (d.isEmpty())
                continue;
            const QString finalPath = d.startsWith('=') ? d.mid(1) : d;
            if (!d.startsWith('='))
                ok(c.id.label, finalPath);
            for (const auto &k : keys) {
                if (c.id.role != QLatin1String(k[0]))
                    continue;
                QString err;
                QDir().mkpath(QFileInfo(paths.config).absolutePath());
                if (!QFileInfo::exists(paths.config)) {
                    QFile f(paths.config);
                    if (f.open(QIODevice::WriteOnly))
                        f.close();
                }
                if (!EmuConfig::setIni(paths.config, "ports.qt", k[1], finalPath, &err)
                    || !EmuConfig::setIni(paths.config, "ports.qt", "useBios", "1", &err))
                    skip(current, QStringLiteral("não consegui ajustar o config.ini do mGBA: %1").arg(err));
            }
            done.insert(c.id.role);
        }
        if (!done.isEmpty())
            notes << QStringLiteral("config.ini do mGBA atualizado (%1). Sem BIOS o mGBA usa a dele, embutida.")
                         .arg(tildePath(paths.config));
        break;
    }
    case Kind::Eden: {
        QList<Candidate> ncas;
        for (const Candidate &c : list) {
            if (c.id.role == "nca") {
                ncas << c;
                continue;
            }
            current = QFileInfo(c.path).fileName();
            const QString d = place(c, paths.keysDir, c.id.destName, true);
            if (!d.isEmpty() && !d.startsWith('='))
                ok(c.id.label, d);
        }
        if (!ncas.isEmpty() && ncas.size() < kMinFirmwareNca) {
            skip(QStringLiteral("%1 arquivo(s) .nca").arg(ncas.size()),
                 QStringLiteral("parece incompleto — o firmware do Switch tem centenas de .nca"));
        } else if (!ncas.isEmpty()) {
            // monta a pasta nova ao lado e só troca no fim (o firmware antigo fica intacto se algo falhar)
            const QString target = QDir::cleanPath(paths.ncaDir);
            const QString fresh = target + ".novo", old = target + ".antigo";
            QDir(fresh).removeRecursively();
            QDir().mkpath(fresh);
            bool failed = false;
            for (const Candidate &c : ncas) {
                current = QFileInfo(c.path).fileName();
                QString err;
                if (!copyFile(c.path, fresh + '/' + QFileInfo(c.path).fileName(), &err)) {
                    if (!cancelled())
                        skip(current, err);
                    failed = true;
                    break;
                }
            }
            if (failed) {
                QDir(fresh).removeRecursively();
            } else {
                QDir(old).removeRecursively();
                if (QFileInfo::exists(target))
                    QDir().rename(target, old);
                if (QDir().rename(fresh, target)) {
                    QDir(old).removeRecursively();
                    ok(QStringLiteral("Firmware do Switch (%1 arquivos .nca)").arg(ncas.size()), target);
                } else {
                    QDir().rename(old, target);
                    QDir(fresh).removeRecursively();
                    skip(QStringLiteral("firmware"), QStringLiteral("não foi possível trocar a pasta do firmware"));
                }
            }
        }
        break;
    }
    case Kind::None:
        break;
    }
    Q_UNUSED(dirLabel);
}

void BiosJob::run()
{
    QDir().mkpath(tmpRoot);
    for (const QString &s : sources) {
        if (cancelled())
            break;
        current = QFileInfo(s).fileName();
        progress(0.02, QStringLiteral("Procurando BIOS e firmware…"), true);
        const int before = found.size(), skippedBefore = skipped.size();
        gather(s, 0);
        if (found.size() == before && skipped.size() == skippedBefore && !cancelled())
            skip(QFileInfo(s).fileName(), QStringLiteral("nada de %1 reconhecido aqui").arg(sys.name));
    }
    if (!cancelled() && !found.isEmpty())
        install();
    if (cancelled())
        wasCancelled = true;
    QDir(tmpRoot).removeRecursively();
    current.clear();
    progress(1, QStringLiteral("Concluído"), true);
}

}  // namespace

// ==========================================================================

BiosManager::BiosManager(Library *library, Runner *runner, QObject *parent)
    : QObject(parent), m_lib(library), m_runner(runner)
{
    connect(m_runner, &Runner::runningChanged, this, [this] {
        if (m_runner->running() || m_installing.isEmpty())
            return;
        SystemDef sys;
        const QString id = m_installing;
        m_installing.clear();
        if (!m_lib->systemDef(id, &sys))
            return;
        const QString now = rpcs3Firmware(pathsFor(sys).devFlash);
        if (!now.isEmpty() && now != m_fwBefore) {
            QFile::remove(stagedPup());
            emit message(QStringLiteral("Firmware do PS3 %1 instalado.").arg(now));
        } else if (!now.isEmpty() && !m_fwBefore.isEmpty()) {
            QFile::remove(stagedPup());
            emit message(QStringLiteral("O firmware %1 continua instalado.").arg(now));
        } else {
            emit message(QStringLiteral("O firmware não foi instalado. Tente de novo em Configurar ▸ BIOS e firmware."));
        }
        bump();
    });
}

BiosManager::~BiosManager()
{
    if (m_thread) {
        m_cancel = true;
        m_thread->wait(10000);
    }
}

void BiosManager::bump()
{
    ++m_revision;
    emit changed();
}

QString BiosManager::stagedPup() const
{
    return xdg("XDG_CACHE_HOME", "/.cache") + "/console-launcher/firmware/PS3UPDAT.PUP";
}

BiosManager::Kind BiosManager::kindOf(const SystemDef &sys)
{
    QString t = sys.bios.value("type").toString().toLower();
    if (t.isEmpty()) {
        const QString e = sys.emulator.toLower();
        if (e.contains("duckstation")) t = "duckstation";
        else if (e.contains("pcsx2")) t = "pcsx2";
        else if (e.contains("rpcs3")) t = "rpcs3";
        else if (e.contains("shadps4")) t = "shadps4";
        else if (e.contains("xemu")) t = "xemu";
        else if (e.contains("dolphin") && sys.id == "gc") t = "dolphin-gc";
        else if (e.contains("mgba")) t = "mgba";
        else if (e.contains("eden") || e.contains("yuzu") || e.contains("sudachi") || e.contains("citron")) t = "eden";
    }
    if (t == "duckstation") return DuckStation;
    if (t == "pcsx2") return Pcsx2;
    if (t == "rpcs3") return Rpcs3;
    if (t == "shadps4") return ShadPs4;
    if (t == "xemu") return Xemu;
    if (t == "dolphin-gc") return DolphinGc;
    if (t == "eden" || t == "yuzu") return Eden;
    if (t == "mgba") return Mgba;
    return None;
}

BiosManager::Paths BiosManager::pathsFor(const SystemDef &sys) const
{
    Paths p;
    p.kind = kindOf(sys);
    const QString data = xdg("XDG_DATA_HOME", "/.local/share");
    const QString config = xdg("XDG_CONFIG_HOME", "/.config");
    const QString over = sys.bios.value("dir").toString();
    switch (p.kind) {
    case DuckStation:
        p.base = data + "/duckstation";
        p.config = p.base + "/settings.ini";
        p.biosDir = over.isEmpty() ? underBase(p.base, EmuConfig::getIni(p.config, "BIOS", "SearchDirectory"), "bios")
                                   : expandHome(over);
        break;
    case Pcsx2:
        p.base = config + "/PCSX2";
        p.config = p.base + "/inis/PCSX2.ini";
        p.biosDir = over.isEmpty() ? underBase(p.base, EmuConfig::getIni(p.config, "Folders", "Bios"), "bios")
                                   : expandHome(over);
        break;
    case Rpcs3: {
        p.base = over.isEmpty() ? config + "/rpcs3" : expandHome(over);
        QString flash;
        for (const QString &vfs : {p.base + "/config/vfs.yml", p.base + "/vfs.yml"}) {
            QFile f(vfs);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            for (const QString &l : QString::fromUtf8(f.readAll()).split('\n'))
                if (l.trimmed().startsWith("/dev_flash/:")) {
                    flash = l.trimmed().mid(12).trimmed().remove('"').remove('\'');
                    break;
                }
            break;
        }
        flash.replace("$(EmulatorDir)", p.base + '/');
        p.devFlash = QDir::cleanPath(flash.isEmpty() ? p.base + "/dev_flash" : expandHome(flash));
        p.biosDir = p.devFlash;
        break;
    }
    case ShadPs4: {
        QString exeDir;
        QString problem;
        const QString exe = m_lib->emulatorPath(sys.id, &problem);
        if (!exe.isEmpty())
            exeDir = QFileInfo(exe).absolutePath();
        if (!over.isEmpty())
            p.base = expandHome(over);
        else if (!exeDir.isEmpty() && QFileInfo(exeDir + "/user").isDir())
            p.base = exeDir + "/user";  // modo portátil (pasta user ao lado do emulador)
        else
            p.base = data + "/shadPS4";
        p.biosDir = p.base + "/sys_modules";
        p.fontsDir = p.base + "/fonts";
        break;
    }
    case Xemu:
        p.base = over.isEmpty() ? data + "/xemu/xemu" : expandHome(over);
        p.config = p.base + "/xemu.toml";
        p.biosDir = p.base;
        break;
    case DolphinGc:
        if (!over.isEmpty())
            p.base = expandHome(over);
        else if (sys.exe == "flatpak" || sys.args.contains("org.DolphinEmu.dolphin-emu"))
            p.base = QDir::homePath() + "/.var/app/org.DolphinEmu.dolphin-emu/data/dolphin-emu";
        else
            p.base = data + "/dolphin-emu";
        p.gcRoot = p.base + "/GC";
        p.biosDir = p.gcRoot;
        break;
    case Mgba: {
        // AppImage/pacote: ~/.config/mgba; Flatpak: dentro de ~/.var/app
        const QString flat = QDir::homePath() + "/.var/app/io.mgba.mGBA/config/mgba";
        QString problem;
        const QString exe = m_lib->emulatorPath(sys.id, &problem);
        if (!over.isEmpty())
            p.base = expandHome(over);
        else if (QFileInfo(exe).fileName() == "flatpak" || (!QFileInfo::exists(config + "/mgba") && QFileInfo::exists(flat)))
            p.base = flat;
        else
            p.base = config + "/mgba";
        p.config = p.base + "/config.ini";
        p.biosDir = p.base + "/bios";
        break;
    }
    case Eden: {
        QString name = sys.emulator.toLower().simplified().remove(' ');
        if (name.isEmpty())
            name = "eden";
        p.base = over.isEmpty() ? data + '/' + name : expandHome(over);
        p.keysDir = p.base + "/keys";
        p.ncaDir = p.base + "/nand/system/Contents/registered";
        p.biosDir = p.keysDir;
        break;
    }
    case None:
        break;
    }
    return p;
}

bool BiosManager::handles(const QString &systemId) const
{
    SystemDef sys;
    return m_lib->systemDef(systemId, &sys) && kindOf(sys) != None;
}

QString BiosManager::folder(const QString &systemId) const
{
    SystemDef sys;
    if (!m_lib->systemDef(systemId, &sys))
        return {};
    const Paths p = pathsFor(sys);
    return p.kind == Eden ? p.base : p.biosDir;
}

QVariantMap BiosManager::state(const QString &systemId) const
{
    SystemDef sys;
    if (!m_lib->systemDef(systemId, &sys))
        return {};
    const Paths p = pathsFor(sys);
    QVariantList items;
    auto add = [&](const QString &label, const QString &detail, const QString &st) {
        items << QVariantMap{{"label", label}, {"detail", detail}, {"state", st}};
    };
    QString need = "required", summary;
    bool ok = false;

    switch (p.kind) {
    case None:
        return {{"need", "none"}, {"ok", true}, {"summary", QStringLiteral("Não precisa de BIOS")}, {"items", QVariantList{}}};
    case DuckStation:
    case Pcsx2: {
        QSet<QString> regions;
        const QString active = p.kind == Pcsx2 ? EmuConfig::getIni(p.config, "Filenames", "BIOS") : QString();
        const QFileInfoList files = QDir(p.biosDir).entryInfoList(QDir::Files, QDir::Name);
        QString activeLabel;
        for (const QFileInfo &fi : files) {
            const Ident id = identify(p.kind, fi);
            if (id.role != "bios")
                continue;
            ok = true;
            regions.insert(id.region);
            const bool isActive = p.kind == Pcsx2 && fi.fileName() == active;
            if (isActive)
                activeLabel = id.label;
            add(id.label, fi.fileName() + (isActive ? QStringLiteral(" · em uso") : QString()), "ok");
        }
        if (!ok) {
            add(QStringLiteral("Nenhuma BIOS encontrada"), tildePath(p.biosDir), "missing");
            summary = QStringLiteral("Falta a BIOS");
        } else if (p.kind == DuckStation) {
            QStringList r;
            for (const char *x : {"U", "E", "J"})
                if (regions.contains(x) || regions.contains("*"))
                    r << regionName(x);
            summary = QStringLiteral("BIOS para: %1").arg(r.isEmpty() ? QStringLiteral("região desconhecida") : r.join(", "));
            if (!regions.contains("*"))
                for (const char *x : {"U", "E", "J"})
                    if (!regions.contains(x))
                        add(QStringLiteral("Sem BIOS da região %1").arg(regionName(x)),
                            QStringLiteral("Jogos dessa região podem não abrir"), "warn");
        } else {
            summary = activeLabel.isEmpty() ? QStringLiteral("%1 BIOS · o PCSX2 escolhe").arg(items.size()) : activeLabel;
        }
        break;
    }
    case Rpcs3: {
        const QString fw = rpcs3Firmware(p.devFlash);
        ok = !fw.isEmpty();
        if (ok)
            add(QStringLiteral("Firmware %1 instalado").arg(fw), tildePath(p.devFlash), "ok");
        else
            add(QStringLiteral("Firmware não instalado"), QStringLiteral("Importe o PS3UPDAT.PUP"), "missing");
        if (QFileInfo::exists(stagedPup()))
            add(QStringLiteral("Firmware aguardando instalação"), QStringLiteral("Use \"Instalar firmware agora\""), "warn");
        summary = ok ? QStringLiteral("Firmware %1").arg(fw) : QStringLiteral("Falta o firmware");
        break;
    }
    case ShadPs4: {
        need = "optional";
        QStringList have, missing;
        for (const QString &m : kPs4Modules)
            (QFileInfo::exists(p.biosDir + '/' + m) ? have : missing) << m;
        const int extra = QDir(p.biosDir).entryList({"*.sprx"}, QDir::Files).size() - have.size();
        ok = !have.isEmpty() || extra > 0;
        add(QStringLiteral("Módulos do sistema: %1 de %2").arg(have.size()).arg(kPs4Modules.size())
                + (extra > 0 ? QStringLiteral(" (+%1 outros)").arg(extra) : QString()),
            missing.isEmpty() ? tildePath(p.biosDir) : QStringLiteral("Faltam: ") + missing.join(", "),
            missing.isEmpty() ? "ok" : (have.isEmpty() ? "missing" : "warn"));
        const bool fonts = QFileInfo(p.fontsDir + "/font").isDir() || QFileInfo(p.fontsDir + "/font2").isDir();
        add(fonts ? QStringLiteral("Fontes do sistema instaladas") : QStringLiteral("Sem as fontes do sistema"),
            tildePath(p.fontsDir), fonts ? "ok" : "info");
        summary = ok ? QStringLiteral("%1 módulo(s)").arg(have.size() + qMax(0, extra))
                     : QStringLiteral("Opcional: módulos do sistema");
        break;
    }
    case Xemu: {
        struct Need { const char *key, *label; bool required; };
        const Need needs[] = {{"bootrom_path", "Boot ROM (MCPX 1.0)", true},
                              {"flashrom_path", "BIOS (flash)", true},
                              {"hdd_path", "Disco rígido (.qcow2)", true},
                              {"eeprom_path", "EEPROM", false}};
        int missing = 0;
        for (const Need &n : needs) {
            const QString v = EmuConfig::getTomlString(p.config, "sys.files", n.key);
            const bool there = !v.isEmpty() && QFileInfo(expandHome(v)).isFile();
            if (!there && n.required)
                ++missing;
            QString detail = there ? tildePath(expandHome(v))
                             : n.required ? QStringLiteral("Faltando") : QStringLiteral("Opcional: o xemu cria uma");
            if (!there && QLatin1String(n.key) == QLatin1String("hdd_path"))
                detail = QStringLiteral("Faltando — o projeto xemu publica uma imagem vazia (xemu-hdd-image)");
            add(QString::fromUtf8(n.label), detail, there ? "ok" : n.required ? "missing" : "info");
        }
        ok = missing == 0;
        summary = ok ? QStringLiteral("Tudo pronto") : QStringLiteral("Faltam %1 arquivo(s)").arg(missing);
        break;
    }
    case DolphinGc: {
        need = "optional";
        int n = 0;
        for (const char *r : {"USA", "EUR", "JAP"}) {
            const QString f = p.gcRoot + '/' + r + "/IPL.bin";
            const bool there = QFileInfo::exists(f);
            n += there;
            add(QStringLiteral("IPL %1").arg(r), there ? tildePath(f) : QStringLiteral("Opcional"), there ? "ok" : "info");
        }
        ok = n > 0;
        summary = ok ? QStringLiteral("IPL de %1 região(ões)").arg(n) : QStringLiteral("Opcional: funciona sem BIOS");
        break;
    }
    case Mgba: {
        need = "optional";
        int n = 0;
        for (const KnownDump &k : kGameBoy) {
            const QString f = p.biosDir + '/' + k.file;
            const bool there = QFileInfo::exists(f);
            n += there;
            add(QStringLiteral("BIOS do %1").arg(k.model), there ? tildePath(f) : QStringLiteral("Opcional"), there ? "ok" : "info");
        }
        ok = n > 0;
        summary = ok ? QStringLiteral("%1 BIOS instalada(s)").arg(n) : QStringLiteral("Opcional: o mGBA tem uma embutida");
        break;
    }
    case Eden: {
        const bool prod = QFileInfo::exists(p.keysDir + "/prod.keys");
        const bool title = QFileInfo::exists(p.keysDir + "/title.keys");
        const int nca = ncaCount(p.ncaDir);
        add(QStringLiteral("prod.keys"), prod ? tildePath(p.keysDir) : QStringLiteral("Faltando"), prod ? "ok" : "missing");
        add(QStringLiteral("title.keys"), title ? tildePath(p.keysDir) : QStringLiteral("Opcional"), title ? "ok" : "info");
        add(QStringLiteral("Firmware"), nca > 0 ? QStringLiteral("%1 arquivos .nca").arg(nca) : QStringLiteral("Faltando"),
            nca >= kMinFirmwareNca ? "ok" : nca > 0 ? "warn" : "missing");
        ok = prod && nca >= kMinFirmwareNca;
        summary = ok ? QStringLiteral("Chaves e firmware instalados")
                     : !prod && nca == 0 ? QStringLiteral("Faltam chaves e firmware")
                     : !prod ? QStringLiteral("Faltam as chaves (prod.keys)") : QStringLiteral("Falta o firmware");
        break;
    }
    }
    return {{"need", need}, {"ok", ok}, {"summary", summary}, {"dir", tildePath(folder(systemId))}, {"items", items}};
}

bool BiosManager::ready(const QString &systemId) const
{
    const QVariantMap s = state(systemId);
    return s.value("need").toString() != "required" || s.value("ok").toBool();
}

static QVariantMap row(const QString &type, const QString &key, const QString &label, const QString &detail = {})
{
    return {{"type", type}, {"key", key}, {"label", label}, {"detail", detail}};
}

QVariantList BiosManager::optionRows(const QString &systemId) const
{
    QVariantList rows;
    SystemDef sys;
    if (!m_lib->systemDef(systemId, &sys) || kindOf(sys) == None)
        return rows;
    const Paths p = pathsFor(sys);
    const QVariantMap st = state(systemId);
    rows << row("header", "h_bios", p.kind == Rpcs3 || p.kind == Eden ? "BIOS e firmware" : "BIOS");
    const QString icon = QStringLiteral("%1");
    for (const QVariant &v : st.value("items").toList()) {
        const QVariantMap it = v.toMap();
        const QString s = it.value("state").toString();
        const QString mark = s == "ok" ? "✓ " : s == "missing" ? "✗ " : s == "warn" ? "⚠ " : "· ";
        rows << row("info", "_bi" + QString::number(rows.size()), mark + it.value("label").toString(),
                    it.value("detail").toString());
    }
    Q_UNUSED(icon);
    if (p.kind == Pcsx2) {
        QVariantList opts{QVariantMap{{"label", "Automático (o PCSX2 escolhe)"}, {"value", ""}}};
        for (const QFileInfo &fi : QDir(p.biosDir).entryInfoList(QDir::Files, QDir::Name)) {
            const Ident id = identify(p.kind, fi);
            if (id.role == "bios")
                opts << QVariantMap{{"label", id.label}, {"value", fi.fileName()}};
        }
        if (opts.size() > 1) {
            QVariantMap r = row("choice", "_biosActive", "BIOS em uso",
                                QFileInfo::exists(p.config) ? QString() : QStringLiteral("⚠ Abra o PCSX2 uma vez para criar a configuração"));
            r.insert("options", opts);
            r.insert("value", EmuConfig::getIni(p.config, "Filenames", "BIOS"));
            rows << r;
        }
    }
    if (p.kind == Rpcs3 && firmwarePending(systemId))
        rows << row("action", "_fwInstall", "Instalar firmware agora", "Abre o RPCS3 para instalar o firmware importado");
    const QString what = p.kind == Rpcs3 ? "Importar firmware (PS3UPDAT.PUP)"
                         : p.kind == Eden ? "Importar chaves e firmware"
                         : p.kind == ShadPs4 ? "Importar módulos do sistema"
                         : p.kind == Xemu ? "Importar BIOS, MCPX e disco"
                                          : "Importar BIOS";
    rows << row("action", "_biosImport", what, QStringLiteral("De uma pasta, pendrive ou arquivo compactado"));
    rows << row("action", "_biosFolder",
                p.kind == Rpcs3 || p.kind == Eden || p.kind == ShadPs4 ? "Abrir a pasta do firmware" : "Abrir a pasta da BIOS",
                tildePath(folder(systemId)));
    return rows;
}

QVariantList BiosManager::overviewRows() const
{
    QVariantList rows;
    QStringList none;
    rows << row("header", "h_need", "Por console");
    for (const SystemDef &s : m_lib->systemDefs()) {
        if (kindOf(s) == None) {
            none << s.name;
            continue;
        }
        const QVariantMap st = state(s.id);
        const bool ok = st.value("ok").toBool();
        const QString need = st.value("need").toString();
        const QString mark = ok ? "✓ " : need == "required" ? "⚠ " : "· ";
        rows << row("action", "bios:" + s.id, s.name,
                    mark + st.value("summary").toString() + QStringLiteral(" · A para importar"));
    }
    if (!none.isEmpty())
        rows << row("info", "none", QStringLiteral("Não precisam de BIOS"), none.join(", "));
    rows << row("info", "legal", QStringLiteral("De onde vêm esses arquivos?"),
                QStringLiteral("Faça o dump do seu próprio console. O launcher não baixa nem distribui BIOS, chaves ou firmware."));
    return rows;
}

void BiosManager::setOption(const QString &systemId, const QString &key, const QString &value)
{
    SystemDef sys;
    if (!m_lib->systemDef(systemId, &sys))
        return;
    const Paths p = pathsFor(sys);
    if (key == "_biosActive" && p.kind == Pcsx2) {
        QString err;
        if (!EmuConfig::setIni(p.config, "Filenames", "BIOS", value, &err))
            emit message(QStringLiteral("Não foi possível mudar a BIOS: %1").arg(err));
        bump();
    }
}

QVariantList BiosManager::detect(const QString &path) const
{
    QVariantList out;
    const QFileInfo fi(path);
    if (!fi.isFile() || fi.size() > 1024 * MB)
        return out;
    for (const SystemDef &s : m_lib->systemDefs()) {
        const Kind k = kindOf(s);
        if (k == None || quickBadge(k, fi).isEmpty())
            continue;
        const Ident id = identify(k, fi);
        if (!id.role.isEmpty() && id.role != "nca")
            out << QVariantMap{{"id", s.id}, {"name", s.name}, {"label", id.label}};
    }
    return out;
}

bool BiosManager::firmwarePending(const QString &systemId) const
{
    SystemDef sys;
    return m_lib->systemDef(systemId, &sys) && kindOf(sys) == Rpcs3 && QFileInfo::exists(stagedPup());
}

QString BiosManager::installFirmware(const QString &systemId)
{
    SystemDef sys;
    if (!m_lib->systemDef(systemId, &sys) || kindOf(sys) != Rpcs3)
        return QStringLiteral("Este console não instala firmware assim.");
    if (!QFileInfo::exists(stagedPup()))
        return QStringLiteral("Importe o PS3UPDAT.PUP primeiro.");
    if (m_runner->running())
        return QStringLiteral("Feche o jogo aberto antes de instalar o firmware.");
    m_fwBefore = rpcs3Firmware(pathsFor(sys).devFlash);
    const QVariantMap info{{"title", QStringLiteral("Instalando o firmware do PS3")}, {"mode", "firmware"}};
    QString err;
    if (!m_lib->startEmulator(systemId, {"--installfw", stagedPup()}, info, &err))
        return err;
    m_installing = systemId;
    return {};
}

// ------------------------------------------------------------------ navegação
QString BiosManager::startDir(const QString &systemId) const
{
    const QString saved = QSettings().value("biosDir/" + systemId).toString();
    if (!saved.isEmpty() && QFileInfo(saved).isDir())
        return saved;
    const QString dl = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return QFileInfo(dl).isDir() ? dl : QDir::homePath();
}

QVariantMap BiosManager::listDir(const QString &path, const QString &systemId) const
{
    SystemDef sys;
    if (!m_lib->systemDef(systemId, &sys))
        return {{"error", "Console desconhecido"}};
    const Kind kind = kindOf(sys);
    const QDir d(path);
    if (!d.exists() || !d.isReadable())
        return {{"error", QStringLiteral("Não foi possível abrir %1").arg(tildePath(path))}};
    QVariantList entries;
    int hidden = 0;
    for (const QFileInfo &fi : d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                               QDir::DirsFirst | QDir::Name | QDir::IgnoreCase)) {
        if (fi.fileName().startsWith('.'))
            continue;
        if (fi.isDir()) {
            QString badge;
            const QString n = fi.fileName().toLower();
            if (kind == ShadPs4 && (n == "font" || n == "font2"))
                badge = "fontes";
            else if (kind == Eden && ncaCount(fi.absoluteFilePath(), kMinFirmwareNca) >= kMinFirmwareNca)
                badge = "firmware";
            entries << QVariantMap{{"name", fi.fileName()}, {"path", fi.absoluteFilePath()}, {"kind", "dir"},
                                   {"gameRoot", !badge.isEmpty()}, {"badge", badge},
                                   {"protected", Importer::isProtectedDir(fi.absoluteFilePath())}, {"sizeText", ""}};
            continue;
        }
        QString kindStr, badge;
        if (isArchiveName(fi.fileName())) {
            if (fi.size() <= 2048 * MB)
                kindStr = "archive";
        } else if (!(badge = quickBadge(kind, fi)).isEmpty()) {
            kindStr = "game";
        }
        if (kindStr.isEmpty()) {
            ++hidden;
            continue;
        }
        entries << QVariantMap{{"name", fi.fileName()}, {"path", fi.absoluteFilePath()}, {"kind", kindStr},
                               {"gameRoot", false}, {"protected", false}, {"badge", badge},
                               {"sizeText", humanSize(fi.size())}};
    }
    QString parent;
    QDir up(d.absolutePath());
    if (up.cdUp() && up.absolutePath() != d.absolutePath())
        parent = up.absolutePath();
    return {{"path", d.absolutePath()}, {"display", tildePath(d.absolutePath())}, {"parent", parent},
            {"entries", entries}, {"hidden", hidden}, {"isDest", false}};
}

QString BiosManager::start(const QString &systemId, const QStringList &paths, bool cleanup)
{
    Q_UNUSED(cleanup);  // a BIOS é copiada: a origem fica como está
    if (m_thread)
        return QStringLiteral("Já existe uma importação de BIOS em andamento.");
    SystemDef sys;
    if (!m_lib->systemDef(systemId, &sys) || kindOf(sys) == None)
        return QStringLiteral("Este console não precisa de BIOS.");
    if (paths.isEmpty())
        return QStringLiteral("Nada selecionado.");
    QStringList sources;
    for (const QString &raw : paths) {
        const QString p = QDir::cleanPath(QFileInfo(raw).absoluteFilePath());
        if (!Importer::isAllowedSource(p))
            return QStringLiteral("Só é possível importar da pasta pessoal ou de discos externos (%1).").arg(tildePath(p));
        if (QFileInfo(p).isDir() && Importer::isProtectedDir(p))
            return QStringLiteral("Escolha os arquivos dentro de %1, não a pasta inteira.").arg(tildePath(p));
        sources << p;
    }
    QSettings().setValue("biosDir/" + systemId, QFileInfo(sources.first()).absolutePath());

    auto job = std::make_shared<BiosJob>();
    job->mgr = this;
    job->sys = sys;
    job->paths = pathsFor(sys);
    job->sources = sources;
    job->tmpRoot = xdg("XDG_CACHE_HOME", "/.cache") + "/console-launcher/bios-tmp-"
                   + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    job->pupStage = stagedPup();

    m_cancel = false;
    m_progress = 0;
    m_status = QStringLiteral("Preparando…");
    m_item = QFileInfo(sources.first()).fileName();
    emit progressChanged();

    m_thread = QThread::create([job] { job->run(); });
    connect(m_thread, &QThread::finished, this, [this, job] {
        m_thread->deleteLater();
        m_thread = nullptr;
        QVariantMap report{{"imported", job->imported}, {"skipped", job->skipped},  {"notes", job->notes},
                           {"present", job->present},
                           {"deleted", 0},              {"freedText", QString()},   {"cancelled", job->wasCancelled},
                           {"systemId", job->sys.id},   {"bios", true},             {"firmware", job->firmware}};
        emit busyChanged();
        bump();
        emit finished(report);
    });
    m_thread->start();
    emit busyChanged();
    return {};
}

void BiosManager::cancel() { m_cancel = true; }

void BiosManager::postProgress(double p, const QString &status, const QString &item)
{
    QMetaObject::invokeMethod(this, [this, p, status, item] {
        m_progress = p;
        m_status = status;
        m_item = item;
        emit progressChanged();
    }, Qt::QueuedConnection);
}
