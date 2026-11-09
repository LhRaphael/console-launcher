#include "sfo.h"

#include <QFile>
#include <QtEndian>

// Formato PSF/SFO (little-endian):
//   0x00 magic "\0PSF"   0x04 versão   0x08 início da tabela de chaves
//   0x0C início dos dados  0x10 número de entradas
//   índice a partir de 0x14, 16 bytes por entrada:
//     u16 key_offset, u16 formato, u32 tamanho usado, u32 tamanho máximo, u32 data_offset
QString readSfoTitle(const QString &path)
{
    if (path.isEmpty())
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024)
        return {};
    const QByteArray d = f.readAll();
    if (d.size() < 0x14 || !d.startsWith(QByteArray("\0PSF", 4)))
        return {};

    auto u16 = [&](int off) { return qFromLittleEndian<quint16>(d.constData() + off); };
    auto u32 = [&](int off) { return qFromLittleEndian<quint32>(d.constData() + off); };

    const quint32 keyTable = u32(0x08);
    const quint32 dataTable = u32(0x0C);
    const quint32 count = u32(0x10);
    if (count > 512)
        return {};

    for (quint32 i = 0; i < count; ++i) {
        const int e = 0x14 + int(i) * 16;
        if (e + 16 > d.size())
            break;
        const quint32 keyPos = keyTable + u16(e);
        const quint32 len = u32(e + 4);
        const quint32 dataPos = dataTable + u32(e + 12);
        if (keyPos >= quint32(d.size()))
            continue;
        const QByteArray key(d.constData() + keyPos);  // termina em '\0'
        if (key != "TITLE")
            continue;
        if (dataPos + len > quint32(d.size()))
            return {};
        QByteArray value(d.constData() + dataPos, int(len));
        const int nul = value.indexOf('\0');
        if (nul >= 0)
            value.truncate(nul);
        return QString::fromUtf8(value).simplified();
    }
    return {};
}
