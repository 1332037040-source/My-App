#include "FFTvsRpmMapper.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace FFTvsRpmMapper {

    double InterpRpmAtTime(
        const std::vector<double>& rpmSignal,
        double fsRpm,
        double tSec
    ) {
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
        double rpmStep
    ) {
        RpmSpectrogram out;
        out.fs = sp.fs;
        out.blockSize = sp.blockSize;
        out.freqBins = sp.freqBins;
        out.rpmStep = (rpmStep > 1e-9 ? rpmStep : 50.0);

        if (sp.timeBins == 0 || sp.freqBins == 0) return out;
        if (frameTimeSec.size() != sp.timeBins) return out;
        if (rpmSignal.empty() || fsRpm <= 0.0) return out;

        // 先求每帧对应 rpm
        std::vector<double> frameRpm(sp.timeBins, 0.0);
        double rMin = std::numeric_limits<double>::infinity();
        double rMax = -std::numeric_limits<double>::infinity();

        for (size_t t = 0; t < sp.timeBins; ++t) {
            const double r = InterpRpmAtTime(rpmSignal, fsRpm, frameTimeSec[t]);
            frameRpm[t] = r;
            rMin = std::min(rMin, r);
            rMax = std::max(rMax, r);
        }

        if (!std::isfinite(rMin) || !std::isfinite(rMax) || rMax < rMin) return out;

        // 扩成整步长边界
        out.rpmMin = std::floor(rMin / out.rpmStep) * out.rpmStep;
        out.rpmMax = std::ceil(rMax / out.rpmStep) * out.rpmStep;
        out.rpmBins = static_cast<size_t>(std::floor((out.rpmMax - out.rpmMin) / out.rpmStep)) + 1;
        if (out.rpmBins == 0) return out;

        out.dataDb.assign(out.rpmBins * out.freqBins, 0.0);
        std::vector<size_t> counts(out.rpmBins, 0);

        // 按 rpm bin 累加（dB 直接平均：先保持与你现有风格兼容；后续可升级功率域平均）
        for (size_t t = 0; t < sp.timeBins; ++t) {
            const double r = frameRpm[t];
            long long bi = static_cast<long long>(std::llround((r - out.rpmMin) / out.rpmStep));
            if (bi < 0) continue;
            if (bi >= static_cast<long long>(out.rpmBins)) continue;

            const size_t b = static_cast<size_t>(bi);
            for (size_t f = 0; f < out.freqBins; ++f) {
                out.at(b, f) += sp.at(t, f);
            }
            counts[b] += 1;
        }

        // 平均
        for (size_t b = 0; b < out.rpmBins; ++b) {
            if (counts[b] == 0) continue;
            const double inv = 1.0 / static_cast<double>(counts[b]);
            for (size_t f = 0; f < out.freqBins; ++f) {
                out.at(b, f) *= inv;
            }
        }

        return out;
    }

} // namespace FFTvsRpmMapper
