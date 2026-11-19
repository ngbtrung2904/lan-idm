#pragma once

#include "Types.h"

#include <QByteArray>
#include <QString>

namespace dc {

class ConfigStore {
public:
    static QString resolveConfigPath(const QString &explicitPath);
    static QString expandTilde(const QString &path);

    static bool load(const QString &filePath, AppConfig *outConfig, QString *error);
    static bool loadDefault(AppConfig *outConfig, QString *error, const QString &explicitPath);
    static bool parseJson(const QByteArray &jsonData, AppConfig *outConfig, QString *error);
};

} // namespace dc
