#pragma once
#include <QString>

// Edição pontual dos arquivos de configuração dos emuladores, preservando o
// resto do arquivo. Nunca cria o arquivo: se ele não existir (emulador nunca
// aberto), retorna false com o motivo em *error.
namespace EmuConfig {
// INI ([Seção] / chave = valor): DuckStation, PCSX2, Dolphin…
bool setIni(const QString &file, const QString &section, const QString &key,
            const QString &value, QString *error);
// YAML de dois níveis (Seção: / "  Chave: valor"): RPCS3
bool setYaml(const QString &file, const QString &section, const QString &key,
             const QString &value, QString *error);
// TOML ([tabela] / chave = 'texto'): xemu. Com create=true cria o arquivo se faltar.
bool setTomlString(const QString &file, const QString &table, const QString &key,
                   const QString &value, bool create, QString *error);

// Leitura (valor vazio se o arquivo, a seção ou a chave não existirem)
QString getIni(const QString &file, const QString &section, const QString &key);
QString getTomlString(const QString &file, const QString &table, const QString &key);
}
