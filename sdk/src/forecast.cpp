#include "goldsdk/forecast.hpp"
#include <cmath>
#include <algorithm>

namespace goldsdk {

DayRangeForecast ForecastEngine::dayRange(const std::vector<IntradayPoint>& points,
                                          double actHigh,
                                          double actLow,
                                          double dayFraction) {
    DayRangeForecast out;
    if (points.size() < 3)
        return out;

    double lastPrice = points.back().price;
    if (actHigh <= 0 || actLow <= 0) {
        actHigh = actLow = points.front().price;
        for (const auto& p : points) {
            actHigh = std::max(actHigh, p.price);
            actLow = std::min(actLow, p.price);
        }
    }

    const int n = (int)points.size();
    const int w = std::min(30, n);
    double mean = 0.0;
    for (int i = n - w; i < n; ++i)
        mean += points[i].price;
    mean /= double(w);
    double var = 0.0;
    for (int i = n - w; i < n; ++i) {
        const double d = points[i].price - mean;
        var += d * d;
    }
    const double stdev = std::sqrt(var / double(std::max(1, w - 1)));
    const double rangeSoFar = std::max(0.01, actHigh - actLow);

    dayFraction = std::clamp(dayFraction, 0.0, 1.0);
    const double remain = std::clamp(1.0 - dayFraction, 0.08, 1.0);
    const double timeScale = std::sqrt(remain);

    double expand = std::max(1.0 * stdev, 0.12 * rangeSoFar);
    expand *= (0.35 + 0.40 * timeScale);
    const double hard = lastPrice * 0.0045;
    expand = std::min(expand, hard);
    expand = std::min(expand, rangeSoFar * 0.55 + stdev);
    expand = std::max(expand, lastPrice * 0.0003);

    double predHigh = std::max(actHigh, lastPrice) + expand * 0.55;
    double predLow = std::min(actLow, lastPrice) - expand * 0.55;
    predHigh = std::max(predHigh, actHigh);
    predLow = std::min(predLow, actLow);
    const double maxWidth = std::min(hard * 1.6, rangeSoFar * 1.35 + 2.0 * stdev);
    if (predHigh - predLow > maxWidth) {
        predHigh = std::min(predHigh, lastPrice + maxWidth * 0.55);
        predLow = std::max(predLow, lastPrice - maxWidth * 0.55);
        predHigh = std::max(predHigh, actHigh);
        predLow = std::min(predLow, actLow);
    }
    out.predHigh = predHigh;
    out.predLow = predLow;
    out.valid = predHigh > predLow && predHigh > 0.0;
    return out;
}

} // namespace goldsdk
