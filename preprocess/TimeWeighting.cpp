#include "TimeWeighting.h"

#include <algorithm>
#include <cmath>

namespace TimeWeighting {

    namespace {
        constexpr double EPS = 1e-30;

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
    }

    DVector applySingleTau(
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

    // 仅展示 applyImpulse 替换函数
    DVector applyImpulse(
        const DVector& power,
        double fs,
        size_t initWindowSamples)
    {
        DVector y;
        if (power.empty() || fs <= 0.0) return y;

        y.resize(power.size(), EPS);

        constexpr double tauRise = 0.035; // DIN/IEC impulse rise
        constexpr double tauFall = 1.5;   // DIN/IEC impulse decay

        const double dt = 1.0 / fs;
        const double aRise = std::exp(-dt / tauRise);
        const double aFall = std::exp(-dt / tauFall);

        const size_t nInit = std::max<size_t>(1, initWindowSamples);
        const size_t initEnd = std::min(nInit, power.size());

        double state = meanPower(power, 0, initEnd);
        if (state < EPS) state = EPS;

        // 可选：给 impulse 也做一个短 warmup（建议开）
        {
            const size_t nWarm = std::max<size_t>(1, static_cast<size_t>(std::llround(4.0 * tauFall * fs)));
            for (size_t i = 0; i < nWarm; ++i) {
                // 预热到初值，避免起点突变
                state = aFall * state + (1.0 - aFall) * state;
                if (state < EPS) state = EPS;
            }
        }

        y[0] = state;

        // ===== 核心：改成 peak-like 语义 =====
        constexpr bool kPeakLikeRise = true;      // true: 上升直接跟随
        constexpr bool kDecayUseInput = true;     // true: 衰减时考虑输入；false: 纯指数衰减

        for (size_t n = 1; n < power.size(); ++n) {
            const double in = std::max(power[n], EPS);

            if (in >= state) {
                if (kPeakLikeRise) state = in;
                else state = aRise * state + (1.0 - aRise) * in;
            }
            else {
                if (kDecayUseInput) state = aFall * state + (1.0 - aFall) * in;
                else state = aFall * state;
            }

            if (state < EPS) state = EPS;
            y[n] = state;
        }

        return y;
    }
    DVector applyByMode(
        const DVector& power,
        double fs,
        Mode mode,
        double manualTauSec,
        size_t impulseInitWindowSamples,
        bool doWarmup)
    {
        switch (mode) {
        case Mode::Fast:
            return applySingleTau(power, fs, 0.125, doWarmup);
        case Mode::Slow:
            return applySingleTau(power, fs, 1.0, doWarmup);
        case Mode::Impulse:
            return applyImpulse(power, fs, impulseInitWindowSamples);
        case Mode::Manual:
        default: {
            const double tau = (manualTauSec > 0.0) ? manualTauSec : 0.125;
            return applySingleTau(power, fs, tau, doWarmup);
        }
        }
    }

} // namespace TimeWeighting