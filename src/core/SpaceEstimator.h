#pragma once

#include "Types.h"

#include <QVector>

namespace dc {

class SpaceEstimator {
public:
    static constexpr double kDefaultMarginFraction = 0.02;

    static SpaceEstimate estimate(const QVector<qint64> &selectedPcBytes,
                                  qint64 freeBytes,
                                  double marginFraction);
};

} // namespace dc
