#pragma once

#include "Types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace dc {

struct ManifestData {
    QString operatorId;
    QString appVersion;
    QDateTime windowStart;
    QDateTime windowEnd;
    QDateTime startedAt;
    QDateTime finishedAt;
    QString status;
    QVector<PcTransferResult> pcs;
};

class Manifest {
public:
    static QString jobStateToString(JobState state);
    static QJsonObject toJson(const ManifestData &data);
    static bool writeAtomic(const QString &filePath,
                            const ManifestData &data,
                            QString *error);
};

} // namespace dc
