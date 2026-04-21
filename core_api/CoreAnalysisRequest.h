#pragma once
#include <string>

struct CoreAnalysisRequest
{
    std::string filePath;
    std::string channelName;
    std::string outputDir;
    std::string analysisMode; // fft / fft_vs_time / fft_vs_rpm / octave / octave_1_1 / octave_1_3 / level_vs_time / level_vs_rpm

    size_t fftSize = 8192;
    double overlap = 0.5;
    std::string weighting = "Z";

    // RPM 相关（fft_vs_rpm / level_vs_rpm）
    std::string rpmChannelName;
    double rpmBinStep = 50.0;

    int maxRetries = 1;
    size_t maxThreads = 0;
    bool enableCancel = false;

    // Level vs Time / Level vs RPM 相关
    // fast / slow / rectangle / manual / impulse
    std::string timeWeighting = "fast";
    double levelTimeConstantSec = 0.125;
    double levelWindowSec = 0.125;
    double levelOutputStepSec = 0.1;
};