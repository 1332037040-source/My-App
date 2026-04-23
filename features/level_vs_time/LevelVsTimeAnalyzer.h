#pragma once

#include "core/common.h"
#include "domain/Types.h"
#include <string>
#include <vector>

namespace LevelVsTimeAnalyzer {

    enum class TimeWeightingMode {
        Fast,
        Slow,
        Impulse,
        Rectangle,
        Manual
    };

    struct LevelPoint {
        double timeSec = 0.0;
        double levelPa = 0.0;   // 改为Pa
    };

    struct LevelSeries {
        double fs = 0.0;
        size_t outputStepSamples = 0;
        double minLevelPa = 0.0; // 改为Pa
        double maxLevelPa = 0.0; // 改为Pa
        std::vector<LevelPoint> points;
    };

    // 将字符串解析为时间计权模式
    TimeWeightingMode ParseTimeWeighting(const std::string& s);

    // 时间计权 Level vs Time（输出单位：Pa）
    LevelSeries Compute(const DVector& x, double fs, const FFTParams& p);

} // namespace LevelVsTimeAnalyzer