#pragma once
#include <string>
#include <vector>
#include "../domain/Types.h"

// UI/上层统一请求（不含任何交互逻辑）
struct RunRequest {
    // 输入
    std::vector<std::string> inputPaths;

    // 模式
    AnalysisMode mode = AnalysisMode::FFT;

    // FFT 参数
    FFTParams fftParams{};

    // ATFX 通道选择（按文件索引）
    std::vector<std::vector<size_t>> atfxSelectedChannelsByFile;

    // RPM 参数（仅 FFT_VS_RPM）
    std::vector<std::string> rpmChannelNameByFile;
    double rpmBinStep = 50.0;

    // 运行参数
    size_t maxThreads = 0;   // 0=auto
    int maxRetries = 1;
    bool enableCancel = false;

    // 新增：是否写 CSV 到磁盘
    bool writeCsvToDisk = false;
};