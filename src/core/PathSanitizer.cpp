#include "PathSanitizer.h"

#include <QSaveFile>

namespace dc {

bool PathSanitizer::isSafeRelativePath(const QString &relPath) {
    if (relPath.isEmpty()) {
        return false;
    }
    if (relPath.startsWith(QLatin1Char('/')) || relPath.startsWith(QLatin1Char('\\'))) {
        return false;
    }
    if (relPath == QLatin1String(".")) {
        return false;
    }

    for (const QChar ch : relPath) {
        const ushort code = ch.unicode();
        if (code == 0 || code < 0x20 || code == 0x7f) {
            return false;
        }
    }

    const QStringList parts = relPath.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part == QLatin1String("..")) {
            return false;
        }
    }

    return true;
}

bool PathSanitizer::writeFilesFromList(const QString &filePath,
                                       const QVector<FileEntry> &entries,
                                       QString *error) {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Cannot open files-from list '%1': %2")
                         .arg(filePath, file.errorString());
        }
        return false;
    }

    for (const FileEntry &entry : entries) {
        if (!isSafeRelativePath(entry.relPath)) {
            file.cancelWriting();
            if (error) {
                *error = QStringLiteral("Rejected unsafe relative path: %1").arg(entry.relPath);
            }
            return false;
        }
        const QByteArray utf8 = entry.relPath.toUtf8();
        if (file.write(utf8) != utf8.size() || file.write("\0", 1) != 1) {
            file.cancelWriting();
            if (error) {
                *error = QStringLiteral("Failed writing files-from list '%1': %2")
                             .arg(filePath, file.errorString());
            }
            return false;
        }
    }

    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Failed committing files-from list '%1': %2")
                         .arg(filePath, file.errorString());
        }
        return false;
    }

    return true;
}

} // namespace dc
