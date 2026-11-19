#include "Manifest.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace dc {

namespace {

QJsonValue formatIsoDateTime(const QDateTime &dt) {
    if (!dt.isValid()) {
        return QJsonValue(QJsonValue::Null);
    }
    return dt.toString(Qt::ISODate);
}

void setError(QString *error, const QString &message) {
    if (error) {
        *error = message;
    }
}

} // namespace

QString Manifest::jobStateToString(JobState state) {
    switch (state) {
    case JobState::Queued:             return QStringLiteral("queued");
    case JobState::CopyingLogs:        return QStringLiteral("copying_logs");
    case JobState::CopyingScreenshots: return QStringLiteral("copying_screenshots");
    case JobState::Verifying:          return QStringLiteral("verifying");
    case JobState::Done:               return QStringLiteral("done");
    case JobState::DoneWithWarnings:   return QStringLiteral("done_with_warnings");
    case JobState::Failed:             return QStringLiteral("failed");
    case JobState::Cancelled:          return QStringLiteral("cancelled");
    case JobState::Skipped:            return QStringLiteral("skipped");
    }
    return QStringLiteral("unknown");
}

QJsonObject Manifest::toJson(const ManifestData &data) {
    QJsonObject root;
    root.insert(QStringLiteral("operatorId"), data.operatorId);
    root.insert(QStringLiteral("appVersion"), data.appVersion);
    root.insert(QStringLiteral("windowStart"), formatIsoDateTime(data.windowStart));
    root.insert(QStringLiteral("windowEnd"), formatIsoDateTime(data.windowEnd));
    root.insert(QStringLiteral("startedAt"), formatIsoDateTime(data.startedAt));
    root.insert(QStringLiteral("finishedAt"), formatIsoDateTime(data.finishedAt));
    root.insert(QStringLiteral("status"), data.status);

    QJsonArray pcsArray;
    for (const PcTransferResult &pc : data.pcs) {
        QJsonObject pcObj;
        pcObj.insert(QStringLiteral("id"), pc.pcId);
        pcObj.insert(QStringLiteral("host"), pc.host);
        pcObj.insert(QStringLiteral("status"), jobStateToString(pc.state));
        pcObj.insert(QStringLiteral("logsFilesPlanned"), pc.logsFilesPlanned);
        pcObj.insert(QStringLiteral("logsFilesCopied"), pc.logsFilesCopied);
        pcObj.insert(QStringLiteral("logsBytesCopied"), static_cast<double>(pc.logsBytesCopied));
        pcObj.insert(QStringLiteral("shotsFilesPlanned"), pc.shotsFilesPlanned);
        pcObj.insert(QStringLiteral("shotsFilesCopied"), pc.shotsFilesCopied);
        pcObj.insert(QStringLiteral("shotsBytesCopied"), static_cast<double>(pc.shotsBytesCopied));
        pcObj.insert(QStringLiteral("logsRsyncExitCode"), pc.logsRsyncExitCode);
        pcObj.insert(QStringLiteral("shotsRsyncExitCode"), pc.shotsRsyncExitCode);

        QJsonArray errArray;
        for (const QString &err : pc.errors) {
            errArray.append(err);
        }
        pcObj.insert(QStringLiteral("errors"), errArray);
        pcsArray.append(pcObj);
    }
    root.insert(QStringLiteral("pcs"), pcsArray);
    return root;
}

bool Manifest::writeAtomic(const QString &filePath,
                           const ManifestData &data,
                           QString *error) {
    if (filePath.isEmpty()) {
        setError(error, QStringLiteral("Manifest file path is empty."));
        return false;
    }

    const QFileInfo fi(filePath);
    const QString parentPath = fi.absolutePath();
    if (!parentPath.isEmpty() && !QDir().mkpath(parentPath)) {
        setError(error, QStringLiteral("Failed to create manifest directory: %1").arg(parentPath));
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Failed to open manifest '%1': %2")
                            .arg(filePath, file.errorString()));
        return false;
    }

    const QByteArray jsonBytes = QJsonDocument(toJson(data)).toJson(QJsonDocument::Indented);
    if (file.write(jsonBytes) != jsonBytes.size()) {
        file.cancelWriting();
        setError(error, QStringLiteral("Failed to write manifest '%1': %2")
                            .arg(filePath, file.errorString()));
        return false;
    }

    if (!file.commit()) {
        setError(error, QStringLiteral("Failed to commit manifest '%1': %2")
                            .arg(filePath, file.errorString()));
        return false;
    }

    return true;
}

} // namespace dc
