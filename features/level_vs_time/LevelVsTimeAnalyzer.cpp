#include "LevelVsTimeAnalyzer.h"
#include "preprocess/Preprocessing.h"
#include "preprocess/Weighting.h"

#include <algorithm>
#include <cmath>

namespace LevelVsTimeAnalyzer {

    namespace
    {
        constexpr double EPS = 1e-30;

        double safeDbPower(double power, double refPower)
        {
            const double denom = (refPower > EPS) ? refPower : EPS;
            const double num = (power > EPS) ? power : EPS;
            return 10.0 * std::log10(num / denom);
        }

        double clampPositive(double v, double fallbackValue)
        {
            return (v > 0.0) ? v : fallbackValue;
        }

        void updateMinMax(LevelSeries& out, double db, bool& first)
        {
            if (first) {
                out.maxLevelDb = db;
                out.minLevelDb = db;
                first = false;
            }
            else {
                out.maxLevelDb = std::max(out.maxLevelDb, db);
                out.minLevelDb = std::min(out.minLevelDb, db);
            }
        }

        void buildPower(const DVector& sig, double cal, DVector& power)
        {
            power.resize(sig.size());
            for (size_t i = 0; i < sig.size(); ++i) {
                const double xcal = sig[i] * cal;
                power[i] = xcal * xcal;
            }
        }

        double meanPower(const DVector& v, size_t begin, size_t end)
        {
            if (v.empty()) return EPS;
            if (begin >= v.size()) return EPS;
            if (end > v.size()) end = v.size();
            if (end <= begin) return EPS;

            double sum = 0.0;
            for (size_t i = begin; i < end; ++i) sum += v[i];
            return sum / static_cast<double>(end - begin);
        }

        double getDefaultOutputStepSec(TimeWeightingMode mode, const FFTParams& p)
        {
            switch (mode) {
            case TimeWeightingMode::Fast:
                return 0.005333333;
            case TimeWeightingMode::Slow:
                return 0.042666667;
            case TimeWeightingMode::Impulse:
                return 0.001333333;
            case TimeWeightingMode::Rectangle:
                return 0.005333333;
            case TimeWeightingMode::Manual: {
                const double tau = (p.level_time_constant_sec > 0.0) ? p.level_time_constant_sec : 0.125;
                return std::max(tau / 24.0, 0.001);
            }
            default:
                return 0.005333333;
            }
        }

        // 单时间常数指数时间计权（Fast/Slow/Manual）
        DVector computeSingleTauWeighting(
            const DVector& power,
            double fs,
            double tau,
            bool doWarmup)
        {
            DVector y;
            if (power.empty() || fs <= 0.0) return y;

            y.resize(power.size(), EPS);

            const double dt = 1.0 / fs;
            const double tauEff = std::max(tau, 1e-6);

            const size_t nInit = std::max<size_t>(1, static_cast<size_t>(std::llround(tauEff * fs)));
            const size_t initEnd = std::min(nInit, power.size());
            double pInit = meanPower(power, 0, initEnd);
            if (pInit < EPS) pInit = EPS;

            double state = pInit;

            if (doWarmup) {
                const size_t nWarm = std::max<size_t>(1, static_cast<size_t>(std::llround(4.0 * tauEff * fs)));
                const double aWarm = std::exp(-dt / tauEff);
                for (size_t i = 0; i < nWarm; ++i) {
                    state = aWarm * state + (1.0 - aWarm) * pInit;
                    if (state < EPS) state = EPS;
                }
            }

            const double a = std::exp(-dt / tauEff);
            y[0] = state;

            for (size_t n = 1; n < power.size(); ++n) {
                const double in = power[n];
                state = a * state + (1.0 - a) * in;
                if (state < EPS) state = EPS;
                y[n] = state;
            }

            return y;
        }

        // Impulse：双时间常数一阶时间计权
        // 上升：35 ms
        // 下降：1.5 s
        // 初始化：首个 35 ms 线性功率均值
        DVector computeImpulseWeighting(
            const DVector& power,
            double fs)
        {
            DVector y;
            if (power.empty() || fs <= 0.0) return y;

            y.resize(power.size(), EPS);

            constexpr double tauRise = 0.035;
            constexpr double tauFall = 1.5;

            const double dt = 1.0 / fs;
            const double aRise = std::exp(-dt / tauRise);
            const double aFall = std::exp(-dt / tauFall);

            const size_t nInit = std::max<size_t>(1, static_cast<size_t>(std::llround(tauRise * fs)));
            const size_t initEnd = std::min(nInit, power.size());

            double state = meanPower(power, 0, initEnd);
            if (state < EPS) state = EPS;

            y[0] = state;

            for (size_t n = 1; n < power.size(); ++n) {
                const double in = std::max(power[n], EPS);

                if (in >= state) {
                    state = aRise * state + (1.0 - aRise) * in;
                }
                else {
                    state = aFall * state + (1.0 - aFall) * in;
                }

                if (state < EPS) state = EPS;
                y[n] = state;
            }

            return y;
        }

        // 点采样导出（Impulse 使用）
        void exportDownsampledLevelsPointSample(
            LevelSeries& out,
            const DVector& weightedPower,
            double fs,
            double refPower,
            size_t outputStepSamples)
        {
            if (weightedPower.empty() || fs <= 0.0 || outputStepSamples == 0) return;

            bool first = true;
            for (size_t rawIdx = 0; rawIdx < weightedPower.size(); rawIdx += outputStepSamples) {
                const double timeSec = static_cast<double>(rawIdx) / fs;
                const double levelDb = safeDbPower(weightedPower[rawIdx], refPower);
                out.points.push_back({ timeSec, levelDb });
                updateMinMax(out, levelDb, first);
            }
        }

        // 块均值导出（Fast/Slow/Manual）
        void exportDownsampledLevelsByBlockMean(
            LevelSeries& out,
            const DVector& weightedPower,
            double fs,
            double refPower,
            size_t outputStepSamples)
        {
            if (weightedPower.empty() || fs <= 0.0 || outputStepSamples == 0) return;

            DVector prefix(weightedPower.size() + 1, 0.0);
            for (size_t i = 0; i < weightedPower.size(); ++i) {
                prefix[i + 1] = prefix[i] + weightedPower[i];
            }

            bool first = true;
            for (size_t rawIdx = 0; rawIdx < weightedPower.size(); rawIdx += outputStepSamples) {
                const size_t end = std::min(weightedPower.size(), rawIdx + outputStepSamples);
                if (end <= rawIdx) break;

                const double meanP =
                    (prefix[end] - prefix[rawIdx]) / static_cast<double>(end - rawIdx);

                const double timeSec = static_cast<double>(rawIdx) / fs;
                const double levelDb = safeDbPower(meanP, refPower);

                out.points.push_back({ timeSec, levelDb });
                updateMinMax(out, levelDb, first);
            }
        }

        size_t reflectIndex(long long idx, size_t N)
        {
            if (N == 0) return 0;
            if (N == 1) return 0;

            const long long period = 2LL * static_cast<long long>(N - 1);
            idx %= period;
            if (idx < 0) idx += period;

            if (idx < static_cast<long long>(N)) return static_cast<size_t>(idx);
            return static_cast<size_t>(period - idx);
        }

        double meanPowerWithReflectPadding(
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

        // Rectangle：固定窗长矩形均值 + 左对齐帧推进 + 0.4W 时间标签 + 有限尾段容忍
        void exportRectangleLevelsByFrames(
            LevelSeries& out,
            const DVector& power,
            double fs,
            double refPower,
            size_t windowSamples,
            size_t outputStepSamples)
        {
            if (power.empty() || fs <= 0.0 || windowSamples == 0 || outputStepSamples == 0) return;

            const size_t N = power.size();
            bool first = true;

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
                    meanP = meanPowerWithReflectPadding(
                        power,
                        startLL,
                        windowSamples
                    );
                }

                const double timeSec = t0 + static_cast<double>(k) * dt;
                const double levelDb = safeDbPower(meanP, refPower);

                out.points.push_back({ timeSec, levelDb });
                updateMinMax(out, levelDb, first);
            }
        }
    }

    TimeWeightingMode ParseTimeWeighting(const std::string& s)
    {
        std::string v = s;
        std::transform(v.begin(), v.end(), v.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (v == "fast")      return TimeWeightingMode::Fast;
        if (v == "slow")      return TimeWeightingMode::Slow;
        if (v == "impulse")   return TimeWeightingMode::Impulse;
        if (v == "rectangle") return TimeWeightingMode::Rectangle;
        if (v == "manual")    return TimeWeightingMode::Manual;

        return TimeWeightingMode::Fast;
    }

    LevelSeries Compute(const DVector& x, double fs, const FFTParams& p)
    {
        LevelSeries out;
        out.fs = fs;
        if (x.empty() || fs <= 0.0) return out;

        DVector sig = x;
        Preprocessing::remove_dc(sig);

        Weighting::apply_weighting_time_domain(
            sig,
            static_cast<uint32_t>(std::llround(fs)),
            p.weight_type
        );

        const TimeWeightingMode mode = ParseTimeWeighting(p.time_weighting);

        const double outputStepSec = clampPositive(
            p.level_output_step_sec,
            getDefaultOutputStepSec(mode, p)
        );

        size_t outputStepSamples = static_cast<size_t>(std::llround(outputStepSec * fs));
        if (outputStepSamples == 0) outputStepSamples = 1;
        out.outputStepSamples = outputStepSamples;

        const double ref = (p.octaveRefValue > 0.0) ? p.octaveRefValue : 20e-6;
        const double cal = (p.calibrationFactor > 0.0) ? p.calibrationFactor : 1.0;
        const double refPower = ref * ref;

        DVector power;
        buildPower(sig, cal, power);
        if (power.empty()) return out;

        if (mode == TimeWeightingMode::Fast) {
            const DVector y = computeSingleTauWeighting(power, fs, 0.125, true);
            exportDownsampledLevelsByBlockMean(out, y, fs, refPower, outputStepSamples);
            return out;
        }

        if (mode == TimeWeightingMode::Slow) {
            const DVector y = computeSingleTauWeighting(power, fs, 1.0, true);
            exportDownsampledLevelsByBlockMean(out, y, fs, refPower, outputStepSamples);
            return out;
        }

        if (mode == TimeWeightingMode::Impulse) {
            const DVector y = computeImpulseWeighting(power, fs);
            exportDownsampledLevelsPointSample(out, y, fs, refPower, outputStepSamples);
            return out;
        }

        if (mode == TimeWeightingMode::Manual) {
            const double tau = clampPositive(p.level_time_constant_sec, 0.125);
            const DVector y = computeSingleTauWeighting(power, fs, tau, true);
            exportDownsampledLevelsByBlockMean(out, y, fs, refPower, outputStepSamples);
            return out;
        }

        {
            const double windowSec = clampPositive(p.level_window_sec, 0.125);
            size_t windowSamples = static_cast<size_t>(std::llround(windowSec * fs));
            if (windowSamples == 0) windowSamples = 1;

            exportRectangleLevelsByFrames(out, power, fs, refPower, windowSamples, outputStepSamples);
            return out;
        }
    }

} // namespace LevelVsTimeAnalyzer 