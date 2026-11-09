#include "emuconfig.h"

#include <QFile>
#include <QSaveFile>
#include <QStringList>

namespace {

bool readLines(const QString &file, QStringList *lines, bool *crlf, QString *error)
{
    QFile f(file);
    if (!f.exists()) {
        *error = QStringLiteral("o arquivo %1 ainda não existe (abra o emulador uma vez)").arg(file);
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return false;
    }
    const QByteArray data = f.readAll();
    *crlf = data.contains("\r\n");
    QString text = QString::fromUtf8(data);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    *lines = text.split('\n');
    if (!lines->isEmpty() && lines->last().isEmpty())
        lines->removeLast();
    return true;
}

bool writeLines(const QString &file, const QStringList &lines, bool crlf, QString *error)
{
    QSaveFile f(file);  // grava em arquivo temporário e troca no fim (nunca deixa meio-escrito)
    if (!f.open(QIODevice::WriteOnly)) {
        *error = f.errorString();
        return false;
    }
    QString text = lines.join('\n') + '\n';
    if (crlf)
        text.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    f.write(text.toUtf8());
    if (!f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}

}  // namespace

bool EmuConfig::setIni(const QString &file, const QString &section, const QString &key,
                       const QString &value, QString *error)
{
    QStringList lines;
    bool crlf = false;
    if (!readLines(file, &lines, &crlf, error))
        return false;

    const QString header = '[' + section + ']';
    int secStart = -1, secEnd = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t.startsWith('[') && t.endsWith(']')) {
            if (secStart >= 0) {
                secEnd = i;
                break;
            }
            if (t.compare(header, Qt::CaseInsensitive) == 0)
                secStart = i;
        }
    }

    if (secStart < 0) {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
            lines << QString();
        lines << header << key + QStringLiteral(" = ") + value;
        return writeLines(file, lines, crlf, error);
    }

    int lastContent = secStart;
    for (int i = secStart + 1; i < secEnd; ++i) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty() || t.startsWith(';') || t.startsWith('#'))
            continue;
        lastContent = i;
        const int eq = t.indexOf('=');
        if (eq > 0 && t.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0) {
            const QString &line = lines[i];
            const int leq = line.indexOf('=');
            if (line.mid(leq + 1).trimmed() == value)
                return true;  // já está com o valor certo: não reescreve
            const bool spaced = leq > 0 && line[leq - 1] == ' ';
            lines[i] = line.left(leq).trimmed() + (spaced ? QStringLiteral(" = ") : QStringLiteral("="))
                       + value;
            return writeLines(file, lines, crlf, error);
        }
    }
    lines.insert(lastContent + 1, key + QStringLiteral(" = ") + value);
    return writeLines(file, lines, crlf, error);
}

bool EmuConfig::setYaml(const QString &file, const QString &section, const QString &key,
                        const QString &value, QString *error)
{
    QStringList lines;
    bool crlf = false;
    if (!readLines(file, &lines, &crlf, error))
        return false;

    int sec = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (!lines[i].startsWith(' ') && lines[i].trimmed() == section + ':') {
            sec = i;
            break;
        }
    }
    if (sec < 0) {
        lines << section + ':' << QStringLiteral("  ") + key + QStringLiteral(": ") + value;
        return writeLines(file, lines, crlf, error);
    }

    int end = lines.size();
    for (int i = sec + 1; i < lines.size(); ++i) {
        const QString &l = lines[i];
        if (!l.trimmed().isEmpty() && !l.startsWith(' ')) {
            end = i;
            break;
        }
    }

    int indent = -1, lastAny = sec;
    for (int i = sec + 1; i < end; ++i) {
        const QString &l = lines[i];
        if (l.trimmed().isEmpty())
            continue;
        lastAny = i;
        int ind = 0;
        while (ind < l.size() && l[ind] == ' ')
            ++ind;
        if (indent < 0)
            indent = ind;
        if (ind != indent)
            continue;  // chave de nível mais profundo
        if (l.mid(ind).startsWith(key + ':')) {
            if (l.mid(ind + key.size() + 1).trimmed() == value)
                return true;
            lines[i] = QString(ind, ' ') + key + QStringLiteral(": ") + value;
            return writeLines(file, lines, crlf, error);
        }
    }
    lines.insert(lastAny + 1, QString(indent < 0 ? 2 : indent, ' ') + key + QStringLiteral(": ") + value);
    return writeLines(file, lines, crlf, error);
}

namespace {

// [Seção] → índice do cabeçalho e fim da seção (-1 se não existir)
void findSection(const QStringList &lines, const QString &header, int *start, int *end)
{
    *start = -1;
    *end = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t.startsWith('[') && t.endsWith(']')) {
            if (*start >= 0) {
                *end = i;
                return;
            }
            if (t.compare(header, Qt::CaseInsensitive) == 0)
                *start = i;
        }
    }
}

QString tomlQuote(const QString &v)
{
    if (!v.contains('\'') && !v.contains('\n'))
        return '\'' + v + '\'';
    QString e = v;
    e.replace('\\', QStringLiteral("\\\\")).replace('"', QStringLiteral("\\\"")).replace('\n', QStringLiteral("\\n"));
    return '"' + e + '"';
}

QString tomlUnquote(QString v)
{
    v = v.trimmed();
    // comentário no fim da linha (fora de aspas)
    if (v.startsWith('\'')) {
        const int end = v.indexOf('\'', 1);
        return end > 0 ? v.mid(1, end - 1) : v.mid(1);
    }
    if (v.startsWith('"')) {
        QString out;
        for (int i = 1; i < v.size(); ++i) {
            if (v[i] == '\\' && i + 1 < v.size()) {
                const QChar c = v[++i];
                out += c == 'n' ? QChar('\n') : c == 't' ? QChar('\t') : c;
            } else if (v[i] == '"') {
                break;
            } else {
                out += v[i];
            }
        }
        return out;
    }
    const int hash = v.indexOf('#');
    return (hash >= 0 ? v.left(hash) : v).trimmed();
}

}  // namespace

bool EmuConfig::setTomlString(const QString &file, const QString &table, const QString &key,
                              const QString &value, bool create, QString *error)
{
    QStringList lines;
    bool crlf = false;
    if (!QFile::exists(file)) {
        if (!create) {
            *error = QStringLiteral("o arquivo %1 ainda não existe (abra o emulador uma vez)").arg(file);
            return false;
        }
    } else if (!readLines(file, &lines, &crlf, error)) {
        return false;
    }
    const QString line = key + QStringLiteral(" = ") + tomlQuote(value);
    int start, end;
    findSection(lines, '[' + table + ']', &start, &end);
    if (start < 0) {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
            lines << QString();
        lines << '[' + table + ']' << line;
        return writeLines(file, lines, crlf, error);
    }
    int last = start;
    for (int i = start + 1; i < end; ++i) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty() || t.startsWith('#'))
            continue;
        last = i;
        const int eq = t.indexOf('=');
        if (eq > 0 && t.left(eq).trimmed() == key) {
            if (tomlUnquote(t.mid(eq + 1)) == value)
                return true;
            lines[i] = line;
            return writeLines(file, lines, crlf, error);
        }
    }
    lines.insert(last + 1, line);
    return writeLines(file, lines, crlf, error);
}

QString EmuConfig::getIni(const QString &file, const QString &section, const QString &key)
{
    QStringList lines;
    bool crlf;
    QString err;
    if (!QFile::exists(file) || !readLines(file, &lines, &crlf, &err))
        return {};
    int start, end;
    findSection(lines, '[' + section + ']', &start, &end);
    if (start < 0)
        return {};
    for (int i = start + 1; i < end; ++i) {
        const QString t = lines[i].trimmed();
        const int eq = t.indexOf('=');
        if (eq > 0 && !t.startsWith(';') && !t.startsWith('#')
            && t.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0)
            return t.mid(eq + 1).trimmed();
    }
    return {};
}

QString EmuConfig::getTomlString(const QString &file, const QString &table, const QString &key)
{
    QStringList lines;
    bool crlf;
    QString err;
    if (!QFile::exists(file) || !readLines(file, &lines, &crlf, &err))
        return {};
    int start, end;
    findSection(lines, '[' + table + ']', &start, &end);
    if (start < 0)
        return {};
    for (int i = start + 1; i < end; ++i) {
        const QString t = lines[i].trimmed();
        const int eq = t.indexOf('=');
        if (eq > 0 && !t.startsWith('#') && t.left(eq).trimmed() == key)
            return tomlUnquote(t.mid(eq + 1));
    }
    return {};
}
