#include "LevelVsRpmAnalyzer.h"

#include "preprocess/Preprocessing.h"
#include "preprocess/Weighting.h"
#include "preprocess/TimeWeighting.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>
#include <iostream>
#include <string>

namespace {
    constexpr double EPS = 1e-30;

    static bool finite_pos(double v) {
        return std::isfinite(v) && v > 0.0;
    }

    static double clampPositive(double v, double fallbackValue) {
        return (v > 0.0) ? v : fallbackValue;
    }

    // 线性插值（时间越界时夹到边界
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
        double sumPower = 0.0; // 累加 Pa^2
        int count = 0;
    };

    static double getDefaultOutputStepSec(const std::string& modeRaw, const FFTParams& p)
    {
        std::string v = modeRaw;
        std::transform(v.begin(), v.end(), v.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (v == "fast") return 0.005333333;
        if (v == "slow") return 0.042666667;
        if (v == "impulse") return 0.001333333;
        if (v == "rectangle") return 0.005333333;
        if (v == "manual") {
            const double tau = (p.level_time_constant_sec > 0.0) ? p.level_time_constant_sec : 0.125;
            return std::max(tau / 24.0, 0.001);
        }
        return 0.005333333;
    }

    static TimeWeighting::Mode parseExpModeOrDefaultFast(const std::string& s)
    {
        std::string v = s;
        std::transform(v.begin(), v.end(), v.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (v == "fast") return TimeWeighting::Mode::Fast;
        if (v == "slow") return TimeWeighting::Mode::Slow;
        if (v == "impulse") return TimeWeighting::Mode::Impulse;
        if (v == "manual") return TimeWeighting::Mode::Manual;
        return TimeWeighting::Mode::Fast;
    }

    static size_t reflectIndex(long long idx, size_t N)
    {
        if (N == 0) return 0;
        if (N == 1) return 0;

        const long long period = 2LL * static_cast<long long>(N - 1);
        idx %= period;
        if (idx < 0) idx += period;

        if (idx < static_cast<long long>(N)) return static_cast<size_t>(idx);
        return static_cast<size_t>(period - idx);
    }

    static double meanPower(const DVector& v, size_t begin, size_t end)
    {
        if (v.empty()) return EPS;
        if (begin >= v.size()) return EPS;
        if (end > v.size()) end = v.size();
        if (end <= begin) return EPS;

        double sum = 0.0;
        for (size_t i = begin; i < end; ++i) sum += v[i];
        return sum / static_cast<double>(end - begin);
    }

    static double meanPowerWithReflectPadding(
        const DVector& power,
        long long start,
        size_t windowSamples)
    {
        if (power.empty() || windowSamples == 0) return EPS;

        const size_t N = power.size();
        double sum = 0.0;
        for (size_t k = 0; k < windowSamples; ++k) {
            const long long idx = start + static_cast<long long>(k);
            const size_t ridx = reflectIndex(idx, N);
            sum += power[ridx];
        }
        return sum / static_cast<double>(windowSamples);
    }

    // 从功率序列导出 level(t,Pa) 点
    static std::vector<LevelVsRpmAnalyzer::LevelPointLite> buildLevelPointsFromPower(
        const DVector& power,
        double fs,
        const FFTParams& p)
    {
        std::vector<LevelVsRpmAnalyzer::LevelPointLite> out;
        if (power.empty() || fs <= 0.0) return out;

        std::string modeRaw = p.time_weighting;
        std::transform(modeRaw.begin(), modeRaw.end(), modeRaw.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        const double outputStepSec = clampPositive(
            p.level_output_step_sec,
            getDefaultOutputStepSec(modeRaw, p)
        );
        size_t outputStepSamples = static_cast<size_t>(std::llround(outputStepSec * fs));
        if (outputStepSamples == 0) outputStepSamples = 1;

        // Rectangle 单独处理；其余走 TimeWeighting
        if (modeRaw == "rectangle") {
            const double windowSec = clampPositive(p.level_window_sec, 0.125);
            size_t windowSamples = static_cast<size_t>(std::llround(windowSec * fs));
            if (windowSamples == 0) windowSamples = 1;

            const size_t N = power.size();
            const double t0 = 0.4 * static_cast<double>(windowSamples) / fs;
            const double dt = static_cast<double>(outputStepSamples) / fs;
            constexpr double kMaxOverflowRatio = 0.20;

            for (size_t k = 0;; ++k) {
                const long long startLL =
                    static_cast<long long>(k) * static_cast<long long>(outputStepSamples);

                if (startLL >= static_cast<long long>(N)) break;

                const long long endLL = startLL + static_cast<long long>(windowSamples);
                const long long overflow =
                    (endLL > static_cast<long long>(N))
                    ? (endLL - static_cast<long long>(N))
                    : 0LL;

                const double overflowRatio =
                    static_cast<double>(overflow) / static_cast<double>(windowSamples);

                if (overflowRatio > kMaxOverflowRatio) break;

                double meanP = EPS;
                if (overflow == 0) {
                    const size_t start = static_cast<size_t>(startLL);
                    const size_t end = start + windowSamples;
                    meanP = meanPower(power, start, end);
                }
                else {
                    meanP = meanPowerWithReflectPadding(power, startLL, windowSamples);
                }

                LevelVsRpmAnalyzer::LevelPointLite pt;
                pt.timeSec = t0 + static_cast<double>(k) * dt;
                pt.levelPa = std::sqrt(std::max(meanP, EPS));
                out.push_back(pt);
            }
            return out;
        }

        // Fast / Slow / Impulse / Manual
        const TimeWeighting::Mode m = parseExpModeOrDefaultFast(modeRaw);
        const double tau = clampPositive(p.level_time_constant_sec, 0.125);

        const DVector y = TimeWeighting::applyByMode(
            power, fs, m, tau, outputStepSamples, true);

        if (y.empty()) return out;

        DVector prefix(y.size() + 1, 0.0);
        for (size_t i = 0; i < y.size(); ++i) {
            prefix[i + 1] = prefix[i] + y[i];
        }

        for (size_t rawIdx = 0; rawIdx < y.size(); rawIdx += outputStepSamples) {
            const size_t end = std::min(y.size(), rawIdx + outputStepSamples);
            if (end <= rawIdx) break;

            const double meanP = (prefix[end] - prefix[rawIdx]) / static_cast<double>(end - rawIdx);

            LevelVsRpmAnalyzer::LevelPointLite pt;
            pt.timeSec = static_cast<double>(rawIdx) / fs;
            pt.levelPa = std::sqrt(std::max(meanP, EPS));
            out.push_back(pt);
        }

        return out;
    }
}

namespace LevelVsRpmAnalyzer {

    LevelVsRpmSeries ComputeFromLevelSeries(
        const std::vector<LevelPointLite>& levelPoints,
        const std::vector<double>& rpmSamples,
        double rpmFs,
        double rpmBinStep)
    {
        LevelVsRpmSeries out;
        out.rpmStep = (rpmBinStep > 0.0) ? rpmBinStep : 50.0;

        if (levelPoints.empty() || rpmSamples.empty() || rpmFs <= 0.0) {
            std::cout << "[DEBUG][LvRpm] invalid input:"
                << " levelPoints=" << levelPoints.size()
                << ", rpmSamples=" << rpmSamples.size()
                << ", rpmFs=" << rpmFs << "\n";
            return out;
        }

        // Artemis风格刻度锚点：30,40,50,...
        const double step = out.rpmStep;
        const double anchor = 30.0;

        struct Pair { double rpm; double pa; };
        std::vector<Pair> pairs;
        pairs.reserve(levelPoints.size());

        size_t droppedNonFinite = 0;
        size_t droppedZeroRpm = 0;

        for (const auto& p : levelPoints) {
            if (!std::isfinite(p.timeSec) || !std::isfinite(p.levelPa)) {
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

            pairs.push_back({ rpm, p.levelPa });
        }

        if (pairs.empty()) {
            std::cout << "[DEBUG][LvRpm] no valid aligned pairs."
                << " droppedNonFinite=" << droppedNonFinite
                << ", droppedZeroRpm=" << droppedZeroRpm << "\n";
            return out;
        }

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
            const double pa = std::max(q.pa, 0.0);
            b.sumPower += pa * pa;
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
            pt.rpm = anchor + static_cast<double>(bin) * step;
            const double meanPower = b.sumPower / static_cast<double>(b.count);
            pt.levelPa = std::sqrt(std::max(meanPower, EPS));
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
            << " levelPoints=" << levelPoints.size()
            << ", pairs=" << pairs.size()
            << ", outPoints=" << out.points.size()
            << ", droppedNonFinite=" << droppedNonFinite
            << ", droppedZeroRpm=" << droppedZeroRpm
            << ", rpmRawMin=" << rpmRawMin
            << ", rpmRawMax=" << rpmRawMax
            << ", step=" << step
            << ", anchor=" << anchor
            << ", stat=RMS(bin)_Pa"
            << "\n";

        return out;
    }

    LevelVsRpmSeries Compute(
        const DVector& signalSamples,
        double signalFs,
        const FFTParams& p,
        const std::vector<double>& rpmSamples,
        double rpmFs,
        double rpmBinStep)
    {
        LevelVsRpmSeries out;
        out.rpmStep = (rpmBinStep > 0.0) ? rpmBinStep : 50.0;

        if (signalSamples.empty() || signalFs <= 0.0 || rpmSamples.empty() || rpmFs <= 0.0) {
            std::cout << "[DEBUG][LvRpm] invalid input for Compute:"
                << " signalN=" << signalSamples.size()
                << ", signalFs=" << signalFs
                << ", rpmN=" << rpmSamples.size()
                << ", rpmFs=" << rpmFs << "\n";
            return out;
        }

        // 1) 时域预处理
        DVector sig = signalSamples;
        Preprocessing::remove_dc(sig);

        // 2) 频率计权（按参数，可为 None）
        Weighting::apply_weighting_time_domain(
            sig,
            static_cast<uint32_t>(std::llround(signalFs)),
            p.weight_type
        );

        // 3) 功率
        const double cal = (p.calibrationFactor > 0.0) ? p.calibrationFactor : 1.0;
        DVector power(sig.size(), 0.0);
        for (size_t i = 0; i < sig.size(); ++i) {
            const double x = sig[i] * cal;
            power[i] = x * x;
        }

        // 4) 生成 level(t, Pa) 点（时间计权）
        const auto levelPoints = buildLevelPointsFromPower(power, signalFs, p);
        if (levelPoints.empty()) {
            std::cout << "[DEBUG][LvRpm] levelPoints empty after time weighting\n";
            return out;
        }

        // 5) 对齐 RPM + 分箱
        return ComputeFromLevelSeries(levelPoints, rpmSamples, rpmFs, rpmBinStep);
    }

} // namespace LevelVsRpmAnalyzer