#pragma once

#include "Types.h"

#include <QString>
#include <QVector>

namespace dc {

class PathSanitizer {
public:
    static bool isSafeRelativePath(const QString &relPath);
    static bool writeFilesFromList(const QString &filePath,
                                   const QVector<FileEntry> &entries,
                                   QString *error);
};

} // namespace dc
