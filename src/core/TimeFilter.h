#pragma once

#include "Types.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

namespace dc {

class TimeFilter {
public:
    static QDate extractFilenameDate(const QString &relPath);
    static QDateTime extractFilenameDateTime(const QString &relPath);

    static FilteredPc filterPc(const PcListing &listing,
                               const QVector<LogSourceConfig> &logSources,
                               const TimeWindow &window,
                               bool logsIncludeModifiedAfterEnd,
                               bool includePictures,
                               bool includeRecordings);
};

} // namespace dc
