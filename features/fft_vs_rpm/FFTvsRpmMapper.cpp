#include "FFTvsRpmMapper.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <vector>

namespace FFTvsRpmMapper {

    namespace {
        // 线性幅值通路（Pa）
        constexpr bool kUseLinearRpmSplit = true;
    }

    double InterpRpmAtTime(
        const std::vector<double>& rpmSignal,
        double fsRpm,
        double tSec)
    {
        if (rpmSignal.empty() || fsRpm <= 0.0 || tSec < 0.0) return 0.0;

        const double idx = tSec * fsRpm;
        const size_t i0 = static_cast<size_t>(std::floor(idx));
        const size_t i1 = i0 + 1;

        if (i0 >= rpmSignal.size()) return rpmSignal.back();
        if (i1 >= rpmSignal.size()) return rpmSignal[i0];

        const double a = idx - static_cast<double>(i0);
        return rpmSignal[i0] * (1.0 - a) + rpmSignal[i1] * a;
    }

    RpmSpectrogram MapTimeToRpm(
        const Spectrogram& sp,
        const std::vector<double>& frameTimeSec,
        const std::vector<double>& rpmSignal,
        double fsRpm,
        double rpmStep)
    {
        RpmSpectrogram out;
        out.fs = sp.fs;
        out.blockSize = sp.blockSize;
        out.freqBins = sp.freqBins;
        out.rpmStep = (rpmStep > 1e-9 ? rpmStep : 50.0);

        if (sp.timeBins == 0 || sp.freqBins == 0) return out;
        if (frameTimeSec.size() != sp.timeBins) return out;
        if (rpmSignal.empty() || fsRpm <= 0.0) return out;
        if (sp.dataLinear.empty()) return out;

        // 固定rpm轴（对齐Artemis）
        out.rpmMin = 50.0;
        out.rpmMax = 4750.0;
        out.rpmBins = static_cast<size_t>(
            std::floor((out.rpmMax - out.rpmMin) / out.rpmStep)
            ) + 1;
        if (out.rpmBins == 0) return out;

        // 权重和 + Pa线性幅值累计
        std::vector<double> weightSum(out.rpmBins, 0.0);
        std::vector<double> accLin(out.rpmBins * out.freqBins, 0.0);

        double seenMin = std::numeric_limits<double>::infinity();
        double seenMax = -std::numeric_limits<double>::infinity();

        auto addFrameToBin = [&](size_t r, double w, size_t t) {
            if (r >= out.rpmBins || w <= 0.0) return;
            weightSum[r] += w;

            for (size_t f = 0; f < out.freqBins; ++f) {
                const double v = sp.atLinear(t, f); // Pa
                if (!std::isfinite(v)) continue;
                if (v < 0.0) continue; // 线性幅值不应为负
                accLin[r * out.freqBins + f] += w * v;
            }
            };

        for (size_t t = 0; t < sp.timeBins; ++t) {
            const double rpm = InterpRpmAtTime(rpmSignal, fsRpm, frameTimeSec[t]);
            seenMin = std::min(seenMin, rpm);
            seenMax = std::max(seenMax, rpm);

            const double x = (rpm - out.rpmMin) / out.rpmStep;

            if (kUseLinearRpmSplit) {
                const long long r0 = static_cast<long long>(std::floor(x));
                const long long r1 = r0 + 1;
                const double a = x - static_cast<double>(r0);

                if (r0 >= 0 && r0 < static_cast<long long>(out.rpmBins)) {
                    addFrameToBin(static_cast<size_t>(r0), (1.0 - a), t);
                }
                if (r1 >= 0 && r1 < static_cast<long long>(out.rpmBins)) {
                    addFrameToBin(static_cast<size_t>(r1), a, t);
                }
            }
            else {
                const long long ridx = static_cast<long long>(std::llround(x));
                if (ridx < 0 || ridx >= static_cast<long long>(out.rpmBins)) {
                    continue;
                }
                addFrameToBin(static_cast<size_t>(ridx), 1.0, t);
            }
        }

        // 注意：沿用 dataDb 字段存放“线性Pa”以减少结构改动
        out.dataDb.assign(out.rpmBins * out.freqBins, 0.0);

        for (size_t r = 0; r < out.rpmBins; ++r) {
            const double w = weightSum[r];
            if (w <= 0.0) continue;

            const double invW = 1.0 / w;
            for (size_t f = 0; f < out.freqBins; ++f) {
                const double meanLin = accLin[r * out.freqBins + f] * invW; // Pa
                out.at(r, f) = meanLin;
            }
        }

        std::cout << "[DEBUG] FFTvsRpmMapper(Pa):"
            << " useLinearRpmSplit=" << (kUseLinearRpmSplit ? 1 : 0)
            << ", rpmMin=" << out.rpmMin
            << ", rpmMax=" << out.rpmMax
            << ", rpmStep=" << out.rpmStep
            << ", rpmBins=" << out.rpmBins
            << ", timeBins=" << sp.timeBins
            << ", freqBins=" << sp.freqBins
            << ", seenRpm=[" << seenMin << ", " << seenMax << "]"
            << std::endl;

        return out;
    }

} // namespace FFTvsRpmMapper