#include "TimeFilter.h"

namespace dc {

FilteredPc TimeFilter::filterPc(const PcListing &listing,
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

    for (const FileEntry &entry : listing.logs) {
        if (!entry.mtime.isValid()) {
            continue;
        }
        const bool match = logsIncludeModifiedAfterEnd
                               ? (entry.mtime >= window.start)
                               : (entry.mtime >= window.start && entry.mtime <= window.end);
        if (match) {
            result.logs.append(entry);
            result.logsBytes += entry.size;
        }
    }

    return result;
}

} // namespace dc
