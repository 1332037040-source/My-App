#pragma once
#include "../../domain/Types.h"
#include "../../core/common.h"
#include <vector>

struct LevelRpmPoint {
    double rpm = 0.0;
    double levelPa = 0.0;
};

struct LevelVsRpmSeries {
    double rpmMin = 0.0;
    double rpmMax = 0.0;
    double rpmStep = 50.0;
    std::vector<LevelRpmPoint> points;
};

namespace LevelVsRpmAnalyzer {

    // 直接从原始主信号 + RPM 计算 Level vs RPM（Pa）
    // 内部会执行：去直流 -> 频率计权(可选) -> 功率 -> 时间计权(F/S/I/Manual/Rectangle) -> 对齐RPM -> 分箱
    LevelVsRpmSeries Compute(
        const DVector& signalSamples,
        double signalFs,
        const FFTParams& p,
        const std::vector<double>& rpmSamples,
        double rpmFs,
        double rpmBinStep
    );

    // 保留：若外部已有 Level(t,Pa) 序列，可直接映射到 RPM 分箱
    struct LevelPointLite {
        double timeSec = 0.0;
        double levelPa = 0.0;
    };

    LevelVsRpmSeries ComputeFromLevelSeries(
        const std::vector<LevelPointLite>& levelPoints,
        const std::vector<double>& rpmSamples,
        double rpmFs,
        double rpmBinStep
    );
}