#include "LevelVsTimeAnalyzer.h"
#include "preprocess/Preprocessing.h"
#include "preprocess/Weighting.h"
#include "preprocess/TimeWeighting.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>

namespace LevelVsTimeAnalyzer {

    namespace
    {
        constexpr double EPS = 1e-30;

        // ===== 调试开关 =====
        constexpr bool kEnableDebugLog = true;

        // ===== 对标开关：指数计权(F/S/I/Manual)导出策略 =====
        // false: 块均值（原逻辑）
        // true : 每块取末样点（更接近部分仪器显示/导出方式）
        constexpr bool kUseSampleLastForExpWeighting = true;

        const char* toTimeWeightingModeStr(TimeWeightingMode mode)
        {
            switch (mode) {
            case TimeWeightingMode::Fast:      return "Fast";
            case TimeWeightingMode::Slow:      return "Slow";
            case TimeWeightingMode::Impulse:   return "Impulse";
            case TimeWeightingMode::Rectangle: return "Rectangle";
            case TimeWeightingMode::Manual:    return "Manual";
            default:                           return "Unknown";
            }
        }

        void debugPrintParams(const FFTParams& p, double fs, size_t inputSize)
        {
            if (!kEnableDebugLog) return;

            std::cout << "\n========== [LevelVsTimeAnalyzer::Compute] ==========\n";
            std::cout << std::fixed << std::setprecision(9);
            std::cout << "fs                      : " << fs << "\n";
            std::cout << "input samples           : " << inputSize << "\n";
            std::cout << "time_weighting(raw)     : \"" << p.time_weighting << "\"\n";
            std::cout << "level_time_constant_sec : " << p.level_time_constant_sec << "\n";
            std::cout << "level_window_sec        : " << p.level_window_sec << "\n";
            std::cout << "level_output_step_sec   : " << p.level_output_step_sec << "\n";
            std::cout << "octaveRefValue          : " << p.octaveRefValue << "\n";
            std::cout << "calibrationFactor       : " << p.calibrationFactor << "\n";
            std::cout << "weight_type(enum int)   : " << static_cast<int>(p.weight_type)
                << " (" << Weighting::weight_type_to_string(p.weight_type) << ")\n";
            std::cout << "====================================================\n";
        }

        void debugPrintRuntimeDecision(
            TimeWeightingMode mode,
            double outputStepSec,
            size_t outputStepSamples,
            double cal,
            double fs)
        {
            if (!kEnableDebugLog) return;

            std::cout << std::fixed << std::setprecision(9);
            std::cout << "[LevelVsTimeAnalyzer] parsed mode      : " << toTimeWeightingModeStr(mode) << "\n";
            std::cout << "[LevelVsTimeAnalyzer] outputStepSec    : " << outputStepSec << "\n";
            std::cout << "[LevelVsTimeAnalyzer] outputStepSamples: " << outputStepSamples << "\n";
            std::cout << "[LevelVsTimeAnalyzer] calibrationFactor: " << cal << "\n";
            std::cout << "[LevelVsTimeAnalyzer] dt(1/fs)         : " << (fs > 0.0 ? 1.0 / fs : 0.0) << "\n";
            std::cout << "[LevelVsTimeAnalyzer] export(exp)      : "
                << (kUseSampleLastForExpWeighting ? "sample_last" : "block_mean") << "\n";
        }

        void debugPrintModeConstants(TimeWeightingMode mode, const FFTParams& p)
        {
            if (!kEnableDebugLog) return;

            if (mode == TimeWeightingMode::Fast) {
                std::cout << "[LevelVsTimeAnalyzer] mode=Fast, tau=0.125 s\n";
            }
            else if (mode == TimeWeightingMode::Slow) {
                std::cout << "[LevelVsTimeAnalyzer] mode=Slow, tau=1.0 s\n";
            }
            else if (mode == TimeWeightingMode::Impulse) {
                std::cout << "[LevelVsTimeAnalyzer] mode=Impulse, tauRise=0.035 s, tauFall=1.5 s\n";
            }
            else if (mode == TimeWeightingMode::Manual) {
                const double tau = (p.level_time_constant_sec > 0.0) ? p.level_time_constant_sec : 0.125;
                std::cout << "[LevelVsTimeAnalyzer] mode=Manual, tau=" << tau << " s\n";
            }
            else if (mode == TimeWeightingMode::Rectangle) {
                const double windowSec = (p.level_window_sec > 0.0) ? p.level_window_sec : 0.125;
                std::cout << "[LevelVsTimeAnalyzer] mode=Rectangle, windowSec=" << windowSec << " s\n";
            }
        }

        double safePaFromPower(double power)
        {
            const double p = (power > EPS) ? power : EPS;
            return std::sqrt(p); // Pa = sqrt(Pa^2)
        }

        double clampPositive(double v, double fallbackValue)
        {
            return (v > 0.0) ? v : fallbackValue;
        }

        void updateMinMax(LevelSeries& out, double pa, bool& first)
        {
            if (first) {
                out.maxLevelPa = pa;
                out.minLevelPa = pa;
                first = false;
            }
            else {
                out.maxLevelPa = std::max(out.maxLevelPa, pa);
                out.minLevelPa = std::min(out.minLevelPa, pa);
            }
        }

        void buildPower(const DVector& sig, double cal, DVector& power)
        {
            power.resize(sig.size());
            for (size_t i = 0; i < sig.size(); ++i) {
                const double xcal = sig[i] * cal;
                power[i] = xcal * xcal; // Pa^2
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
                return 0.001333333; // Artemis 导出常见值
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

        // 块均值导出（指数时间计权分支）
        void exportDownsampledLevelsByBlockMean(
            LevelSeries& out,
            const DVector& weightedPower,
            double fs,
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
                const double levelPa = safePaFromPower(meanP);

                out.points.push_back({ timeSec, levelPa });
                updateMinMax(out, levelPa, first);
            }
        }

        // 每块取末样点导出（指数时间计权分支）
        void exportDownsampledLevelsBySampleLast(
            LevelSeries& out,
            const DVector& weightedPower,
            double fs,
            size_t outputStepSamples)
        {
            if (weightedPower.empty() || fs <= 0.0 || outputStepSamples == 0) return;

            bool first = true;
            for (size_t rawIdx = 0; rawIdx < weightedPower.size(); rawIdx += outputStepSamples) {
                const size_t end = std::min(weightedPower.size(), rawIdx + outputStepSamples);
                if (end <= rawIdx) break;

                const size_t idxLast = end - 1;
                const double p = weightedPower[idxLast];

                // 时间标签与样点对齐（块末）
                const double timeSec = static_cast<double>(idxLast) / fs;
                const double levelPa = safePaFromPower(p);

                out.points.push_back({ timeSec, levelPa });
                updateMinMax(out, levelPa, first);
            }
        }

        void exportDownsampledLevelsForExpWeighting(
            LevelSeries& out,
            const DVector& weightedPower,
            double fs,
            size_t outputStepSamples)
        {
            if (kUseSampleLastForExpWeighting) {
                exportDownsampledLevelsBySampleLast(out, weightedPower, fs, outputStepSamples);
            }
            else {
                exportDownsampledLevelsByBlockMean(out, weightedPower, fs, outputStepSamples);
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

        // Rectangle：固定窗长矩形均值 + 左对齐帧推进 + 0.4W 时间标签 + 有限尾段容忍 -> 输出Pa
        void exportRectangleLevelsByFrames(
            LevelSeries& out,
            const DVector& power,
            double fs,
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
                    meanP = meanPowerWithReflectPadding(power, startLL, windowSamples);
                }

                const double timeSec = t0 + static_cast<double>(k) * dt;
                const double levelPa = safePaFromPower(meanP);

                out.points.push_back({ timeSec, levelPa });
                updateMinMax(out, levelPa, first);
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

        debugPrintParams(p, fs, x.size());

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

        const double cal = (p.calibrationFactor > 0.0) ? p.calibrationFactor : 1.0;

        debugPrintRuntimeDecision(mode, outputStepSec, outputStepSamples, cal, fs);
        debugPrintModeConstants(mode, p);

        DVector power;
        buildPower(sig, cal, power);
        if (power.empty()) return out;

        if (mode == TimeWeightingMode::Fast) {
            const DVector y = TimeWeighting::applyByMode(
                power, fs, TimeWeighting::Mode::Fast, 0.125, outputStepSamples, true);
            exportDownsampledLevelsForExpWeighting(out, y, fs, outputStepSamples);
            return out;
        }

        if (mode == TimeWeightingMode::Slow) {
            const DVector y = TimeWeighting::applyByMode(
                power, fs, TimeWeighting::Mode::Slow, 0.125, outputStepSamples, true);
            exportDownsampledLevelsForExpWeighting(out, y, fs, outputStepSamples);
            return out;
        }

        if (mode == TimeWeightingMode::Impulse) {
            // 关键修正：
            // 1) Impulse 初始窗口不再绑定输出步长
            // 2) 使用与 tauRise 对齐的固定窗口（约35ms）
            size_t impulseInitWindowSamples = static_cast<size_t>(std::llround(0.035 * fs));
            if (impulseInitWindowSamples == 0) impulseInitWindowSamples = 1;

            const DVector y = TimeWeighting::applyByMode(
                power, fs, TimeWeighting::Mode::Impulse, 0.125, impulseInitWindowSamples, true);
            exportDownsampledLevelsForExpWeighting(out, y, fs, outputStepSamples);
            return out;
        }

        if (mode == TimeWeightingMode::Manual) {
            const double tau = clampPositive(p.level_time_constant_sec, 0.125);
            const DVector y = TimeWeighting::applyByMode(
                power, fs, TimeWeighting::Mode::Manual, tau, outputStepSamples, true);
            exportDownsampledLevelsForExpWeighting(out, y, fs, outputStepSamples);
            return out;
        }

        {
            const double windowSec = clampPositive(p.level_window_sec, 0.125);
            size_t windowSamples = static_cast<size_t>(std::llround(windowSec * fs));
            if (windowSamples == 0) windowSamples = 1;

            exportRectangleLevelsByFrames(out, power, fs, windowSamples, outputStepSamples);
            return out;
        }
    }

} // namespace LevelVsTimeAnalyzer