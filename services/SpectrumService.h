#pragma once

#include "../domain/Types.h"
#include <string>
#include <vector>

class SpectrumService {
public:
    // 计算平均 FFT（从 job.params 读取 block/overlap/window/amp scaling）
    bool ComputeAveragedFFT(const SignalData& sig,
        const Job& job,
        CVector& outFft,
        std::string& err) const;

    // 频域加权（从 job.params.weight_type 读取）
    void ApplyWeighting(CVector& fft,
        double fs,
        const Job& job) const;

    // 一步完成：平均 FFT + 加权
    bool ComputeWeightedAveragedFFT(const SignalData& sig,
        const Job& job,
        CVector& outFft,
        std::string& err) const;

    // 从 FFT 结果计算峰值
    PeakPreview CalcPeakFromFFT(const CVector& fft, double fs) const;

    // 通用峰值计算：给定幅值数组 + 对应频率数组
    PeakPreview CalcPeakFromValues(const std::vector<double>& mags,
        const std::vector<double>& freqs) const;
};