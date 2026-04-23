#pragma once
#include <string>
#include <cstddef>

struct CoreAnalysisRequest
{
    std::string filePath;
    std::string channelName;
    std::string outputDir;
    std::string analysisMode;

    size_t fftSize = 8192;
    double overlap = 0.5;
    std::string weighting = "Z";

    std::string rpmChannelName;
    double rpmBinStep = 50.0;

    int maxRetries = 1;
    size_t maxThreads = 0;
    bool enableCancel = false;

    std::string timeWeighting = "fast";
    double levelTimeConstantSec = 0.125;
    double levelWindowSec = 0.125;
    double levelOutputStepSec = 0.1;

    bool returnInMemory = true;
    bool includeFilePaths = false;

    // 新增：是否允许在磁盘写出 CSV（Qt 端应设为 false）
    bool writeCsvToDisk = false;
};