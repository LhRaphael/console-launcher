#include "library.h"
#include "gamemode.h"
#include "emuconfig.h"
#include "runner.h"
#include "sfo.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace {

constexpr int kMaxRecent = 24;
constexpr int kMaxDepth = 3;  // ps2/Pasta/Sub/jogo.iso
const QChar kSep(0x1f);
const QString kInherit = QStringLiteral("__inherit__");

QStringList toStringList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray())
        out << x.toString();
    return out;
}

QString firstExisting(const QStringList &candidates)
{
    for (const QString &c : candidates)
        if (QFileInfo(c).isFile())
            return c;
    return {};
}

QString coverFor(const QString &dir, const QString &base, const QString &systemDir)
{
    static const QStringList exts{"png", "jpg", "jpeg", "webp"};
    QStringList c;
    for (const QString &e : exts) {
        c << dir + '/' + base + '.' + e;
        c << systemDir + "/media/covers/" + base + '.' + e;
    }
    return firstExisting(c);
}

// Capa mais larga que alta (ex.: ICON0.PNG do PS3): lê só o cabeçalho da imagem.
bool isWide(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QSize sz = QImageReader(path).size();
    return sz.isValid() && sz.width() > sz.height() * 1.05;
}

QString fileUrl(const QString &path)
{
    return path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString();
}

QJsonObject readJson(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return {};
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("JSON inválido (%1, posição %2)").arg(pe.errorString()).arg(pe.offset);
        return {};
    }
    return doc.object();
}

QJsonObject findOption(const QJsonObject &setting, const QString &value)
{
    for (const QJsonValue &o : setting.value("options").toArray())
        if (o.toObject().value("value").toString() == value)
            return o.toObject();
    return {};
}

QString optionLabel(const QJsonObject &setting, const QString &value)
{
    const QJsonObject o = findOption(setting, value);
    return o.isEmpty() ? value : o.value("label").toString();
}

QVariantList optionsOf(const QJsonObject &setting)
{
    QVariantList out;
    for (const QJsonValue &o : setting.value("options").toArray())
        out << QVariantMap{{"label", o.toObject().value("label").toString()},
                           {"value", o.toObject().value("value").toString()}};
    return out;
}

QVariantMap row(const QString &type, const QString &key, const QString &label,
                const QString &detail = QString())
{
    return {{"type", type}, {"key", key}, {"label", label}, {"detail", detail}};
}

}  // namespace

QString expandHome(QString p)
{
    if (p == "~")
        return QDir::homePath();
    if (p.startsWith("~/"))
        p.replace(0, 1, QDir::homePath());
    return QDir::cleanPath(p);
}

QString tildePath(const QString &p)
{
    const QString home = QDir::homePath();
    if (p == home)
        return "~";
    if (p.startsWith(home + '/'))
        return '~' + p.mid(home.size());
    return p;
}

void splitGameTitle(const QString &base, QString *title, QStringList *tags)
{
    static const QRegularExpression tagRe(R"(\s*[\(\[]([^\)\]]*)[\)\]])");
    QString t = base;
    t.replace('_', ' ');
    auto it = tagRe.globalMatch(t);
    while (it.hasNext()) {
        const QString tag = it.next().captured(1).trimmed();
        if (!tag.isEmpty() && tags)
            tags->append(tag);
    }
    t.remove(tagRe);
    *title = t.simplified().isEmpty() ? base : t.simplified();
}

Library::Library(const QString &configOverride, Runner *runner, QObject *parent)
    : QObject(parent), m_runner(runner)
{
    QSettings st;
    m_overlayEnabled = st.value("overlay", false).toBool();
    m_lastSystem = st.value("lastSystem").toString();

    const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                           + "/console-launcher";
    QDir().mkpath(cfgDir);
    m_storePath = cfgDir + "/settings.json";
    if (QFileInfo::exists(m_storePath)) {
        QString e;
        m_store = readJson(m_storePath, &e);
        if (!e.isEmpty())
            m_startupMessage = QStringLiteral("Erro em %1: %2").arg(tildePath(m_storePath), e);
    }

    QString error;
    if (!configOverride.isEmpty()) {
        m_configPath = QFileInfo(configOverride).absoluteFilePath();
    } else {
        m_configPath = cfgDir + "/systems.json";
        ensureUserConfig();
    }
    if (!loadConfig(m_configPath, &error)) {
        m_startupMessage = QStringLiteral("Erro em %1: %2 — usando a configuração padrão.")
                               .arg(tildePath(m_configPath), error);
        QString ignored;
        loadConfig(":/data/systems.json", &ignored);
    }

    // Em uma instalação nova as pastas de ROM ainda não existem: cria todas,
    // para o usuário saber onde colocar cada jogo.
    for (const SystemDef &s : m_defs)
        QDir().mkpath(systemPath(s));

    // Runner::failed é tratado na interface (abre o registro do jogo)

    // Apps instalados/removidos (apt, flatpak) aparecem sozinhos.
    m_appTimer.setSingleShot(true);
    m_appTimer.setInterval(1500);
    connect(&m_appTimer, &QTimer::timeout, this, [this] {
        scanApps();
        rebuildSystemList();
        ++m_revision;
        emit revisionChanged();
        emit systemsChanged();
    });
    for (const QString &d : applicationDirs())
        if (QFileInfo(d).isDir())
            m_appWatch.addPath(d);
    connect(&m_appWatch, &QFileSystemWatcher::directoryChanged, &m_appTimer,
            qOverload<>(&QTimer::start));

    scanApps();
    scan();
}

void Library::ensureUserConfig()
{
    QString e;
    const int want = readJson(":/data/systems.json", &e).value("configVersion").toInt(1);
    if (QFileInfo::exists(m_configPath)) {
        const QJsonObject user = readJson(m_configPath, &e);
        if (!e.isEmpty() || user.value("configVersion").toInt(1) >= want)
            return;  // atualizado (ou com erro, que aparece ao carregar)
        const QString bak = m_configPath + ".bak";
        QFile::remove(bak);
        QFile::rename(m_configPath, bak);
        m_startupMessage = QStringLiteral("Configuração dos consoles atualizada (a anterior foi salva em %1).")
                               .arg(tildePath(bak));
    }
    if (QFile::copy(":/data/systems.json", m_configPath))
        QFile(m_configPath).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ReadGroup | QFileDevice::ReadOther);
}

bool Library::loadConfig(const QString &path, QString *error)
{
    const QJsonObject root = readJson(path, error);
    if (root.isEmpty())
        return false;

    QList<SystemDef> defs;
    QSet<QString> seen;
    for (const QJsonValue &v : root.value("systems").toArray()) {
        const QJsonObject o = v.toObject();
        SystemDef s;
        s.id = o.value("id").toString().trimmed();
        if (s.id.isEmpty() || s.id == "recent" || s.id == "apps" || seen.contains(s.id))
            continue;
        seen.insert(s.id);
        s.name = o.value("name").toString(s.id);
        s.shortName = o.value("short").toString(s.name);
        s.color = o.value("color").toString("#4c8dff");
        s.dir = o.value("dir").toString(s.id);
        s.emulator = o.value("emulator").toString();
        s.exe = o.value("exe").toString();
        s.args = toStringList(o.value("args"));
        s.openArgs = toStringList(o.value("openArgs"));
        s.extensions = toStringList(o.value("extensions"));
        s.folderMarkers = toStringList(o.value("folderMarkers"));
        s.fallback = toStringList(o.value("fallback"));
        s.settings = o.value("settings").toArray();
        s.overlay = o.value("overlay").toBool(true);
        s.bios = o.value("bios").toObject();
        defs << s;
    }
    if (defs.isEmpty()) {
        *error = QStringLiteral("nenhum console em \"systems\"");
        return false;
    }

    m_defs = defs;
    m_romRoot = expandHome(root.value("romRoot").toString("~/ROMs"));
    m_emuDir = expandHome(root.value("emulatorDir").toString("~/emulators"));
    m_showEmpty = root.value("showEmptySystems").toBool(true);
    const QString wrapper = root.value("overlayWrapper").toString("linosd-run");
    m_wrapperPath = wrapper.isEmpty() ? QString() : QStandardPaths::findExecutable(expandHome(wrapper));
    if (m_wrapperPath.isEmpty() && !wrapper.isEmpty() && QFileInfo(expandHome(wrapper)).isExecutable())
        m_wrapperPath = expandHome(wrapper);
    return true;
}

const SystemDef *Library::findSystem(const QString &id) const
{
    for (const SystemDef &s : m_defs)
        if (s.id == id)
            return &s;
    return nullptr;
}

bool Library::systemDef(const QString &id, SystemDef *out) const
{
    const SystemDef *s = findSystem(id);
    if (s)
        *out = *s;
    return s != nullptr;
}

// ------------------------------------------------------------------ settings.json

bool Library::lookup(const char *group, const QString &id, const QString &key, QString *out) const
{
    const QJsonValue v = m_store.value(QLatin1String(group)).toObject().value(id).toObject().value(key);
    if (!v.isString())
        return false;
    *out = v.toString();
    return true;
}

void Library::store(const char *group, const QString &id, const QString &key, const QString *value)
{
    QJsonObject g = m_store.value(QLatin1String(group)).toObject();
    QJsonObject o = g.value(id).toObject();
    if (value)
        o.insert(key, *value);
    else
        o.remove(key);
    if (o.isEmpty())
        g.remove(id);
    else
        g.insert(id, o);
    m_store.insert(QLatin1String(group), g);
    saveStore();
}

void Library::saveStore()
{
    QSaveFile f(m_storePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(m_store).toJson(QJsonDocument::Indented));
        if (f.commit())
            return;
    }
    emit message(QStringLiteral("Não foi possível salvar %1").arg(tildePath(m_storePath)));
}

QString Library::effective(const SystemDef &sys, const QString &gamePath, const QString &key,
                           const QString &fallback) const
{
    QString v;
    if (!gamePath.isEmpty() && lookup("games", gamePath, key, &v))
        return v;
    if (lookup("systems", sys.id, key, &v))
        return v;
    return fallback;
}

// ------------------------------------------------------------------ emuladores

QStringList Library::exeCandidates(const SystemDef &sys) const
{
    const QString spec = expandHome(sys.exe);
    if (!spec.contains(QRegularExpression("[*?\\[]")))
        return {};
    const QFileInfo fi(spec);
    const QString dir = QDir::isAbsolutePath(spec) ? fi.path() : m_emuDir;
    const QString pattern = QDir::isAbsolutePath(spec) ? fi.fileName() : spec;
    QStringList out;
    for (const QFileInfo &f : QDir(dir).entryInfoList({pattern}, QDir::Files, QDir::Time))
        out << f.fileName();
    return out;
}

static bool flatpakInstalled(const QString &id)
{
    for (const QString &root : {QDir::homePath() + "/.local/share/flatpak/app/", QStringLiteral("/var/lib/flatpak/app/")})
        if (QFileInfo::exists(root + id + "/current/active"))
            return true;
    return false;
}

QString Library::resolveFallback(const SystemDef &sys, QStringList *prefix) const
{
    for (const QString &alt : sys.fallback) {
        if (alt.startsWith("flatpak:")) {
            const QString id = alt.mid(8).trimmed();
            const QString flatpak = QStandardPaths::findExecutable("flatpak");
            if (id.isEmpty() || flatpak.isEmpty() || !flatpakInstalled(id))
                continue;
            if (prefix) {
                // o Flatpak só enxerga a pasta dos jogos se ela for liberada
                *prefix = QStringList{"run"};
                if (!m_romRoot.isEmpty())
                    *prefix << "--filesystem=" + m_romRoot;
                *prefix << id;
            }
            return flatpak;
        }
        const QString p = QStandardPaths::findExecutable(expandHome(alt));
        if (!p.isEmpty())
            return p;
    }
    return {};
}

QString Library::resolveExe(const SystemDef &sys, QString *problem, QStringList *prefix) const
{
    if (prefix)
        prefix->clear();
    auto checkExec = [&](const QString &path) -> QString {
        if (!QFileInfo(path).isExecutable()) {
            *problem = QStringLiteral("%1 sem permissão de execução (rode setup-emuladores.sh)")
                           .arg(QFileInfo(path).fileName());
            return {};
        }
        return path;
    };

    // Versão escolhida nas opções do console
    QString chosen;
    if (lookup("systems", sys.id, "_exe", &chosen) && !chosen.isEmpty()) {
        const QString p = QDir::isAbsolutePath(chosen) ? chosen : m_emuDir + '/' + chosen;
        if (QFileInfo(p).isFile())
            return checkExec(p);
    }

    const QString spec = expandHome(sys.exe);
    if (spec.isEmpty()) {
        *problem = QStringLiteral("nenhum emulador configurado");
        return {};
    }
    if (spec.contains(QRegularExpression("[*?\\[]"))) {
        // Padrão como "pcsx2-*.AppImage": fica com o arquivo mais novo
        // (sobrevive a atualizações de versão).
        const QStringList hits = exeCandidates(sys);
        if (hits.isEmpty()) {
            const QString alt = resolveFallback(sys, prefix);
            if (!alt.isEmpty())
                return alt;
            const QFileInfo fi(spec);
            *problem = QStringLiteral("%1 não encontrado em %2")
                           .arg(QDir::isAbsolutePath(spec) ? fi.fileName() : spec,
                                tildePath(QDir::isAbsolutePath(spec) ? fi.path() : m_emuDir));
            if (!sys.fallback.isEmpty())
                *problem += QStringLiteral(" (nem instalado pelo sistema/Flatpak)");
            return {};
        }
        const QString dir = QDir::isAbsolutePath(spec) ? QFileInfo(spec).path() : m_emuDir;
        return checkExec(dir + '/' + hits.first());
    }
    if (spec.contains('/')) {
        if (!QFileInfo::exists(spec)) {
            *problem = QStringLiteral("%1 não existe").arg(tildePath(spec));
            return {};
        }
        return checkExec(spec);
    }
    const QString local = m_emuDir + '/' + spec;
    if (QFileInfo(local).isFile())
        return checkExec(local);
    QString inPath = QStandardPaths::findExecutable(spec);
    if (inPath.isEmpty())
        inPath = resolveFallback(sys, prefix);
    if (inPath.isEmpty())
        *problem = QStringLiteral("'%1' não está instalado").arg(spec);
    return inPath;
}

// ------------------------------------------------------------------ varredura

QVariantList Library::scanSystem(const SystemDef &sys) const
{
    const QString root = systemPath(sys);
    struct Item {
        QString title, original, path, cover, label;
        QStringList tags;
    };
    QList<Item> items;

    if (!sys.folderMarkers.isEmpty()) {
        // Jogos em pasta (PS3/PS4): uma subpasta por jogo, com o executável dentro.
        const QFileInfoList dirs = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &d : dirs) {
            const QString base = d.absoluteFilePath();
            QString exe;
            for (const QString &m : sys.folderMarkers)
                if (QFileInfo(base + '/' + m).isFile()) {
                    exe = base + '/' + m;
                    break;
                }
            if (exe.isEmpty())
                continue;
            Item it;
            it.path = exe;
            it.label = d.fileName() + '/';
            it.title = readSfoTitle(firstExisting({base + "/PS3_GAME/PARAM.SFO", base + "/PARAM.SFO",
                                                   base + "/sce_sys/param.sfo"}));
            if (it.title.isEmpty())  // sem PARAM.SFO: usa o nome da pasta
                splitGameTitle(d.fileName(), &it.title, &it.tags);
            it.cover = firstExisting({base + "/cover.png", base + "/cover.jpg", base + "/PS3_GAME/ICON0.PNG",
                                      base + "/ICON0.PNG", base + "/sce_sys/icon0.png"});
            if (it.cover.isEmpty())
                it.cover = coverFor(root, d.fileName(), root);
            items << it;
        }
    }

    if (!sys.extensions.isEmpty()) {
        QStringList filters;
        for (const QString &e : sys.extensions)
            filters << "*." + e;
        QStringList files;
        QDirIterator walk(root, filters, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (walk.hasNext()) {
            const QString p = walk.next();
            const QString rel = p.mid(root.size() + 1);
            if (rel.count('/') >= kMaxDepth || rel.startsWith("media/") || rel.startsWith('.')
                || rel.contains("/."))
                continue;  // ignora pastas ocultas (inclui .importando-*)
            files << p;
        }

        // Discos listados em um .m3u aparecem uma vez só (pelo .m3u).
        QSet<QString> inPlaylist;
        for (const QString &p : files) {
            if (!p.endsWith(".m3u", Qt::CaseInsensitive))
                continue;
            QFile m3u(p);
            if (!m3u.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QDir base = QFileInfo(p).dir();
            while (!m3u.atEnd()) {
                const QString line = QString::fromUtf8(m3u.readLine()).trimmed();
                if (!line.isEmpty() && !line.startsWith('#'))
                    inPlaylist.insert(QDir::cleanPath(base.absoluteFilePath(line)));
            }
        }

        for (const QString &p : files) {
            if (inPlaylist.contains(QDir::cleanPath(p)))
                continue;
            const QFileInfo fi(p);
            Item it;
            it.path = fi.absoluteFilePath();
            splitGameTitle(fi.completeBaseName(), &it.title, &it.tags);
            it.cover = coverFor(fi.absolutePath(), fi.completeBaseName(), root);
            items << it;
        }
    }

    for (Item &it : items) {
        it.original = it.title;
        QString custom;
        if (lookup("games", it.path, "_title", &custom) && !custom.trimmed().isEmpty())
            it.title = custom.trimmed();
    }

    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(items.begin(), items.end(),
              [&](const Item &a, const Item &b) { return coll.compare(a.title, b.title) < 0; });

    QVariantList out;
    for (const Item &it : items) {
        out << QVariantMap{{"title", it.title},
                           {"originalTitle", it.original},
                           {"tags", it.tags},
                           {"path", it.path},
                           {"file", it.label.isEmpty() ? QFileInfo(it.path).fileName() : it.label},
                           {"cover", fileUrl(it.cover)},
                           {"wide", isWide(it.cover)},
                           {"icon", ""},
                           {"system", sys.id},
                           {"systemName", sys.name}};
    }
    return out;
}

void Library::scanApps()
{
    m_apps = scanDesktopApps({"console-launcher.desktop"});
}

QVariantList Library::appList() const
{
    QVariantList out;
    for (const AppEntry &a : m_apps) {
        out << QVariantMap{{"title", a.name},
                           {"originalTitle", a.name},
                           {"comment", a.comment},
                           {"tags", QStringList()},
                           {"path", a.file},
                           {"file", a.id},
                           {"cover", ""},
                           {"wide", false},
                           {"icon", a.icon.isEmpty() ? QString()
                                    : "image://appicon/" + QString::fromUtf8(QUrl::toPercentEncoding(a.icon))},
                           {"system", "apps"},
                           {"systemName", "Aplicativos"}};
    }
    return out;
}

void Library::scan()
{
    m_games.clear();
    for (const SystemDef &s : m_defs)
        m_games.insert(s.id, scanSystem(s));
    rebuildSystemList();
    ++m_revision;
    emit revisionChanged();
    emit systemsChanged();
}

void Library::rescanSystem(const QString &id)
{
    if (const SystemDef *s = findSystem(id))
        m_games.insert(id, scanSystem(*s));
    rebuildSystemList();
    ++m_revision;
    emit revisionChanged();
    emit systemsChanged();
}

void Library::rebuildSystemList()
{
    QVariantList list;
    const QVariantList recent = recentGames();
    auto entry = [](const QString &id, const QString &name, const QString &shortName, const QString &color,
                    int count) {
        return QVariantMap{{"id", id},         {"name", name},     {"short", shortName}, {"color", color},
                           {"count", count},   {"emulator", ""},   {"available", true},  {"problem", ""},
                           {"dir", ""},        {"extensions", QStringList()}};
    };
    if (!recent.isEmpty())
        list << entry("recent", "Jogados recentemente", "Recentes", "#c9ced8", recent.size());

    for (const SystemDef &s : m_defs) {
        const int count = m_games.value(s.id).size();
        if (!m_showEmpty && count == 0)
            continue;
        QString problem;
        const bool available = !resolveExe(s, &problem).isEmpty();
        QStringList exts = s.extensions;
        if (!s.folderMarkers.isEmpty())
            exts.prepend(QStringLiteral("pasta do jogo"));
        list << QVariantMap{{"id", s.id},
                            {"name", s.name},
                            {"short", s.shortName},
                            {"color", s.color},
                            {"count", count},
                            {"emulator", s.emulator},
                            {"available", available},
                            {"problem", problem},
                            {"dir", tildePath(systemPath(s))},
                            {"extensions", exts}};
    }
    list << entry("apps", "Aplicativos", "Apps", "#8fa0bf", m_apps.size());
    m_systemList = list;
}

QVariantList Library::games(const QString &systemId) const
{
    if (systemId == "recent")
        return recentGames();
    if (systemId == "apps")
        return appList();
    return m_games.value(systemId);
}

QVariantMap Library::gameEntry(const QString &systemId, const QString &path) const
{
    for (const QVariant &g : m_games.value(systemId))
        if (g.toMap().value("path") == path)
            return g.toMap();
    return {};
}

QVariantList Library::recentGames() const
{
    QVariantList out;
    const QStringList entries = QSettings().value("recent").toStringList();
    for (const QString &e : entries) {
        const QStringList f = e.split(kSep);
        if (f.size() < 3 || !QFileInfo::exists(f[1]))
            continue;
        const SystemDef *sys = findSystem(f[0]);
        if (!sys)
            continue;
        // Reaproveita os dados da varredura (capa, etiquetas, nome) quando o jogo ainda está lá.
        QVariantMap game = gameEntry(sys->id, f[1]);
        if (game.isEmpty())
            game = QVariantMap{{"title", f[2]},  {"originalTitle", f[2]}, {"tags", QStringList()},
                               {"path", f[1]},   {"file", QFileInfo(f[1]).fileName()},
                               {"cover", ""},    {"wide", false},         {"icon", ""},
                               {"system", sys->id}, {"systemName", sys->name}};
        game.insert("tags", QStringList{sys->shortName} + game.value("tags").toStringList());
        out << game;
    }
    return out;
}

void Library::pushRecent(const QString &systemId, const QString &path, const QString &title)
{
    QSettings st;
    QStringList entries = st.value("recent").toStringList();
    const QString prefix = systemId + kSep + path + kSep;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const QString &e) { return e.startsWith(prefix); }),
                  entries.end());
    entries.prepend(prefix + title);
    while (entries.size() > kMaxRecent)
        entries.removeLast();
    st.setValue("recent", entries);

    rebuildSystemList();
    ++m_recentRevision;
    emit recentChanged();
    emit systemsChanged();
}

void Library::refresh()
{
    QString error;
    if (!loadConfig(m_configPath, &error))
        emit message(QStringLiteral("Erro em %1: %2").arg(tildePath(m_configPath), error));
    for (const SystemDef &s : m_defs)
        QDir().mkpath(systemPath(s));
    scanApps();
    scan();
    int total = 0;
    for (const auto &g : std::as_const(m_games))
        total += g.size();
    emit message(QStringLiteral("Biblioteca atualizada: %1 jogo(s), %2 aplicativo(s).").arg(total).arg(m_apps.size()));
}

// ------------------------------------------------------------------ execução

bool Library::launch(const QString &systemId, const QString &path, const QString &title,
                     const QString &cover)
{
    const SystemDef *sys = findSystem(systemId);
    if (!sys) {
        emit message(QStringLiteral("Console desconhecido: %1").arg(systemId));
        return false;
    }
    QString problem;
    QStringList prefix;
    const QString exe = resolveExe(*sys, &problem, &prefix);
    if (exe.isEmpty()) {
        emit message(QStringLiteral("%1: %2").arg(sys->emulator.isEmpty() ? sys->name : sys->emulator, problem));
        return false;
    }

    // Opções do emulador: argumentos de linha de comando e/ou chaves gravadas
    // no arquivo de configuração do emulador antes de abrir o jogo.
    QStringList optArgs, notApplied;
    QString firstError;
    for (const QJsonValue &v : sys->settings) {
        const QJsonObject st = v.toObject();
        QString value = effective(*sys, path, st.value("id").toString(), st.value("default").toString());
        if (st.value("id").toString() == "fullscreen" && GameMode::forceFullscreen()) {
            // modo jogo: sempre em tela cheia, seja qual for a escolha do console/jogo
            for (const QJsonValue &o : st.value("options").toArray()) {
                const QString v = o.toObject().value("value").toString();
                if (v == "on" || v == "true") {
                    value = v;
                    break;
                }
            }
        }
        const QJsonObject opt = findOption(st, value);
        const QStringList a = opt.contains("args") ? toStringList(opt.value("args"))
                              : value.isEmpty()    ? QStringList()
                                                   : toStringList(st.value("args"));
        for (QString x : a)
            optArgs << x.replace("{value}", value);
        if (value.isEmpty())
            continue;  // "Padrão do emulador": não mexe no arquivo
        const QJsonObject ini = st.value("ini").toObject(), yaml = st.value("yaml").toObject();
        QString err;
        bool ok = true;
        if (!ini.isEmpty()) {
            const QString file = expandHome(ini.value("file").toString());
            // "create": o emulador completa o resto com os padrões dele (ex.: Snes9x)
            if (ini.value("create").toBool() && !QFileInfo::exists(file)) {
                QDir().mkpath(QFileInfo(file).absolutePath());
                QFile f(file);
                if (f.open(QIODevice::WriteOnly))
                    f.close();
            }
            ok = EmuConfig::setIni(file, ini.value("section").toString(), ini.value("key").toString(), value, &err);
        }
        else if (!yaml.isEmpty())
            ok = EmuConfig::setYaml(expandHome(yaml.value("file").toString()), yaml.value("section").toString(),
                                    yaml.value("key").toString(), value, &err);
        // gravações extras da opção escolhida (ex.: Snes9x esconde a barra de menus em tela cheia)
        for (const QJsonValue &sv : opt.value("set").toArray()) {
            const QJsonObject w = sv.toObject();
            const QString file = expandHome(w.value("file").toString());
            if (w.value("create").toBool() && !QFileInfo::exists(file)) {
                QDir().mkpath(QFileInfo(file).absolutePath());
                QFile f(file);
                if (f.open(QIODevice::WriteOnly))
                    f.close();
            }
            QString e2;
            if (!EmuConfig::setIni(file, w.value("section").toString(), w.value("key").toString(),
                                   w.value("value").toString(), &e2) && ok) {
                ok = false;
                err = e2;
            }
        }
        if (!ok) {
            notApplied << st.value("label").toString();
            if (firstError.isEmpty())
                firstError = err;
        }
    }
    QString extra;
    if (lookup("systems", sys->id, "_extraArgs", &extra))
        optArgs << QProcess::splitCommand(extra);
    if (lookup("games", path, "_extraArgs", &extra))
        optArgs << QProcess::splitCommand(extra);

    const QFileInfo rom(path);
    QStringList args;
    bool hasRom = false, hasOptions = false;
    for (const QString &raw : sys->args) {
        if (raw == "{options}") {
            args << optArgs;
            hasOptions = true;
            continue;
        }
        QString a = raw;
        if (a.contains("{rom}"))
            hasRom = true;
        a.replace("{rom}", rom.absoluteFilePath());
        a.replace("{romdir}", rom.absolutePath());
        a.replace("{title}", title);
        args << a;
    }
    if (!hasOptions)
        args = optArgs + args;
    if (!hasRom)
        args << rom.absoluteFilePath();
    args = prefix + args;

    const QString overlayChoice = effective(*sys, path, "_overlay", QString());
    const bool overlay = sys->overlay && overlayAvailable()
                         && (overlayChoice == "on" || (overlayChoice.isEmpty() && m_overlayEnabled));
    QString program = exe;
    if (overlay) {
        args.prepend(exe);
        program = m_wrapperPath;
    }

    const QVariantMap info{{"title", title},     {"systemName", sys->name}, {"emulator", sys->emulator},
                           {"cover", cover},     {"color", sys->color},     {"mode", "game"},
                           {"systemId", sys->id}, {"rom", rom.absoluteFilePath()}};
    QString error;
    if (!m_runner->start(program, args, QFileInfo(exe).absolutePath(), info, &error)) {
        emit message(error);
        return false;
    }
    if (!notApplied.isEmpty())
        emit message(QStringLiteral("Opções não aplicadas (%1): %2").arg(notApplied.join(", "), firstError));
    pushRecent(systemId, rom.absoluteFilePath(), title);
    return true;
}

bool Library::openEmulator(const QString &systemId)
{
    const SystemDef *sys = findSystem(systemId);
    if (!sys)
        return false;
    QString problem;
    QStringList prefix;
    const QString exe = resolveExe(*sys, &problem, &prefix);
    if (exe.isEmpty()) {
        emit message(QStringLiteral("%1: %2").arg(sys->emulator, problem));
        return false;
    }
    const QVariantMap info{{"title", QStringLiteral("Configurações do %1").arg(sys->emulator)},
                           {"systemName", sys->name}, {"emulator", sys->emulator},
                           {"cover", ""}, {"color", sys->color}, {"mode", "config"}, {"systemId", sys->id}};
    QString error;
    if (!m_runner->start(exe, prefix + sys->openArgs, QFileInfo(exe).absolutePath(), info, &error)) {
        emit message(error);
        return false;
    }
    return true;
}

QString Library::emulatorPath(const QString &systemId, QString *problem) const
{
    QString dummy;
    const SystemDef *sys = findSystem(systemId);
    if (!sys) {
        (problem ? *problem : dummy) = QStringLiteral("console desconhecido");
        return {};
    }
    return resolveExe(*sys, problem ? problem : &dummy);
}

bool Library::startEmulator(const QString &systemId, const QStringList &args, const QVariantMap &info,
                            QString *error)
{
    QString problem;
    QStringList prefix;
    const SystemDef *sys = findSystem(systemId);
    const QString exe = sys ? resolveExe(*sys, &problem, &prefix) : QString();
    if (exe.isEmpty() || !sys) {
        *error = QStringLiteral("%1: %2").arg(sys ? sys->emulator : systemId, problem);
        return false;
    }
    QVariantMap full{{"systemName", sys->name}, {"emulator", sys->emulator}, {"cover", ""},
                     {"color", sys->color}, {"mode", "config"}, {"systemId", sys->id}};
    for (auto it = info.begin(); it != info.end(); ++it)
        full.insert(it.key(), it.value());
    return m_runner->start(exe, prefix + args, QFileInfo(exe).absolutePath(), full, error);
}

bool Library::launchApp(const QString &desktopFile)
{
    for (const AppEntry &a : m_apps) {
        if (a.file != desktopFile)
            continue;
        QString error;
        qint64 pgid = 0;
        if (!launchDesktopApp(a, &error, &pgid)) {
            emit message(error);
            return false;
        }
        emit appStarted(pgid, a.name,
                        a.icon.isEmpty() ? QString() : "image://appicon/" + QString::fromUtf8(QUrl::toPercentEncoding(a.icon)));
        emit message(QStringLiteral("Abrindo %1… (toque no Guia para voltar ao launcher)").arg(a.name));
        return true;
    }
    emit message(QStringLiteral("Aplicativo não encontrado."));
    return false;
}

// ------------------------------------------------------------------ painéis de opções

QVariantList Library::systemOptions(const QString &systemId) const
{
    QVariantList rows;
    const SystemDef *s = findSystem(systemId);
    if (!s)
        return rows;

    rows << row("header", "h_emu", "Emulador");
    QString problem;
    QStringList prefix;
    const QString exe = resolveExe(*s, &problem, &prefix);
    const QString exeDetail = exe.isEmpty()      ? QStringLiteral("⚠ ") + problem
                              : !prefix.isEmpty() ? QStringLiteral("Usando o Flatpak %1").arg(prefix.last())
                                                  : QStringLiteral("Usando ") + tildePath(exe);
    const QStringList cands = exeCandidates(*s);
    QString chosen;
    lookup("systems", s->id, "_exe", &chosen);
    if (cands.size() > 1 || !chosen.isEmpty()) {
        QVariantList opts{QVariantMap{{"label", "Automático (versão mais recente)"}, {"value", ""}}};
        for (const QString &c : cands)
            opts << QVariantMap{{"label", c}, {"value", c}};
        QVariantMap r = row("choice", "_exe", "Versão do " + s->emulator, exeDetail);
        r.insert("options", opts);
        r.insert("value", chosen);
        rows << r;
    } else {
        rows << row("info", "_exeinfo", s->emulator, exeDetail);
    }
    rows << row("action", "_open", QStringLiteral("Abrir o %1").arg(s->emulator),
                "Configuração completa: controles, BIOS/firmware, gráficos e áudio");

    if (!s->settings.isEmpty())
        rows << row("header", "h_opts", "Imagem e desempenho");
    for (const QJsonValue &v : s->settings) {
        const QJsonObject st = v.toObject();
        const QString id = st.value("id").toString();
        QString detail = st.value("description").toString();
        const QJsonObject target = st.contains("ini") ? st.value("ini").toObject() : st.value("yaml").toObject();
        if (!target.isEmpty() && !QFileInfo::exists(expandHome(target.value("file").toString())))
            detail = QStringLiteral("⚠ Abra o %1 uma vez para criar a configuração").arg(s->emulator);
        QVariantMap r = row("choice", id, st.value("label").toString(), detail);
        r.insert("options", optionsOf(st));
        r.insert("value", effective(*s, QString(), id, st.value("default").toString()));
        rows << r;
    }

    rows << row("header", "h_launcher", "Launcher");
    if (overlayAvailable() && s->overlay) {
        QVariantMap r = row("choice", "_overlay", "Overlay de desempenho (LinOSD)",
                            "\"Seguir o menu\" usa a opção geral do menu Start");
        r.insert("options", QVariantList{QVariantMap{{"label", "Seguir o menu"}, {"value", ""}},
                                         QVariantMap{{"label", "Ligado"}, {"value", "on"}},
                                         QVariantMap{{"label", "Desligado"}, {"value", "off"}}});
        r.insert("value", effective(*s, QString(), "_overlay", QString()));
        rows << r;
    }
    QVariantMap extra = row("text", "_extraArgs", "Argumentos extras",
                            "Adicionados ao comando de todos os jogos deste console");
    extra.insert("value", effective(*s, QString(), "_extraArgs", QString()));
    rows << extra;

    rows << row("header", "h_lib", "Biblioteca");
    rows << row("action", "_import", "Importar jogos", "Para " + tildePath(systemPath(*s)));
    rows << row("action", "_refresh", "Atualizar biblioteca", "Procura jogos novos nas pastas");
    QVariantMap reset = row("action", "_reset", "Restaurar os padrões deste console",
                            "Apaga as escolhas acima (os jogos não são alterados)");
    reset.insert("confirm", true);
    rows << reset;
    return rows;
}

QVariantList Library::gameOptions(const QString &systemId, const QString &path) const
{
    QVariantList rows;
    const SystemDef *s = findSystem(systemId);
    if (!s)
        return rows;
    const QVariantMap g = gameEntry(systemId, path);
    const QString original = g.value("originalTitle").toString();

    rows << row("header", "h_game", "Jogo");
    QVariantMap title = row("text", "_title", "Nome exibido",
                            original.isEmpty() ? QString() : QStringLiteral("Nome original: ") + original);
    title.insert("value", g.value("title").toString());
    rows << title;

    if (!s->settings.isEmpty())
        rows << row("header", "h_opts", QStringLiteral("Opções só deste jogo"));
    for (const QJsonValue &v : s->settings) {
        const QJsonObject st = v.toObject();
        const QString id = st.value("id").toString();
        const QString consoleValue = effective(*s, QString(), id, st.value("default").toString());
        QVariantList opts{QVariantMap{{"label", QStringLiteral("Igual ao console (%1)").arg(optionLabel(st, consoleValue))},
                                      {"value", kInherit}}};
        opts << optionsOf(st);
        QString value;
        if (!lookup("games", path, id, &value))
            value = kInherit;
        QVariantMap r = row("choice", id, st.value("label").toString(), st.value("description").toString());
        r.insert("options", opts);
        r.insert("value", value);
        rows << r;
    }
    if (overlayAvailable() && s->overlay) {
        QString value;
        if (!lookup("games", path, "_overlay", &value))
            value = kInherit;
        QVariantMap r = row("choice", "_overlay", "Overlay de desempenho (LinOSD)");
        r.insert("options", QVariantList{QVariantMap{{"label", "Igual ao console"}, {"value", kInherit}},
                                         QVariantMap{{"label", "Ligado"}, {"value", "on"}},
                                         QVariantMap{{"label", "Desligado"}, {"value", "off"}}});
        r.insert("value", value);
        rows << r;
    }
    QString extraValue;
    lookup("games", path, "_extraArgs", &extraValue);
    QVariantMap extra = row("text", "_extraArgs", "Argumentos extras", "Somados aos argumentos do console");
    extra.insert("value", extraValue);
    rows << extra;

    QVariantMap reset = row("action", "_reset", "Restaurar os padrões deste jogo", "Volta a seguir o console");
    reset.insert("confirm", true);
    rows << reset;
    rows << row("info", "_path", "Arquivo", tildePath(path));
    return rows;
}

void Library::setSystemOption(const QString &systemId, const QString &key, const QString &value)
{
    if (!findSystem(systemId))
        return;
    store("systems", systemId, key, &value);
    if (key == "_exe") {
        rebuildSystemList();
        emit systemsChanged();
    }
}

void Library::setGameOption(const QString &systemId, const QString &path, const QString &key,
                            const QString &value)
{
    if (!findSystem(systemId) || path.isEmpty())
        return;
    QString v = value;
    if (key == "_title") {
        v = v.simplified();
        const QString original = gameEntry(systemId, path).value("originalTitle").toString();
        if (v.isEmpty() || v == original)
            v = kInherit;
    }
    if (key == "_extraArgs" && v.trimmed().isEmpty())
        v = kInherit;
    store("games", path, key, v == kInherit ? nullptr : &v);
    if (key == "_title")
        rescanSystem(systemId);
}

void Library::resetSystemOptions(const QString &systemId)
{
    QJsonObject g = m_store.value("systems").toObject();
    g.remove(systemId);
    m_store.insert("systems", g);
    saveStore();
    rebuildSystemList();
    emit systemsChanged();
}

void Library::resetGameOptions(const QString &systemId, const QString &path)
{
    QJsonObject g = m_store.value("games").toObject();
    g.remove(path);
    m_store.insert("games", g);
    saveStore();
    rescanSystem(systemId);
}

void Library::setOverlayEnabled(bool on)
{
    if (m_overlayEnabled == on)
        return;
    m_overlayEnabled = on;
    QSettings().setValue("overlay", on);
    emit overlayEnabledChanged();
}

void Library::setLastSystem(const QString &id)
{
    if (m_lastSystem == id)
        return;
    m_lastSystem = id;
    QSettings().setValue("lastSystem", id);
    emit lastSystemChanged();
}

QString Library::romRootDisplay() const { return tildePath(m_romRoot); }
QString Library::configPathDisplay() const { return tildePath(m_configPath); }
QString Library::version() const { return QStringLiteral(APP_VERSION); }

void Library::powerOff() { QProcess::startDetached("systemctl", {"poweroff"}); }
void Library::reboot() { QProcess::startDetached("systemctl", {"reboot"}); }
