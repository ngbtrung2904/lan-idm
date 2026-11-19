#pragma once

#include "Types.h"

#include <QDate>
#include <QString>
#include <QVector>

namespace dc {

class TimeFilter {
public:
    static QDate extractFilenameDate(const QString &relPath);

    static FilteredPc filterPc(const PcListing &listing,
                               const QVector<LogSourceConfig> &logSources,
                               const TimeWindow &window,
                               bool logsIncludeModifiedAfterEnd);
};

} // namespace dc
