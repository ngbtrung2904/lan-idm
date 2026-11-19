#pragma once

#include "Types.h"

namespace dc {

class TimeFilter {
public:
    static FilteredPc filterPc(const PcListing &listing,
                               const TimeWindow &window,
                               bool logsIncludeModifiedAfterEnd);
};

} // namespace dc
