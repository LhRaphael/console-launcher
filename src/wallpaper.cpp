#include "wallpaper.h"
#include "library.h"

#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

const QStringList kImageExt{"jpg", "jpeg", "png", "webp", "bmp", "gif", "tif", "tiff", "avif", "heic"};
const QStringList kVideoExt{"mp4", "webm", "mkv", "mov", "avi", "m4v", "wmv", "ogv", "mpg", "mpeg"};

QVariantMap row(const QString &type, const QString &key, const QString &label, const QString &detail = {})
{
    return {{"type", type}, {"key", key}, {"label", label}, {"detail", detail}};
}

QVariantMap choice(const QString &key, const QString &label, const QString &value,
                   const QList<QPair<QString, QString>> &opts, const QString &detail = {})
{
    QVariantList list;
    for (const auto &o : opts)
        list << QVariantMap{{"label", o.first}, {"value", o.second}};
    QVariantMap r = row("choice", key, label, detail);
    r.insert("options", list);
    r.insert("value", value);
    return r;
}

QString tilde(const QString &p)
{
    const QString home = QDir::homePath();
    return p.startsWith(home) ? '~' + p.mid(home.size()) : p;
}

}  // namespace

Wallpapers::Wallpapers(Library *library, QObject *parent) : QObject(parent), m_lib(library)
{
    // vídeo precisa do módulo QML QtMultimedia (pacote qml6-module-qtmultimedia)
    const QString qml = QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
    m_video = QFileInfo::exists(qml + "/QtMultimedia/qmldir");
}

void Wallpapers::bump()
{
    ++m_revision;
    emit changed();
}

QVariant Wallpapers::value(const QString &scope, const QString &key, const QVariant &def) const
{
    return QSettings().value(QStringLiteral("wallpaper/%1/%2").arg(scope, key), def);
}

void Wallpapers::put(const QString &scope, const QString &key, const QVariant &v)
{
    QSettings().setValue(QStringLiteral("wallpaper/%1/%2").arg(scope, key), v);
}

QString Wallpapers::kindOf(const QString &scope) const
{
    return value(scope, "kind", scope == "global" ? "default" : "inherit").toString();
}

bool Wallpapers::isVideo(const QString &path) const { return kVideoExt.contains(QFileInfo(path).suffix().toLower()); }

bool Wallpapers::isMedia(const QString &path) const
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return kImageExt.contains(ext) || (m_video && kVideoExt.contains(ext));
}

QStringList Wallpapers::mediaIn(const QString &folder) const
{
    QStringList out;
    const QFileInfoList list = QDir(folder).entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : list)
        if (!fi.fileName().startsWith('.') && isMedia(fi.absoluteFilePath()))
            out << fi.absoluteFilePath();
    return out;
}

QString Wallpapers::scopeName(const QString &scope) const
{
    if (scope == "global" || scope.isEmpty())
        return QStringLiteral("Geral");
    if (scope == "recent")
        return QStringLiteral("Recentes");
    if (scope == "apps")
        return QStringLiteral("Aplicativos");
    SystemDef d;
    return m_lib->systemDef(scope, &d) ? d.name : scope;
}

QVariantMap Wallpapers::effective(const QString &systemId) const
{
    QString scope = "global";
    if (!systemId.isEmpty() && kindOf(systemId) != "inherit")
        scope = systemId;
    const QString kind = kindOf(scope);
    const QString path = value(scope, "path", "").toString();
    QStringList files;
    if (kind == "file" && QFileInfo(path).isFile() && isMedia(path))
        files << path;
    else if (kind == "folder")
        files = mediaIn(path);
    return {{"scope", scope},
            {"kind", files.isEmpty() ? QStringLiteral("default") : kind},
            {"path", path},
            {"files", files},
            {"interval", value(scope, "interval", 10).toInt()},
            {"order", value(scope, "order", "seq").toString()},
            {"dim", value(scope, "dim", 35).toInt()},
            {"fit", value(scope, "fit", "cover").toString()},
            {"key", scope + '|' + kind + '|' + path}};
}

QVariantList Wallpapers::rows(const QString &consoleId) const
{
    QVariantList rows;
    rows << row("header", "wp_h", "Papel de parede");

    // qual papel de parede está sendo editado: o geral ou o da aba atual
    QString scope = m_editScope;
    if (scope != "global" && scope != consoleId)
        scope = "global";
    if (!consoleId.isEmpty())
        rows << choice("wp:scope", "Editando", scope,
                       {{QStringLiteral("Geral (todas as abas)"), "global"},
                        {QStringLiteral("Só a aba %1").arg(scopeName(consoleId)), consoleId}},
                       scope == "global" ? QStringLiteral("Vale para as abas que não têm um próprio")
                                         : QStringLiteral("Só quando a aba %1 estiver aberta").arg(scopeName(consoleId)));

    const QString kind = kindOf(scope);
    QList<QPair<QString, QString>> kinds;
    if (scope != "global")
        kinds << qMakePair(QStringLiteral("Usar o geral"), QStringLiteral("inherit"));
    kinds << qMakePair(QStringLiteral("Padrão do launcher"), QStringLiteral("default"))
          << qMakePair(m_video ? QStringLiteral("Uma imagem ou vídeo") : QStringLiteral("Uma imagem"), QStringLiteral("file"))
          << qMakePair(QStringLiteral("Uma pasta (troca sozinho)"), QStringLiteral("folder"));
    rows << choice("wp:kind", "Fundo", kind, kinds);

    const QString path = value(scope, "path", "").toString();
    if (kind == "file") {
        const bool ok = QFileInfo(path).isFile();
        rows << row("action", "wp:pickFile", m_video ? "Escolher imagem ou vídeo…" : "Escolher imagem…",
                    path.isEmpty() ? QStringLiteral("Nenhum escolhido ainda")
                    : ok          ? tilde(path)
                                  : QStringLiteral("⚠ Não encontrado: ") + tilde(path));
    } else if (kind == "folder") {
        const int n = mediaIn(path).size();
        rows << row("action", "wp:pickFolder", "Escolher pasta…",
                    path.isEmpty() ? QStringLiteral("Nenhuma escolhida ainda")
                    : n == 0      ? QStringLiteral("⚠ Nenhuma imagem ou vídeo em ") + tilde(path)
                                  : QStringLiteral("%1 · %2 arquivo(s)").arg(tilde(path)).arg(n));
        rows << choice("wp:interval", "Trocar a cada", value(scope, "interval", 10).toString(),
                       {{"1 minuto", "1"}, {"5 minutos", "5"}, {"10 minutos", "10"}, {"15 minutos", "15"},
                        {"30 minutos", "30"}, {"1 hora", "60"}, {"Só quando eu pedir", "0"}});
        rows << choice("wp:order", "Ordem", value(scope, "order", "seq").toString(),
                       {{"Em sequência", "seq"}, {"Aleatória", "random"}});
        rows << row("action", "wp:next", "Trocar agora", "Passa para o próximo da pasta");
    }
    if (kind == "file" || kind == "folder") {
        QVariantMap dim = row("slider", "wp:dim", "Escurecer o fundo", "Deixa os textos legíveis sobre fundos claros");
        dim.insert("value", value(scope, "dim", 35).toInt());
        rows << dim;
        rows << choice("wp:fit", "Enquadramento", value(scope, "fit", "cover").toString(),
                       {{"Preencher a tela (corta as bordas)", "cover"}, {"Mostrar inteiro", "contain"}});
    }
    if (!m_video)
        rows << row("info", "wp_novideo", QStringLiteral("Vídeos como fundo"),
                    QStringLiteral("⚠ Instale o pacote qml6-module-qtmultimedia para usar vídeos"));
    else
        rows << row("info", "wp_tip", QStringLiteral("Dica"),
                    QStringLiteral("No gerenciador de arquivos, abra o menu de uma imagem ou vídeo para usar como fundo"));
    return rows;
}

bool Wallpapers::themes() const { return QSettings().value("theme/enabled", true).toBool(); }
bool Wallpapers::themeMotion() const { return QSettings().value("theme/motion", true).toBool(); }

QVariantList Wallpapers::themeRows() const
{
    QVariantList rows;
    rows << row("header", "th_h", "Temas dos consoles");
    QVariantMap t = row("toggle", "th:enabled", "Tema próprio em cada aba",
                        "Letras, cores e fundo que lembram cada console");
    t.insert("value", themes());
    QVariantMap m = row("toggle", "th:motion", "Animar o fundo do tema", "Desligue para gastar menos processador");
    m.insert("value", themeMotion());
    rows << t << m;
    return rows;
}

void Wallpapers::set(const QString &consoleId, const QString &key, const QString &v)
{
    if (key == "th:enabled" || key == "th:motion") {
        QSettings().setValue(key == "th:enabled" ? "theme/enabled" : "theme/motion", v == "on" || v == "true");
        bump();
        return;
    }
    QString scope = m_editScope;
    if (scope != "global" && scope != consoleId)
        scope = "global";

    if (key == "wp:scope") {
        m_editScope = v;
        bump();
        return;
    }
    if (key == "wp:kind") {
        put(scope, "kind", v);
        // escolheu imagem/pasta mas ainda não há caminho: abre o seletor
        const QString path = value(scope, "path", "").toString();
        if (v == "file" && !QFileInfo(path).isFile())
            emit pickRequested(scope, "file", QFileInfo(path).absolutePath());
        else if (v == "folder" && !QFileInfo(path).isDir())
            emit pickRequested(scope, "folder", path);
        bump();
        return;
    }
    if (key == "wp:pickFile" || key == "wp:pickFolder") {
        const QString path = value(scope, "path", "").toString();
        const QString start = QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath();
        emit pickRequested(scope, key == "wp:pickFile" ? "file" : "folder", QFileInfo(start).isDir() ? start : QString());
        return;
    }
    if (key == "wp:next") {
        emit nextRequested();
        return;
    }
    if (key == "wp:interval" || key == "wp:order" || key == "wp:dim" || key == "wp:fit") {
        const QString k = key.mid(3);
        put(scope, k, (k == "interval" || k == "dim") ? QVariant(v.toInt()) : QVariant(v));
        bump();
    }
}

void Wallpapers::choose(const QString &scope, const QString &kind, const QString &path)
{
    const QString sc = scope.isEmpty() ? QStringLiteral("global") : scope;
    if (kind == "file" && !isMedia(path)) {
        emit message(QStringLiteral("Esse arquivo não serve como papel de parede."));
        return;
    }
    if (kind == "folder" && mediaIn(path).isEmpty()) {
        emit message(QStringLiteral("Não há imagens%1 nessa pasta.").arg(m_video ? " nem vídeos" : ""));
        return;
    }
    put(sc, "kind", kind);
    put(sc, "path", path);
    m_editScope = sc;
    emit message(QStringLiteral("Papel de parede %1 trocado.").arg(sc == "global" ? QStringLiteral("geral")
                                                                                   : QStringLiteral("de ") + scopeName(sc)));
    bump();
}
