#pragma once
#include <vector>
#include <cstddef>

struct RpmSpectrogram {
    double fs = 0.0;
    size_t blockSize = 0;
    size_t freqBins = 0;

    double rpmMin = 0.0;
    double rpmMax = 0.0;
    double rpmStep = 50.0;
    size_t rpmBins = 0;

    // 扁平存储：dataDb[r * freqBins + f]
    std::vector<double> dataDb;

    double& at(size_t r, size_t f) { return dataDb[r * freqBins + f]; }
    double  at(size_t r, size_t f) const { return dataDb[r * freqBins + f]; }
};