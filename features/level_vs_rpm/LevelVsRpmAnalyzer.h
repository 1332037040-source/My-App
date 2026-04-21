#pragma once
#include "../level_vs_time/LevelVsTimeAnalyzer.h"
#include "../../domain/Types.h"
#include <vector>

struct LevelRpmPoint {
    double rpm = 0.0;
    double levelDb = 0.0;
};

struct LevelVsRpmSeries {
    double rpmMin = 0.0;
    double rpmMax = 0.0;
    double rpmStep = 50.0;
    std::vector<LevelRpmPoint> points;
};

namespace LevelVsRpmAnalyzer {
    LevelVsRpmSeries ComputeFromLevelSeries(
        const LevelVsTimeAnalyzer::LevelSeries& levelSeries, // ’‚¿Ô∏ƒ
        const std::vector<double>& rpmSamples,
        double rpmFs,
        double rpmBinStep);
}