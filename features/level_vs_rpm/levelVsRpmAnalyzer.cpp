#include "LevelVsRpmAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>
#include <iostream>

namespace {
    constexpr double EPS = 1e-30;

    static bool finite_pos(double v) {
        return std::isfinite(v) && v > 0.0;
    }

    static double db_to_power(double db) {
        return std::pow(10.0, db / 10.0);
    }

    static double power_to_db(double p) {
        const double pp = (p > EPS) ? p : EPS;
        return 10.0 * std::log10(pp);
    }

    // 线性插值（时间越界时夹到边界）
    static double sample_linear_at_time_clamped(
        const std::vector<double>& y, double fs, double tSec)
    {
        if (y.empty() || fs <= 0.0 || !std::isfinite(tSec)) {
            return std::numeric_limits<double>::quiet_NaN();
        }

        const double maxT = (y.size() > 1) ? (static_cast<double>(y.size() - 1) / fs) : 0.0;
        double tc = tSec;
        if (tc < 0.0) tc = 0.0;
        if (tc > maxT) tc = maxT;

        const double idx = tc * fs;
        const size_t i0 = static_cast<size_t>(std::floor(idx));
        const size_t i1 = (i0 + 1 < y.size()) ? (i0 + 1) : i0;

        const double a = idx - static_cast<double>(i0);
        return (1.0 - a) * y[i0] + a * y[i1];
    }

    struct BinAcc {
        double sumPower = 0.0; // dB转功率后累加
        int count = 0;
    };
}

LevelVsRpmSeries LevelVsRpmAnalyzer::ComputeFromLevelSeries(
    const LevelVsTimeAnalyzer::LevelSeries& levelSeries,
    const std::vector<double>& rpmSamples,
    double rpmFs,
    double rpmBinStep)
{
    LevelVsRpmSeries out;
    out.rpmStep = (rpmBinStep > 0.0) ? rpmBinStep : 50.0;

    if (levelSeries.points.empty() || rpmSamples.empty() || rpmFs <= 0.0) {
        std::cout << "[DEBUG][LvRpm] invalid input:"
            << " levelPoints=" << levelSeries.points.size()
            << ", rpmSamples=" << rpmSamples.size()
            << ", rpmFs=" << rpmFs << "\n";
        return out;
    }

    // Artemis风格刻度锚点：30,40,50,...
    const double step = out.rpmStep;
    const double anchor = 30.0;

    // 1) 将 level(t) 对齐到 rpm(t)
    struct Pair { double rpm; double db; };
    std::vector<Pair> pairs;
    pairs.reserve(levelSeries.points.size());

    size_t droppedNonFinite = 0;
    size_t droppedZeroRpm = 0;

    for (const auto& p : levelSeries.points) {
        if (!std::isfinite(p.timeSec) || !std::isfinite(p.levelDb)) {
            ++droppedNonFinite;
            continue;
        }

        double rpm = sample_linear_at_time_clamped(rpmSamples, rpmFs, p.timeSec);
        if (!std::isfinite(rpm)) {
            ++droppedNonFinite;
            continue;
        }

        rpm = std::abs(rpm);
        if (!finite_pos(rpm)) {
            ++droppedZeroRpm;
            continue;
        }

        pairs.push_back({ rpm, p.levelDb });
    }

    if (pairs.empty()) {
        std::cout << "[DEBUG][LvRpm] no valid aligned pairs."
            << " droppedNonFinite=" << droppedNonFinite
            << ", droppedZeroRpm=" << droppedZeroRpm << "\n";
        return out;
    }

    // 2) 分箱并做 Leq(bin): 先功率平均，再转dB
    std::map<long long, BinAcc> acc;
    double rpmRawMin = std::numeric_limits<double>::infinity();
    double rpmRawMax = 0.0;

    for (const auto& q : pairs) {
        rpmRawMin = std::min(rpmRawMin, q.rpm);
        rpmRawMax = std::max(rpmRawMax, q.rpm);

        const long long bin = static_cast<long long>(std::floor((q.rpm - anchor) / step));
        const double rpmOut = anchor + static_cast<double>(bin) * step;
        if (rpmOut < anchor) continue;

        auto& b = acc[bin];
        b.sumPower += db_to_power(q.db);
        b.count += 1;
    }

    if (acc.empty()) {
        std::cout << "[DEBUG][LvRpm] no bins after anchor clipping."
            << " anchor=" << anchor << ", step=" << step << "\n";
        return out;
    }

    out.points.clear();
    out.points.reserve(acc.size());

    for (const auto& kv : acc) {
        const long long bin = kv.first;
        const BinAcc& b = kv.second;
        if (b.count <= 0) continue;

        LevelRpmPoint pt;
        pt.rpm = anchor + static_cast<double>(bin) * step; // 30,40,50...
        pt.levelDb = power_to_db(b.sumPower / static_cast<double>(b.count)); // Leq(bin)
        out.points.push_back(pt);
    }

    std::sort(out.points.begin(), out.points.end(),
        [](const LevelRpmPoint& a, const LevelRpmPoint& b) { return a.rpm < b.rpm; });

    if (!out.points.empty()) {
        out.rpmMin = out.points.front().rpm;
        out.rpmMax = out.points.back().rpm;
    }
    else {
        out.rpmMin = 0.0;
        out.rpmMax = 0.0;
    }

    std::cout << "[DEBUG][LvRpm] done:"
        << " levelPoints=" << levelSeries.points.size()
        << ", pairs=" << pairs.size()
        << ", outPoints=" << out.points.size()
        << ", droppedNonFinite=" << droppedNonFinite
        << ", droppedZeroRpm=" << droppedZeroRpm
        << ", rpmRawMin=" << rpmRawMin
        << ", rpmRawMax=" << rpmRawMax
        << ", step=" << step
        << ", anchor=" << anchor
        << ", stat=LeqBin"
        << "\n";

    return out;
}