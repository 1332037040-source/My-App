#include "FFTvsRpmMapper.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <vector>

namespace FFTvsRpmMapper {

    namespace {
        constexpr double EPS = 1e-30;

        // ===== A/B 开关 1：聚合方式 =====
        // true  -> 功率均值(先db->power��均值后再转db)
        // false -> dB直接均值
        constexpr bool kUsePowerAverage = true;

        // ===== A/B 开关 2：rpm映射方式 =====
        // true  -> 线性分摊到相邻两个rpm bin（推荐）
        // false -> 最近邻单bin归属
        constexpr bool kUseLinearRpmSplit = true;

        // ===== A/B 开关 3：可选全局dB补偿（先保持0）=====
        constexpr double kGlobalDbOffset = 0.0;

        inline double dbToPower(double db) {
            return std::pow(10.0, db / 10.0);
        }

        inline double powerToDb(double p) {
            return 10.0 * std::log10((p > EPS) ? p : EPS);
        }

        inline double applyOffset(double db) {
            return db + kGlobalDbOffset;
        }
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

        // 固定rpm轴（对齐Artemis）
        out.rpmMin = 50.0;
        out.rpmMax = 4750.0;
        out.rpmBins = static_cast<size_t>(
            std::floor((out.rpmMax - out.rpmMin) / out.rpmStep)
            ) + 1;
        if (out.rpmBins == 0) return out;

        // 权重计数（线性分摊时用权重和；最近邻时退化为计数）
        std::vector<double> weightSum(out.rpmBins, 0.0);

        // 两种累计容器（二选一使用）
        std::vector<double> accDb(out.rpmBins * out.freqBins, 0.0);
        std::vector<double> accP(out.rpmBins * out.freqBins, 0.0);

        double seenMin = std::numeric_limits<double>::infinity();
        double seenMax = -std::numeric_limits<double>::infinity();

        auto addFrameToBin = [&](size_t r, double w, size_t t) {
            if (r >= out.rpmBins || w <= 0.0) return;
            weightSum[r] += w;

            if (kUsePowerAverage) {
                for (size_t f = 0; f < out.freqBins; ++f) {
                    const double db = sp.atDb(t, f);
                    if (!std::isfinite(db)) continue;
                    if (db <= -300.0) continue; // 忽略无效/填充值
                    accP[r * out.freqBins + f] += w * dbToPower(db);
                }
            }
            else {
                for (size_t f = 0; f < out.freqBins; ++f) {
                    const double db = sp.atDb(t, f);
                    if (!std::isfinite(db)) continue;
                    if (db <= -300.0) continue;
                    accDb[r * out.freqBins + f] += w * db;
                }
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
                const double a = x - static_cast<double>(r0); // [0,1)

                // 分摊到相邻两bin，越界自动丢弃
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

        out.dataDb.assign(out.rpmBins * out.freqBins, -300.0);

        for (size_t r = 0; r < out.rpmBins; ++r) {
            const double w = weightSum[r];
            if (w <= 0.0) continue;

            const double invW = 1.0 / w;

            if (kUsePowerAverage) {
                for (size_t f = 0; f < out.freqBins; ++f) {
                    const double meanP = accP[r * out.freqBins + f] * invW;
                    out.at(r, f) = applyOffset(powerToDb(meanP));
                }
            }
            else {
                for (size_t f = 0; f < out.freqBins; ++f) {
                    const double meanDb = accDb[r * out.freqBins + f] * invW;
                    out.at(r, f) = applyOffset(meanDb);
                }
            }
        }

        std::cout << "[DEBUG] FFTvsRpmMapper:"
            << " usePowerAverage=" << (kUsePowerAverage ? 1 : 0)
            << ", useLinearRpmSplit=" << (kUseLinearRpmSplit ? 1 : 0)
            << ", globalDbOffset=" << kGlobalDbOffset
            << ", rpmMin=" << out.rpmMin
            << ", rpmMax=" << out.rpmMax
            << ", rpmStep=" << out.rpmStep
            << ", rpmBins=" << out.rpmBins
            << ", timeBins=" << sp.timeBins
            << ", freqBins=" << sp.freqBins
            << ", seenRpm=[" << seenMin << ", " << seenMax << "]"
            << std::endl;

        std::cout << "[DEBUG] FFTvsRpmMapper non-empty bins:";
        for (size_t r = 0; r < out.rpmBins; ++r) {
            if (weightSum[r] > 0.0) {
                const double rpm = out.rpmMin + static_cast<double>(r) * out.rpmStep;
                std::cout << " (" << rpm << ":" << weightSum[r] << ")";
            }
        }
        std::cout << std::endl;

        return out;
    }

} // namespace FFTvsRpmMapper