#pragma once
#include "core/common.h"
#include <cstddef>

namespace TimeWeighting {

    // 指数时间计权模式（不含 Rectangle）
    enum class Mode {
        Fast,
        Slow,
        Impulse,
        Manual
    };

    // 单时间常数指数时间计权（Fast/Slow/Manual）
    // power: 输入功率序列（Pa^2）
    // fs   : 采样率
    // tau  : 时间常数(s)
    // doWarmup: 是否执行 warmup
    DVector applySingleTau(
        const DVector& power,
        double fs,
        double tau,
        bool doWarmup = true
    );

    // Impulse 双时间常数计权（35ms rise / 1.5s fall）
    // initWindowSamples: 初始均值窗口长度（样本数）
    DVector applyImpulse(
        const DVector& power,
        double fs,
        size_t initWindowSamples
    );

    // 按模式统一调用（Fast/Slow/Impulse/Manual）
    DVector applyByMode(
        const DVector& power,
        double fs,
        Mode mode,
        double manualTauSec,
        size_t impulseInitWindowSamples,
        bool doWarmup = true
    );

} // namespace TimeWeighting