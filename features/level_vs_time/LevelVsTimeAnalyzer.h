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
        double levelDb = 0.0;
    };

    struct LevelSeries {
        double fs = 0.0;
        size_t outputStepSamples = 0;
        double minLevelDb = 0.0;
        double maxLevelDb = 0.0;
        std::vector<LevelPoint> points;
    };

    // 将字符串解析为时间计权模式
    TimeWeightingMode ParseTimeWeighting(const std::string& s);

    // 时间计权 Level vs Time
    //
    // 当前实现策略：
    // - Fast:      指数时间计权，短时常数，采用左镜像边界 + 中心相位取样
    // - Slow:      指数时间计权，长时常数，采用稳态均方初始化 + 因果输出
    // - Impulse:   双时间常数（上升/下降不同），采用稳态初始化
    // - Manual:    自定义指数时间常数；短tau沿用镜像策略，长tau采用稳态初始化
    // - Rectangle: 滑动均方窗（矩形窗）实现，不走一阶IIR时间计权
    //
    // 输入：
    // - x: 原始时域信号
    // - fs: 采样率
    // - p: 分析参数（频率计权、时间计权、校准值、参考值、输出步长等）
    //
    // 输出：
    // - LevelSeries，包含时间历程、最小值、最大值、输出步长等
    LevelSeries Compute(const DVector& x, double fs, const FFTParams& p);

} // namespace LevelVsTimeAnalyzer