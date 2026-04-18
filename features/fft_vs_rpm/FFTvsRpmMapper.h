#pragma once
#include "domain/Spectrogram.h"
#include "domain/RpmSpectrogram.h"
#include <vector>

namespace FFTvsRpmMapper {

    // 线性插值：根据时刻 tSec，从 rpmSignal（等采样率 fsRpm）取 rpm 值
    double InterpRpmAtTime(
        const std::vector<double>& rpmSignal,
        double fsRpm,
        double tSec
    );

    // 将时频谱 Spectrogram[t,f] 按 rpm 分箱，映射为 RpmSpectrogram[r,f]
    // 输入约定：
    // - sp.dataDb 为 dB 值（与现有 FFT vs time 输出一致）
    // - frameTimeSec.size() == sp.timeBins
    // - rpmSignal 与 frameTimeSec 对齐到同一时间轴（通过 fsRpm + 插值）
    RpmSpectrogram MapTimeToRpm(
        const Spectrogram& sp,
        const std::vector<double>& frameTimeSec,
        const std::vector<double>& rpmSignal,
        double fsRpm,
        double rpmStep
    );
}