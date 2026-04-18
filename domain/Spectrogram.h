#pragma once
#include <vector>
#include <cstddef>

enum class SpectrogramValueType {
    Linear = 0,
    dB = 1
};

struct Spectrogram {
    double fs = 0.0;
    size_t blockSize = 0;
    size_t hopSize = 0;
    size_t timeBins = 0;
    size_t freqBins = 0;

    // 平铺存储: [t * freqBins + f]
    std::vector<double> dataLinear; // 线性幅值
    std::vector<double> dataDb;     // dB值

    // 每一帧中心时间（秒）
    std::vector<double> frameTimeSec;

    double& atLinear(size_t t, size_t f) { return dataLinear[t * freqBins + f]; }
    double  atLinear(size_t t, size_t f) const { return dataLinear[t * freqBins + f]; }

    double& atDb(size_t t, size_t f) { return dataDb[t * freqBins + f]; }
    double  atDb(size_t t, size_t f) const { return dataDb[t * freqBins + f]; }

    // 兼容旧代码（默认返回 dB）
    double& at(size_t t, size_t f) { return dataDb[t * freqBins + f]; }
    double  at(size_t t, size_t f) const { return dataDb[t * freqBins + f]; }
};