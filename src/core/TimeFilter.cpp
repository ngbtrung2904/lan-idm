#include "TimeFilter.h"

#include <QRegularExpression>
#include <QTime>

namespace dc {

QDate TimeFilter::extractFilenameDate(const QString &relPath) {
    const int slashPos = relPath.lastIndexOf(QLatin1Char('/'));
    const QString baseName = (slashPos >= 0) ? relPath.mid(slashPos + 1) : relPath;
    if (baseName.isEmpty()) {
        return {};
    }

    static const QRegularExpression kDateRegex(
        QStringLiteral("(?<!\\d)((?:19|20)\\d{2})(0[1-9]|1[0-2])(0[1-9]|[12]\\d|3[01])(?!\\d)"));

    QRegularExpressionMatchIterator it = kDateRegex.globalMatch(baseName);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int year = match.captured(1).toInt();
        const int month = match.captured(2).toInt();
        const int day = match.captured(3).toInt();
        if (QDate::isValid(year, month, day)) {
            return QDate(year, month, day);
        }
    }

    return {};
}

QDateTime TimeFilter::extractFilenameDateTime(const QString &relPath) {
    const int slashPos = relPath.lastIndexOf(QLatin1Char('/'));
    const QString baseName = (slashPos >= 0) ? relPath.mid(slashPos + 1) : relPath;
    if (baseName.isEmpty()) {
        return {};
    }

    auto tryMatch = [&baseName](const QRegularExpression &regex) -> QDateTime {
        QRegularExpressionMatchIterator it = regex.globalMatch(baseName);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const int year = match.captured(1).toInt();
            const int month = match.captured(2).toInt();
            const int day = match.captured(3).toInt();
            const int hour = match.captured(4).toInt();
            const int minute = match.captured(5).toInt();
            const int second = match.captured(6).toInt();
            if (QDate::isValid(year, month, day) && QTime::isValid(hour, minute, second)) {
                const QDateTime dt(QDate(year, month, day), QTime(hour, minute, second), Qt::LocalTime);
                if (dt.isValid()) {
                    return dt;
                }
            }
        }
        return {};
    };

    static const QRegularExpression kDashedDateTimeRegex(
        QStringLiteral("(?<!\\d)(\\d{4})-(\\d{2})-(\\d{2})[_T-](\\d{2})-(\\d{2})-(\\d{2})(?!\\d)"));
    const QDateTime dashed = tryMatch(kDashedDateTimeRegex);
    if (dashed.isValid()) {
        return dashed;
    }

    static const QRegularExpression kCompactDateTimeRegex(
        QStringLiteral("(?<!\\d)(\\d{4})(\\d{2})(\\d{2})[_-](\\d{2})(\\d{2})(\\d{2})(?!\\d)"));
    return tryMatch(kCompactDateTimeRegex);
}

FilteredPc TimeFilter::filterPc(const PcListing &listing,
                                const QVector<LogSourceConfig> &logSources,
                                const TimeWindow &window,
                                bool logsIncludeModifiedAfterEnd,
                                bool includePictures,
                                bool includeRecordings) {
    FilteredPc result;
    result.pcId = listing.pcId;

    if (!window.isValid()) {
        return result;
    }

    if (includePictures) {
        for (const FileEntry &entry : listing.screenshots) {
            const QDateTime fnDt = extractFilenameDateTime(entry.relPath);
            const QDateTime ts = fnDt.isValid() ? fnDt : entry.mtime;
            if (!ts.isValid()) {
                continue;
            }
            if (ts >= window.start && ts <= window.end) {
                result.screenshots.append(entry);
                result.shotsBytes += entry.size;
            }
        }
    }

    if (includeRecordings) {
        for (const FileEntry &entry : listing.recordings) {
            const QDateTime fnDt = extractFilenameDateTime(entry.relPath);
            const QDateTime ts = fnDt.isValid() ? fnDt : entry.mtime;
            if (!ts.isValid()) {
                continue;
            }
            if (ts >= window.start && ts <= window.end) {
                result.recordings.append(entry);
                result.recordingsBytes += entry.size;
            }
        }
    }

    const QDate startDate = window.start.date();
    const QDate endDate = window.end.date();

    for (const FileEntry &entry : listing.logs) {
        TimestampSource source = TimestampSource::Mtime;
        if (entry.sourceIndex >= 0 && entry.sourceIndex < logSources.size()) {
            source = logSources.at(entry.sourceIndex).timestampSource;
        }

        bool match = false;
        if (source == TimestampSource::Filename) {
            const QDate fileDate = extractFilenameDate(entry.relPath);
            match = fileDate.isValid() && fileDate >= startDate && fileDate <= endDate;
        } else {
            if (!entry.mtime.isValid()) {
                continue;
            }
            match = logsIncludeModifiedAfterEnd
                        ? (entry.mtime >= window.start)
                        : (entry.mtime >= window.start && entry.mtime <= window.end);
        }

        if (match) {
            result.logs.append(entry);
            result.logsBytes += entry.size;
        }
    }

    return result;
}

} // namespace dc
