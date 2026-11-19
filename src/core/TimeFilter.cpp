#include "TimeFilter.h"

#include <QRegularExpression>

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

FilteredPc TimeFilter::filterPc(const PcListing &listing,
                                const QVector<LogSourceConfig> &logSources,
                                const TimeWindow &window,
                                bool logsIncludeModifiedAfterEnd) {
    FilteredPc result;
    result.pcId = listing.pcId;

    if (!window.isValid()) {
        return result;
    }

    for (const FileEntry &entry : listing.screenshots) {
        if (!entry.mtime.isValid()) {
            continue;
        }
        if (entry.mtime >= window.start && entry.mtime <= window.end) {
            result.screenshots.append(entry);
            result.shotsBytes += entry.size;
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
