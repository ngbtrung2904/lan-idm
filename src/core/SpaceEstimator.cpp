#include "SpaceEstimator.h"

#include <algorithm>
#include <cmath>

namespace dc {

namespace {

qint64 withMargin(qint64 bytes, double marginFraction) {
    if (bytes <= 0) {
        return 0;
    }
    const double safeMargin = marginFraction > 0.0 ? marginFraction : 0.0;
    if (safeMargin == 0.0) {
        return bytes;
    }

    constexpr qint64 kScale = 1000000;
    const double scaledMargin = safeMargin * static_cast<double>(kScale);
    const qint64 ppm = static_cast<qint64>(std::llround(scaledMargin));
    if (ppm >= 0 && ppm <= kScale * 1000 && std::fabs(scaledMargin - static_cast<double>(ppm)) < 1e-6) {
        const qint64 whole = (bytes / kScale) * ppm;
        const qint64 rem = (bytes % kScale) * ppm;
        const qint64 extra = whole + (rem + kScale - 1) / kScale;
        return bytes + extra;
    }

    const long double scaled = static_cast<long double>(bytes) * (1.0L + static_cast<long double>(safeMargin));
    return static_cast<qint64>(std::ceil(scaled));
}

} // namespace

SpaceEstimate SpaceEstimator::estimate(const QVector<qint64> &selectedPcBytes,
                                       qint64 freeBytes,
                                       double marginFraction) {
    SpaceEstimate out;
    out.freeBytes = freeBytes > 0 ? freeBytes : 0;

    QVector<qint64> sanitized;
    sanitized.reserve(selectedPcBytes.size());

    qint64 total = 0;
    for (qint64 b : selectedPcBytes) {
        const qint64 clamped = b > 0 ? b : 0;
        sanitized.append(clamped);
        total += clamped;
    }

    out.neededBytes = total;
    out.requiredWithMarginBytes = withMargin(total, marginFraction);
    out.fits = (out.freeBytes > 0) && (out.requiredWithMarginBytes <= out.freeBytes);

    if (out.freeBytes <= 0 || sanitized.isEmpty()) {
        out.fittingPcCount = 0;
        return out;
    }

    std::sort(sanitized.begin(), sanitized.end());

    qint64 running = 0;
    int count = 0;
    for (qint64 pcBytes : sanitized) {
        const qint64 nextTotal = running + pcBytes;
        if (withMargin(nextTotal, marginFraction) <= out.freeBytes) {
            running = nextTotal;
            ++count;
        } else {
            break;
        }
    }
    out.fittingPcCount = count;
    return out;
}

} // namespace dc
