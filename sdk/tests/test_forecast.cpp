#include "goldsdk/forecast.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

using namespace goldsdk;

namespace {

int g_passed = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " -> " << #cond << std::endl; \
            return 1;                                                                              \
        }                                                                                          \
        ++g_passed;                                                                                \
    } while (0)

double winklerScore(double l, double u, double actH, double actL, double alpha = 0.20) {
    const double width = std::max(0.0, u - l);
    double missLow = (actL < l) ? (l - actL) : 0.0;
    double missHigh = (actH > u) ? (actH - u) : 0.0;
    return width + (2.0 / alpha) * (missLow + missHigh);
}

std::vector<DailyBar> makeTrendBars(int count, double startPx, double step, double halfRange) {
    std::vector<DailyBar> bars;
    bars.reserve(static_cast<size_t>(count));
    double px = startPx;
    for (int i = 0; i < count; ++i) {
        DailyBar b;
        b.open = px;
        b.close = px + step;
        b.high = std::max(b.open, b.close) + halfRange;
        b.low = std::min(b.open, b.close) - halfRange;
        b.source = "zs";
        b.unit = "CNY/g";
        bars.push_back(b);
        px = b.close;
    }
    return bars;
}

std::vector<DailyBar> makeRangeBars(int count, double centerPx, double amp, double halfRange) {
    std::vector<DailyBar> bars;
    bars.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double sign = (i % 2 == 0) ? 1.0 : -1.0;
        DailyBar b;
        b.open = centerPx - sign * amp * 0.5;
        b.close = centerPx + sign * amp * 0.5;
        b.high = std::max(b.open, b.close) + halfRange;
        b.low = std::min(b.open, b.close) - halfRange;
        b.source = "zs";
        b.unit = "CNY/g";
        bars.push_back(b);
    }
    return bars;
}

} // namespace

int main() {
    // 1. Wilder RSI + MultiDayTrendFromBars + Regime detection
    {
        const auto bullBars = makeTrendBars(30, 720.0, 2.2, 1.5);
        const auto bull = ForecastEngine::multiDayTrendFromBars(bullBars);
        CHECK(bull.valid);
        CHECK(bull.bias == TrendBias::StrongBull);
        CHECK(bull.rsi14 > 75.0);
        CHECK(bull.efficiencyRatio > 0.65);
        CHECK(bull.regime == MarketRegime::Trending);
        CHECK(bull.atr14 > 2.5);

        const auto rangeBars = makeRangeBars(30, 750.0, 1.8, 1.2);
        const auto rng = ForecastEngine::multiDayTrendFromBars(rangeBars);
        CHECK(rng.valid);
        CHECK(rng.efficiencyRatio < 0.35);
        CHECK(rng.regime == MarketRegime::LowVolRange);
    }

    // 2. 2D Kalman Filter denoising & session gap resilience
    {
        std::vector<IntradayPoint> pts;
        const int64_t t0 = 1760000000000LL; // arbitrary UTC ms
        for (int m = 0; m < 60; ++m) {
            const double noise = (m % 2 == 0 ? 0.6 : -0.6);
            pts.push_back({t0 + static_cast<int64_t>(m) * 60000LL, 750.0 + 0.15 * m + noise});
        }
        const auto kf = ForecastEngine::kalmanTrend(pts, 8.0);
        CHECK(kf.valid);
        CHECK(kf.velocityPerMin > 0.05);
        CHECK(kf.velocityZ > 0.3);
        const double trueEnd = 750.0 + 0.15 * 59.0;
        CHECK(std::abs(kf.filteredPrice - trueEnd) < 1.2);

        // With 30-min gap + jump, velocity must remain bounded
        pts.push_back({t0 + 90LL * 60000LL, trueEnd + 5.0});
        const auto kfGap = ForecastEngine::kalmanTrend(pts, 8.0);
        CHECK(kfGap.valid);
        CHECK(std::isfinite(kfGap.velocityPerMin));
        CHECK(std::isfinite(kfGap.velocityZ));
    }

    // 3. HAR-RV + Garman-Klass Volatility Clustering
    {
        auto bars = makeRangeBars(25, 750.0, 1.0, 1.5);
        // Last 5 days spike in volatility
        for (size_t i = 20; i < bars.size(); ++i) {
            bars[i].open = 750.0;
            bars[i].close = (i % 2 == 0) ? 758.0 : 742.0;
            bars[i].high = 763.0;
            bars[i].low = 737.0;
        }
        std::vector<IntradayPoint> dummyPts = {{1760000000000LL, 750.0}, {1760000300000LL, 754.0}};
        const auto vol = ForecastEngine::estimateVolatility(bars, dummyPts, 6.0);
        CHECK(vol.valid);
        CHECK(vol.regimeVolRatio > 1.20);
        CHECK(vol.harRvRange > 10.0);
        CHECK(vol.garmanKlassVol > 8.0);
    }

    // 4. Online Conformal Scaling
    {
        std::vector<double> ph(20, 755.0), pl(20, 750.0);
        std::vector<double> ahWide(20, 759.0), alWide(20, 746.0);
        const double scaleUp = ForecastEngine::computeConformalScale(ph, pl, ahWide, alWide, 0.80);
        CHECK(scaleUp > 1.08);

        std::vector<double> phW(20, 775.0), plW(20, 730.0);
        std::vector<double> ahNar(20, 754.0), alNar(20, 751.0);
        const double scaleDown = ForecastEngine::computeConformalScale(phW, plW, ahNar, alNar, 0.80);
        CHECK(scaleDown < 0.95);
    }

    // 5. dayRangeWithContext: Asymmetric Drift, Cross-Market Lead, Historical Buckets
    {
        const auto bars = makeTrendBars(25, 730.0, 1.2, 3.0);
        const auto trend = ForecastEngine::multiDayTrendFromBars(bars);
        const int64_t t0 = 1760000000000LL;
        std::vector<IntradayPoint> upPts;
        for (int m = 0; m < 40; ++m) {
            upPts.push_back({t0 + static_cast<int64_t>(m) * 60000LL, 758.0 + 0.08 * m});
        }
        ForecastContext bullCtx;
        bullCtx.actHigh = 761.5;
        bullCtx.actLow = 758.0;
        bullCtx.dayFraction = 0.40;
        bullCtx.atr = 9.0;
        bullCtx.prevClose = 758.0;
        bullCtx.dailyBars = bars;
        bullCtx.multiDayTrend = trend;
        bullCtx.crossMarketLeadSignal = 2.5;
        bullCtx.sentimentScore = 4.0;
        bullCtx.historicalPeakBucketCounts.assign(48, 0);
        bullCtx.historicalPeakBucketCounts[42] = 25; // 21:00-21:30 peak concentration

        ForecastContext bearCtx = bullCtx;
        bearCtx.crossMarketLeadSignal = -2.5;
        bearCtx.sentimentScore = -4.0;
        bearCtx.multiDayTrend.bias = TrendBias::StrongBear;
        bearCtx.multiDayTrend.score = -2.5;

        const auto bullFr = ForecastEngine::dayRangeWithContext(upPts, bullCtx);
        const auto bearFr = ForecastEngine::dayRangeWithContext(upPts, bearCtx);
        CHECK(bullFr.valid && bearFr.valid);
        CHECK(bullFr.predHigh >= bullCtx.actHigh);
        CHECK(bullFr.predLow <= bullCtx.actLow);
        const double bullMid = 0.5 * (bullFr.predHigh + bullFr.predLow);
        const double bearMid = 0.5 * (bearFr.predHigh + bearFr.predLow);
        CHECK(bullMid > bearMid);
        CHECK(bullFr.remainingUpside >= bearFr.remainingUpside);
    }

    // 6. Time-bucket resampling + identifyPattern stability
    {
        const int64_t t0 = 1760000000000LL;
        std::vector<IntradayPoint> irregularPts;
        // Dense ticks at start (every 5s), sparse ticks later (every 60s), forming a W double-bottom
        for (int i = 0; i < 60; ++i) {
            irregularPts.push_back({t0 + static_cast<int64_t>(i) * 5000LL, 760.0 - 0.08 * i});
        }
        for (int m = 6; m < 45; ++m) {
            double px = 755.2;
            if (m < 15) px = 755.2 + 0.45 * (m - 6);      // rally to ~759.2
            else if (m < 25) px = 759.2 - 0.40 * (m - 15); // second dip ~755.2
            else px = 755.2 + 0.35 * (m - 25);            // breakout above 760
            irregularPts.push_back({t0 + static_cast<int64_t>(m) * 60000LL, px});
        }
        const auto resampled = ForecastEngine::resamplePoints(irregularPts, 1);
        CHECK(resampled.size() >= 35 && resampled.size() <= 50);

        PatternConfig pcfg;
        pcfg.atr = 8.0;
        pcfg.feeRate = 0.004;
        const auto pat = ForecastEngine::identifyPattern(irregularPts, pcfg);
        CHECK(pat.valid);
    }

    // 7. evaluateDecision 0.4% fee-edge filter
    {
        const int64_t t0 = 1760000000000LL;
        std::vector<IntradayPoint> flatPts;
        for (int m = 0; m < 40; ++m) {
            flatPts.push_back({t0 + static_cast<int64_t>(m) * 60000LL, 750.0 + 0.15 * std::sin(m * 0.4)});
        }
        DecisionConfig dcfg;
        dcfg.feeRate = 0.004; // 0.4% -> ~3.0 CNY/g fee!
        dcfg.currentPrice = 750.05;
        dcfg.atr = 2.0;
        dcfg.predHigh = 751.0; // only +0.95 upside, cannot cover 3.0 fee
        dcfg.predLow = 749.2;
        dcfg.hasForecast = true;
        const auto dec = ForecastEngine::evaluateDecision(flatPts, dcfg);
        CHECK(dec.valid);
        CHECK(!dec.feeEdgeSatisfied);
        CHECK(dec.action != DecisionAction::StrongBuy);
    }

    // 8. Synthetic Multi-Regime Walk-Forward Benchmark (PICP & Winkler Score)
    {
        std::mt19937 rng(42);
        std::normal_distribution<double> stdNorm(0.0, 1.0);

        std::vector<DailyBar> histBars;
        std::vector<double> recPredH, recPredL, recActH, recActL;

        double dayOpen = 740.0;
        double modelWinklerSum = 0.0;
        double naiveWinklerSum = 0.0;
        int modelCovered = 0;
        int naiveCovered = 0;
        int evalDays = 0;

        for (int d = 0; d < 90; ++d) {
            // Regime switches: calm (0..29), high-vol crisis (30..59), trend (60..89)
            double dailySigma = 3.2;
            double drift = 0.0;
            if (d >= 30 && d < 60) {
                dailySigma = 9.5;
            } else if (d >= 60) {
                dailySigma = 5.0;
                drift = 2.5;
            }

            // Simulate 48 half-hour steps inside the day
            std::vector<IntradayPoint> dayPts;
            double px = dayOpen;
            double dHigh = px, dLow = px;
            double midHigh = px, midLow = px;
            const int64_t dayT0 = 1760000000000LL + static_cast<int64_t>(d) * 86400000LL;
            const double stepSigma = dailySigma / std::sqrt(48.0);
            const double stepDrift = drift / 48.0;

            for (int k = 0; k < 48; ++k) {
                if (k > 0) {
                    px += stepDrift + stepSigma * stdNorm(rng);
                }
                dHigh = std::max(dHigh, px);
                dLow = std::min(dLow, px);
                if (k <= 22) { // observe first ~45% of session
                    midHigh = std::max(midHigh, px);
                    midLow = std::min(midLow, px);
                    dayPts.push_back({dayT0 + static_cast<int64_t>(k) * 1800000LL, px});
                }
            }

            if (d >= 30) {
                ForecastContext ctx;
                ctx.actHigh = midHigh;
                ctx.actLow = midLow;
                ctx.dayFraction = 0.45;
                ctx.prevClose = histBars.back().close;
                ctx.dailyBars = histBars;
                ctx.multiDayTrend = ForecastEngine::multiDayTrendFromBars(histBars);
                ctx.conformalScale = ForecastEngine::computeConformalScale(recPredH, recPredL, recActH, recActL, 0.80);
                // naive 10-day ATR
                double sumTr = 0.0;
                for (size_t j = histBars.size() - 10; j < histBars.size(); ++j) {
                    sumTr += (histBars[j].high - histBars[j].low);
                }
                const double atr10 = sumTr / 10.0;
                ctx.atr = atr10;

                const auto fr = ForecastEngine::dayRangeWithContext(dayPts, ctx);
                CHECK(fr.valid);

                // Naive baseline: current range +/- 0.35 * atr10
                const double naiveH = midHigh + atr10 * 0.35;
                const double naiveL = midLow - atr10 * 0.35;

                const double tol = std::max(0.3, dHigh * 0.0005);
                if (dHigh <= fr.predHigh + tol && dLow >= fr.predLow - tol) ++modelCovered;
                if (dHigh <= naiveH + tol && dLow >= naiveL - tol) ++naiveCovered;

                modelWinklerSum += winklerScore(fr.predLow, fr.predHigh, dHigh, dLow, 0.20);
                naiveWinklerSum += winklerScore(naiveL, naiveH, dHigh, dLow, 0.20);
                ++evalDays;

                recPredH.push_back(fr.predHigh);
                recPredL.push_back(fr.predLow);
                recActH.push_back(dHigh);
                recActL.push_back(dLow);
            }

            DailyBar bar;
            bar.open = dayOpen;
            bar.high = dHigh;
            bar.low = dLow;
            bar.close = px;
            histBars.push_back(bar);
            dayOpen = px;
        }

        const double modelPicp = static_cast<double>(modelCovered) / evalDays;
        const double modelMeanWinkler = modelWinklerSum / evalDays;
        const double naiveMeanWinkler = naiveWinklerSum / evalDays;
        std::cout << "[Walk-Forward Benchmark 60d] Model PICP=" << (modelPicp * 100.0)
                  << "% | Model Winkler=" << modelMeanWinkler
                  << " vs Naive Winkler=" << naiveMeanWinkler << std::endl;

        CHECK(modelPicp >= 0.68);
        CHECK(modelMeanWinkler < naiveMeanWinkler);
    }

    std::cout << "All " << g_passed << " assertions passed!" << std::endl;
    return 0;
}
