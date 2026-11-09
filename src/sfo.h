#pragma once
#include <QString>

// Lê o campo TITLE de um PARAM.SFO (PS3) / param.sfo (PS4).
// Retorna string vazia se o arquivo não existir ou não for um SFO válido.
QString readSfoTitle(const QString &path);
